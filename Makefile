PROJECT  := zuno
VERSION  := 0.1
# export MallocStackLogging=1
# leaks --atExit --leak-check=full --track-origins=yes
ROOT     := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
LIBS     := $(ROOT)/../libs
INCLUDE  := $(ROOT)/inc
BUILD    := $(ROOT)/build

SRC      := $(wildcard src/*.c)
SRCS     := $(filter-out src/main.c, $(SRC))

# Utils (standalone demo binaries — link against debug objects)
UTILS_SRC := $(wildcard utils/*.c)
UTILS_DIR := $(BUILD)/utils
UTILS_BIN := $(patsubst utils/%.c, $(UTILS_DIR)/%, $(UTILS_SRC))
DEMO_SH   := $(UTILS_DIR)/demo.sh

# Release build dirs and objects
REL_DIR  := $(BUILD)/release
REL_OBJ  := $(patsubst src/%.c, $(REL_DIR)/%.o, $(SRCS))
REL_MAIN := $(REL_DIR)/main.o

# Debug build dirs and objects
DBG_DIR  := $(BUILD)/debug
DBG_OBJ  := $(patsubst src/%.c, $(DBG_DIR)/%.o, $(SRCS))
DBG_MAIN := $(DBG_DIR)/main.o

# Tests (link against debug objects)
TEST_SRC := $(wildcard tests/test_*.c)
TEST_BIN := $(patsubst tests/test_%.c, $(BUILD)/test_%, $(TEST_SRC))

# Coverage build dirs and objects (instrumented with --coverage)
COV_SRC_DIR  := $(BUILD)/cov/src
COV_TST_DIR  := $(BUILD)/cov/tests
COV_SRC_OBJ  := $(patsubst src/%.c,        $(COV_SRC_DIR)/%.o, $(SRCS))
COV_TST_OBJ  := $(patsubst tests/test_%.c, $(COV_TST_DIR)/%.o, $(TEST_SRC))
COV_BIN      := $(patsubst tests/test_%.c, $(BUILD)/cov/test_%, $(TEST_SRC))

CC       := gcc

MARCH        ?= -march=native
BASE_CFLAGS  := -pedantic -Wall -Wextra $(MARCH)
BASE_CFLAGS  += -I/usr/local/include
BASE_CFLAGS  += -I/opt/homebrew/include
BASE_CFLAGS  += -fmacro-prefix-map=$(ROOT)=.
BASE_CFLAGS  += -I$(INCLUDE)
BASE_CFLAGS  += -I$(LIBS)/liboqs/build/include

REL_CFLAGS   := $(BASE_CFLAGS) -O2
DBG_CFLAGS   := $(BASE_CFLAGS) -g -DDEBUG
COV_CFLAGS   := $(BASE_CFLAGS) -g -DDEBUG --coverage

# liboqs must precede OpenSSL: liboqs.a references EVP symbols from libcrypto,
# so GNU ld requires -loqs before -lcrypto/-lssl (left-to-right resolution).
LDFLAGS  += -L$(LIBS)/liboqs/build/lib -loqs
LDFLAGS  += -L/usr/local/lib -lcrypto -lssl

.DEFAULT_GOAL = all

.PHONY: all release debug test_bins test coverage check utils clean help

all: release debug utils

# Build all test binaries without running them (used by CI build job).
test_bins: $(TEST_BIN)

utils: $(UTILS_DIR) $(UTILS_BIN) $(DEMO_SH)

$(UTILS_DIR):
	mkdir -p $(UTILS_DIR)

$(UTILS_DIR)/%: utils/%.c $(DBG_OBJ) | $(UTILS_DIR) debug
	$(CC) $(DBG_CFLAGS) -o $@ $< $(DBG_OBJ) $(LDFLAGS)

$(DEMO_SH): utils/demo.sh | $(UTILS_DIR)
	cp utils/demo.sh $@
	chmod +x $@

release: $(REL_DIR) $(REL_OBJ) $(REL_MAIN)
	@echo "Linking $(PROJECT) release $(VERSION)..."
	$(CC) $(REL_CFLAGS) -o $(BUILD)/$(PROJECT) $(REL_OBJ) $(REL_MAIN) $(LDFLAGS)

debug: $(DBG_DIR) $(DBG_OBJ) $(DBG_MAIN)
	@echo "Linking $(PROJECT) debug $(VERSION)..."
	$(CC) $(DBG_CFLAGS) -o $(BUILD)/$(PROJECT)-debug $(DBG_OBJ) $(DBG_MAIN) $(LDFLAGS)

$(REL_DIR):
	mkdir -p $(REL_DIR)

$(DBG_DIR):
	mkdir -p $(DBG_DIR)

$(REL_DIR)/%.o: src/%.c
	$(CC) $(REL_CFLAGS) -fPIC -c $< -o $@

$(DBG_DIR)/%.o: src/%.c
	$(CC) $(DBG_CFLAGS) -fPIC -c $< -o $@

# Tests use debug objects (no main)
$(BUILD)/test_%.o: tests/test_%.c | $(DBG_DIR)
	$(CC) $(DBG_CFLAGS) -fPIC -c $< -o $@

$(BUILD)/test_%: $(BUILD)/test_%.o $(DBG_OBJ) | debug
	$(CC) $(DBG_CFLAGS) -o $@ $< $(DBG_OBJ) $(LDFLAGS) -L/opt/homebrew/lib -lcmocka

# Coverage objects (src/ and tests/ compiled with --coverage)
$(COV_SRC_DIR):
	mkdir -p $(COV_SRC_DIR)

$(COV_TST_DIR):
	mkdir -p $(COV_TST_DIR)

$(COV_SRC_DIR)/%.o: src/%.c | $(COV_SRC_DIR)
	$(CC) $(COV_CFLAGS) -fPIC -c $< -o $@

$(COV_TST_DIR)/%.o: tests/test_%.c | $(COV_TST_DIR)
	$(CC) $(COV_CFLAGS) -fPIC -c $< -o $@

$(BUILD)/cov/test_%: $(COV_TST_DIR)/%.o $(COV_SRC_OBJ)
	$(CC) $(COV_CFLAGS) --coverage -o $@ $< $(COV_SRC_OBJ) $(LDFLAGS) -L/opt/homebrew/lib -lcmocka

# ── test ────────────────────────────────────────────────────────────────────
#
# Runs every test suite even if one fails, then prints an aggregated summary:
#
#   Suite                                   Passed  Failed   Total
#   ──────────────────────────────────────────────────────────────
#   test_block                                  12       0      12
#   ...
#   ──────────────────────────────────────────────────────────────
#   TOTAL                                      115       0     115
#
# Pass/fail counts are extracted from cmocka's group-summary lines:
#   "[  PASSED  ] N test(s)."  and  "[  FAILED  ] N test(s)."
#
test: $(TEST_BIN)
	@echo "Running tests..."
	@tmpdir=$$(mktemp -d); \
	trap 'rm -rf "$$tmpdir"' EXIT; \
	failures=0; total_pass=0; total_fail=0; \
	SEP="  ──────────────────────────────────────────────────────────────"; \
	for t in $(TEST_BIN); do \
		name=$$(basename $$t); \
		out=$$tmpdir/$$name; \
		$$t > $$out 2>&1; rc=$$?; \
		cat $$out; \
		[ $$rc -ne 0 ] && failures=$$((failures + 1)); \
		p=$$(awk '/\[  PASSED  \]/{match($$0,/[0-9]+/); s+=substr($$0,RSTART,RLENGTH)} END{print s+0}' $$out); \
		f=$$(awk '/\[  FAILED  \].*test\(s\)/{match($$0,/[0-9]+/); s+=substr($$0,RSTART,RLENGTH)} END{print s+0}' $$out); \
		total_pass=$$((total_pass + p)); \
		total_fail=$$((total_fail + f)); \
		printf "  %-40s %6d  %6d  %6d\n" "$$name" $$p $$f $$((p+f)) >> $$tmpdir/rows; \
	done; \
	echo ""; \
	printf "  %-40s %6s  %6s  %6s\n" "Suite" "Passed" "Failed" "Total"; \
	echo "$$SEP"; \
	cat $$tmpdir/rows 2>/dev/null || true; \
	echo "$$SEP"; \
	printf "  %-40s %6d  %6d  %6d\n\n" "TOTAL" $$total_pass $$total_fail $$((total_pass+total_fail)); \
	if [ $$failures -eq 0 ]; then \
		echo "  All $$((total_pass+total_fail)) tests passed."; \
	else \
		echo "  $$failures test suite(s) FAILED  ($$total_fail test(s) failed)." >&2; \
	fi; \
	echo ""; \
	[ $$failures -eq 0 ]

# ── coverage ─────────────────────────────────────────────────────────────────
#
# Builds all source and test files with --coverage (gcov-compatible profiling),
# runs every test suite, then reports line coverage per source file.
#
# Requires lcov for a full report:
#   brew install lcov
# Falls back to raw gcov output if lcov is not installed.
#
# HTML report (when lcov is available):  build/cov/html/index.html
#
coverage: $(COV_BIN)
	@echo "Running tests with coverage instrumentation..."
	@for t in $(COV_BIN); do $$t > /dev/null 2>&1 || true; done
	@echo ""
	@if command -v lcov > /dev/null 2>&1; then \
		lcov --capture \
		     --directory $(COV_SRC_DIR) \
		     --output-file $(BUILD)/cov/coverage.info \
		     --quiet; \
		lcov --remove $(BUILD)/cov/coverage.info \
		     '/usr/*' '/opt/*' '*/tests/*' \
		     --output-file $(BUILD)/cov/coverage.info \
		     --quiet; \
		lcov --summary $(BUILD)/cov/coverage.info; \
		genhtml $(BUILD)/cov/coverage.info \
		        --output-directory $(BUILD)/cov/html \
		        --quiet; \
		echo "  HTML report: $(BUILD)/cov/html/index.html"; \
	else \
		echo "  File                               Lines"; \
		echo "  ─────────────────────────────────────────"; \
		for src in $(SRCS); do \
			base=$$(basename $$src .c); \
			gcov -o $(COV_SRC_DIR) $$src 2>&1 \
			| awk -v f="$$src" \
			  '/^Lines executed:/{printf "  %-34s %s\n", f, $$0}'; \
		done; \
		rm -f *.gcov; \
		echo ""; \
		echo "  Install lcov for a full report and HTML output:"; \
		echo "    brew install lcov"; \
	fi

# ── check ────────────────────────────────────────────────────────────────────
#
# Full validation: run the test suite (fast, debug objects) then rebuild with
# --coverage and report line coverage.
#
# Use make test  alone during development for rapid feedback.
# Use make check before tagging a release or when auditing coverage gaps.
#
check: test coverage

clean:
	rm -rf $(BUILD)
	rm -rf .chain
	find . -type f -name "compile_commands.json" -exec rm -f {} \;

help:
	@echo "$(PROJECT) $(VERSION)"
	@echo ""
	@echo "Usage: make [target]"
	@echo ""
	@echo "Targets:"
	@echo "  all       Build release, debug, and utility binaries (default)"
	@echo "  release   Build optimized binary (-O2) -> build/$(PROJECT)"
	@echo "  debug     Build debug binary (-g -DDEBUG) -> build/$(PROJECT)-debug"
	@echo "  test_bins Build test binaries without running them (CI build stage)"
	@echo "  test      Build and run all test suites; print aggregated summary"
	@echo "  coverage  Rebuild with --coverage; report line coverage per file"
	@echo "              (brew install lcov for full report + HTML output)"
	@echo "  check     Run both test and coverage (full validation)"
	@echo "  utils     Build demo utility binaries -> build/utils/"
	@echo "  clean     Remove build artifacts and .chain data"
	@echo "  help      Show this help message"

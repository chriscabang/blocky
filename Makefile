PROJECT  := blocky
VERSION  := 0.1
# export MallocStackLogging=1
# leaks --atExit --leak-check=full --track-origins=yes
ROOT     := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
LIBS     := $(ROOT)/../libs
INCLUDE  := $(ROOT)/inc
BUILD    := $(ROOT)/build

SRC      := $(wildcard src/*.c)
# UTILS    := $(wildcard utils/*.c)
SRCS     := $(filter-out src/main.c, $(SRC))

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

CC       := gcc

BASE_CFLAGS  := -pedantic -Wall -Wextra -march=native
BASE_CFLAGS  += -I/usr/local/include
BASE_CFLAGS  += -I/opt/homebrew/include
BASE_CFLAGS  += -fmacro-prefix-map=$(ROOT)=.
BASE_CFLAGS  += -I$(INCLUDE)
BASE_CFLAGS  += -I$(LIBS)/liboqs/build/include

REL_CFLAGS   := $(BASE_CFLAGS) -O2
DBG_CFLAGS   := $(BASE_CFLAGS) -g -DDEBUG

LDFLAGS  += -L/usr/local/lib -lcrypto -lssl
LDFLAGS  += -L$(LIBS)/liboqs/build/lib -loqs

.DEFAULT_GOAL = all

.PHONY: all release debug test clean help

all: release debug

release: $(REL_DIR) $(REL_OBJ) $(REL_MAIN)
	@echo "Linking $(PROJECT) release $(VERSION)..."
	$(CC) $(REL_CFLAGS) $(LDFLAGS) -o $(BUILD)/$(PROJECT) $(REL_OBJ) $(REL_MAIN)

debug: $(DBG_DIR) $(DBG_OBJ) $(DBG_MAIN)
	@echo "Linking $(PROJECT) debug $(VERSION)..."
	$(CC) $(DBG_CFLAGS) $(LDFLAGS) -o $(BUILD)/$(PROJECT)-debug $(DBG_OBJ) $(DBG_MAIN)

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
	$(CC) $(DBG_CFLAGS) $(LDFLAGS) -o $@ $< $(DBG_OBJ) -L/opt/homebrew/lib -lcmocka

# Build and run unit tests
test: $(TEST_BIN)
	@echo "Running tests..."
	@for test in $(TEST_BIN); do \
		$$test || exit 1; \
	done
	@echo "All tests passed."

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
	@echo "  all      Build both release and debug binaries (default)"
	@echo "  release  Build optimized binary (-O2) -> build/$(PROJECT)"
	@echo "  debug    Build debug binary (-g -DDEBUG) -> build/$(PROJECT)-debug"
	@echo "  test     Build and run unit tests using debug objects"
	@echo "  clean    Remove build artifacts and .chain data"
	@echo "  help     Show this help message"

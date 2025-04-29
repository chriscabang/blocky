PROJECT  := cutechain
VERSION  := 0.1
# export MallocStackLogging=1
# leaks --atExit --leak-check=full --track-origins=yes
ROOT     := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
MAIN     := src/main.o
SRC      := $(wildcard src/*.c)
# UTILS    := $(wildcard utils/*.c)
OBJ      := $(filter-out src/main.o, $(SRC:.c=.o))
LIBS     := $(ROOT)/../libs

INCLUDE  := $(ROOT)/inc
BUILD    := $(ROOT)/build

TEST_SRC := $(wildcard tests/test_*.c)
TEST_OBJ := $(patsubst tests/test_%.c, $(BUILD)/test_%.o, $(TEST_SRC))
TEST_BIN := $(patsubst tests/test_%.c, $(BUILD)/test_%, $(TEST_SRC))

DEBUG    := 1

CC       := gcc

CFLAGS   += -pedantic -Wall -Wextra -march=native
CFLAGS   += -I/usr/local/include
CFLAGS   += -I/opt/homebrew/include
CFLAGS   += -I$(INCLUDE)
CFLAGS   += -I$(LIBS)/liboqs/build/include

LDFLAGS  += -L/usr/local/lib -lcrypto -lssl
LDFLAGS  += -L$(LIBS)/liboqs/build/lib -loqs

ifdef DEBUG
	CFLAGS  += -g -DDEBUG=$(DEBUG)
endif

.DEFAULT_GOAL = all

prerequisites:
	mkdir -p $(BUILD)

$(CC): prerequisites
	@echo "Compiling $(PROJECT) version $(VERSION)..."

# Build the main blockchain executable
all: prerequisites cutechain

cutechain: $(OBJ) $(MAIN)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(BUILD)/$@ $^

%.o: %.c
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(BUILD)/test_%: tests/test_%.o $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -L/opt/homebrew/lib -lcmocka

# Build Unit Tests
test: $(TEST_BIN)
	@echo "Runnint tests..."
	@for test in $(TEST_BIN); do \
		$$test || exit 1; \
	done
	@echo "All tests passed."

clean:
	rm -rf $(BUILD)
	rm -rf .chain
	find . -type f -name "*.o" -exec rm -f {} \;
	find . -type f -name "compile_commands.json" -exec rm -f {} \;

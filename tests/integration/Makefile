# Integration Tests Makefile
# Tests full system integration and cross-module functionality

include ../common/common.mk

# Project directories
ROOTDIR=../..
SRCDIR=$(ROOTDIR)/src/c
INCDIR=$(ROOTDIR)/include
COMMON_DIR=../common
HELPERS_DIR=../helpers

# Tools - Use common test compiler settings
CC=$(TEST_CC)
CFLAGS=$(TEST_CFLAGS) -I$(INCDIR) -I$(COMMON_DIR) -I$(HELPERS_DIR)

# Test targets - updated for new structure
TESTS=test_integ_memory
TEST_EXES=$(TESTS:%=%.exe)

.PHONY: all clean test test-all run-tests

all: test-dirs $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS) $(TESTS)
	@echo "All integration tests built successfully"

test: all
	@echo "Running integration tests..."
	@for test in $(TESTS); do \
		echo "Running $$test..."; \
		./$$test || echo "$$test FAILED"; \
	done

test-all: test

run-tests: test

# Memory Integration Test
test_integ_memory: test_integ_memory.o $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)
	@echo "Linking memory integration test..."
	$(CC) $(CFLAGS) $^ -o $@

test_integ_memory.o: test_integ_memory.c $(COMMON_HEADERS)
	@echo "Compiling memory integration test..."
	$(CC) $(CFLAGS) -c $< -o $@

# Build common dependencies
$(COMMON_TEST_OBJS):
	@echo "Building common test framework..."
	@$(MAKE) -C $(COMMON_DIR)

$(HELPER_TEST_OBJS):
	@echo "Building test helpers..."
	@$(MAKE) -C $(HELPERS_DIR)

clean:
	@echo "Cleaning integration test artifacts..."
	@rm -f *.o *.exe $(TESTS)
	@$(MAKE) -C $(COMMON_DIR) clean
	@$(MAKE) -C $(HELPERS_DIR) clean
# Test Runner Compilation Makefile
# This file handles compilation of test runners and execution coordination

include ../common/common.mk

# Runner-specific settings
RUNNER_CFLAGS = $(TEST_CFLAGS) -DTEST_RUNNER
RUNNER_LDFLAGS = -L../helpers -L../../build -lm
COMMON_DIR = ../common
HELPERS_DIR = ../helpers

# Source files - specific runner implementations
RUNNER_SOURCES = runner_main.c runner_unit.c runner_integration.c runner_performance.c \
                 runner_stress.c runner_drivers.c runner_protocols.c
RUNNER_OBJECTS = $(RUNNER_SOURCES:.c=.o)
RUNNER_TARGETS = $(RUNNER_SOURCES:.c=)

# Dependencies
HELPER_LIBS = $(HELPER_TEST_OBJS)

# Default target
all: test-dirs $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS) runners
	@echo "All test runners built successfully"

# Build all runners
runners: $(RUNNER_TARGETS)
	@echo "Test runners compilation complete"

# Specific runner compilation rules
runner_main: runner_main.o $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)
	@echo "Linking main test runner..."
	$(TEST_CC) $(RUNNER_CFLAGS) $^ -o $@ $(RUNNER_LDFLAGS)

runner_unit: runner_unit.o $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)
	@echo "Linking unit test runner..."
	$(TEST_CC) $(RUNNER_CFLAGS) $^ -o $@ $(RUNNER_LDFLAGS)

runner_integration: runner_integration.o $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)
	@echo "Linking integration test runner..."
	$(TEST_CC) $(RUNNER_CFLAGS) $^ -o $@ $(RUNNER_LDFLAGS)

runner_performance: runner_performance.o $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)
	@echo "Linking performance test runner..."
	$(TEST_CC) $(RUNNER_CFLAGS) $^ -o $@ $(RUNNER_LDFLAGS)

runner_stress: runner_stress.o $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)
	@echo "Linking stress test runner..."
	$(TEST_CC) $(RUNNER_CFLAGS) $^ -o $@ $(RUNNER_LDFLAGS)

runner_drivers: runner_drivers.o $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)
	@echo "Linking driver test runner..."
	$(TEST_CC) $(RUNNER_CFLAGS) $^ -o $@ $(RUNNER_LDFLAGS)

runner_protocols: runner_protocols.o $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)
	@echo "Linking protocol test runner..."
	$(TEST_CC) $(RUNNER_CFLAGS) $^ -o $@ $(RUNNER_LDFLAGS)

# Object file compilation
%.o: %.c $(COMMON_HEADERS)
	@echo "Compiling runner: $<"
	$(TEST_CC) $(RUNNER_CFLAGS) -c $< -o $@

# Build common dependencies
$(COMMON_TEST_OBJS):
	@echo "Building common test framework..."
	@$(MAKE) -C $(COMMON_DIR)

$(HELPER_TEST_OBJS):
	@echo "Building test helpers..."
	@$(MAKE) -C $(HELPERS_DIR)

# Run all tests
run-tests: runners
	@echo "Running all test runners..."
	@echo "============================"
	@echo "1. Main Test Runner:"
	@echo "-------------------"
	-./runner_main
	@echo ""
	@echo "2. Unit Test Runner:"
	@echo "-------------------"
	-./runner_unit
	@echo ""
	@echo "3. Integration Test Runner:"
	@echo "---------------------------"
	-./runner_integration
	@echo ""
	@echo "4. Performance Test Runner:"
	@echo "---------------------------"
	-./runner_performance
	@echo ""
	@echo "5. Stress Test Runner:"
	@echo "---------------------"
	-./runner_stress
	@echo ""
	@echo "6. Driver Test Runner:"
	@echo "---------------------"
	-./runner_drivers
	@echo ""
	@echo "7. Protocol Test Runner:"
	@echo "-----------------------"
	-./runner_protocols
	@echo ""
	@echo "All test runners completed."

# Clean runners
clean:
	@echo "Cleaning test runners..."
	@rm -f *.o $(RUNNER_TARGETS)
	@$(MAKE) -C $(COMMON_DIR) clean
	@$(MAKE) -C $(HELPERS_DIR) clean
	@echo "Test runners clean complete"

.PHONY: all runners run-tests clean test
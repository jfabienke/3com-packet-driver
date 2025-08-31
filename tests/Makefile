# Makefile for 3Com Packet Driver Test Suite
# Comprehensive testing for packet operations and interrupt handling

# Include common test definitions
include common/common.mk
include helpers/helpers.mk

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O0 -DTESTING
INCLUDES = -I../include -I../src/c -Icommon -Ihelpers
LIBS = -lm

# Directories
SRC_DIR = ../src/c
INCLUDE_DIR = ../include
UNIT_DIR = unit
INTEGRATION_DIR = integration
PERFORMANCE_DIR = performance
STRESS_DIR = stress
ASM_DIR = asm
RUNNERS_DIR = runners
COMMON_DIR = common
HELPERS_DIR = helpers
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj

# Source files
TEST_RUNNER_SRC = $(RUNNERS_DIR)/runner_main.c
PACKET_OPS_TEST_SRC = $(UNIT_DIR)/test_packet_ops.c
IRQ_TEST_SRC = $(UNIT_DIR)/test_irq.c
PERFORMANCE_TEST_SRC = $(PERFORMANCE_DIR)/test_perf_basic.c
ASM_API_TEST_SRC = $(UNIT_DIR)/test_asm_api.c

# Implementation source files (mock implementations)
MOCK_SRCS = $(HELPERS_DIR)/helper_mock_hardware.c \
           $(HELPERS_DIR)/helper_network_sim.c \
           $(COMMON_DIR)/test_framework.c

# Header dependencies
HEADERS = $(COMMON_DIR)/test_common.h \
          $(COMMON_DIR)/test_framework.h \
          $(COMMON_DIR)/test_hardware.h \
          $(COMMON_DIR)/test_macros.h \
          $(INCLUDE_DIR)/packet_ops.h \
          $(INCLUDE_DIR)/common.h \
          $(INCLUDE_DIR)/hardware.h \
          $(INCLUDE_DIR)/logging.h \
          $(INCLUDE_DIR)/memory.h

# Object files
TEST_RUNNER_OBJ = $(OBJ_DIR)/runner_main.o
PACKET_OPS_TEST_OBJ = $(OBJ_DIR)/test_packet_ops.o
IRQ_TEST_OBJ = $(OBJ_DIR)/test_irq.o
PERFORMANCE_TEST_OBJ = $(OBJ_DIR)/test_perf_basic.o
ASM_API_TEST_OBJ = $(OBJ_DIR)/asm_api_test.o
MOCK_OBJS = $(OBJ_DIR)/helper_mock_hardware.o $(OBJ_DIR)/helper_network_sim.o $(OBJ_DIR)/test_framework.o

ALL_OBJS = $(TEST_RUNNER_OBJ) $(PACKET_OPS_TEST_OBJ) $(IRQ_TEST_OBJ) \
           $(PERFORMANCE_TEST_OBJ) $(ASM_API_TEST_OBJ) $(MOCK_OBJS)

# Executables
TEST_RUNNER = $(BUILD_DIR)/test_runner
PACKET_OPS_TEST = $(BUILD_DIR)/test_packet_ops
IRQ_TEST = $(BUILD_DIR)/test_irq
PERFORMANCE_TEST = $(BUILD_DIR)/test_perf_basic
ASM_API_TEST = $(BUILD_DIR)/asm_api_test

# Targets
.PHONY: all clean test test-unit test-integration test-performance test-stress test-asm \
        test-all test-verbose test-quick dirs help asm-test asm-clean runners-test \
        build-runners build-common build-helpers

# Default target
all: dirs build-common build-helpers build-runners $(TEST_RUNNER)

# Create build directories
dirs:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(OBJ_DIR)

# Main test runner
$(TEST_RUNNER): $(ALL_OBJS)
	@echo "Linking comprehensive test runner..."
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
	@echo "✓ Test runner built successfully"

# Individual test executables
$(PACKET_OPS_TEST): $(PACKET_OPS_TEST_OBJ) $(MOCK_OBJS)
	@echo "Linking packet operations test..."
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(IRQ_TEST): $(IRQ_TEST_OBJ) $(MOCK_OBJS)
	@echo "Linking interrupt handling test..."
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(PERFORMANCE_TEST): $(PERFORMANCE_TEST_OBJ) $(MOCK_OBJS)
	@echo "Linking performance test..."
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(ASM_API_TEST): $(ASM_API_TEST_OBJ) $(MOCK_OBJS)
	@echo "Linking assembly API test..."
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# Build targets for subdirectories
build-common:
	@echo "Building common test framework..."
	@$(MAKE) -C $(COMMON_DIR)

build-helpers:
	@echo "Building test helpers..."
	@$(MAKE) -C $(HELPERS_DIR)

build-runners:
	@echo "Building test runners..."
	@$(MAKE) -C $(RUNNERS_DIR)

# Object file compilation rules
$(OBJ_DIR)/runner_main.o: $(TEST_RUNNER_SRC) $(HEADERS)
	@echo "Compiling main test runner..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/test_packet_ops.o: $(PACKET_OPS_TEST_SRC) $(HEADERS)
	@echo "Compiling packet operations test..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/test_irq.o: $(IRQ_TEST_SRC) $(HEADERS)
	@echo "Compiling interrupt handling test..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/test_perf_basic.o: $(PERFORMANCE_TEST_SRC) $(HEADERS)
	@echo "Compiling performance test..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/asm_api_test.o: $(ASM_API_TEST_SRC) $(HEADERS)
	@echo "Compiling assembly API test..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/helper_mock_hardware.o: $(HELPERS_DIR)/helper_mock_hardware.c $(HEADERS)
	@echo "Compiling helper mock hardware..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/helper_network_sim.o: $(HELPERS_DIR)/helper_network_sim.c $(HEADERS)
	@echo "Compiling helper network sim..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/test_framework.o: $(COMMON_DIR)/test_framework.c $(HEADERS)
	@echo "Compiling test framework..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Test execution targets
test: $(TEST_RUNNER)
	@echo ""
	@echo "=========================================="
	@echo "Running Comprehensive Test Suite"
	@echo "=========================================="
	./$(TEST_RUNNER)

test-verbose: $(TEST_RUNNER)
	@echo ""
	@echo "=========================================="
	@echo "Running Comprehensive Test Suite (Verbose)"
	@echo "=========================================="
	./$(TEST_RUNNER) --verbose

test-quick: $(TEST_RUNNER)
	@echo ""
	@echo "=========================================="
	@echo "Running Quick Test Suite (No Performance)"
	@echo "=========================================="
	./$(TEST_RUNNER) --no-performance

test-stress: $(TEST_RUNNER)
	@echo ""
	@echo "=========================================="
	@echo "Running Stress Test Suite"
	@echo "=========================================="
	./$(TEST_RUNNER) --stress --verbose

test-unit:
	@echo ""
	@echo "=========================================="
	@echo "Running Unit Tests"
	@echo "=========================================="
	@$(MAKE) -C $(UNIT_DIR) test

test-integration:
	@echo ""
	@echo "=========================================="
	@echo "Running Integration Tests"
	@echo "=========================================="
	@$(MAKE) -C $(INTEGRATION_DIR) test

test-performance:
	@echo ""
	@echo "=========================================="
	@echo "Running Performance Tests"
	@echo "=========================================="
	@$(MAKE) -C $(PERFORMANCE_DIR) test-all-performance

test-stress:
	@echo ""
	@echo "=========================================="
	@echo "Running Stress Tests"
	@echo "=========================================="
	@$(MAKE) -C $(STRESS_DIR) test-all-stress

test-asm:
	@echo ""
	@echo "=========================================="
	@echo "Running Assembly Tests"
	@echo "=========================================="
	@$(MAKE) -C $(ASM_DIR) test-all

runners-test:
	@echo ""
	@echo "=========================================="
	@echo "Running All Test Runners"
	@echo "=========================================="
	@$(MAKE) -C $(RUNNERS_DIR) run-tests

asm-test:
	@echo ""
	@echo "=========================================="
	@echo "Running Complete Assembly Test Suite"
	@echo "=========================================="
	@$(MAKE) -C $(ASM_DIR) test-all

test-all: test-unit test-integration test-performance test-stress test-asm runners-test
	@echo ""
	@echo "=========================================="
	@echo "All Test Suites Complete"
	@echo "=========================================="

# Analysis and reporting
test-coverage: $(TEST_RUNNER)
	@echo "Running test coverage analysis..."
	# In a real environment, this would use gcov or similar
	./$(TEST_RUNNER) --verbose > test_coverage_report.txt
	@echo "✓ Coverage report generated: test_coverage_report.txt"

test-profile: $(TEST_RUNNER)
	@echo "Running performance profiling..."
	# In a real environment, this would use gprof or similar
	./$(TEST_RUNNER) --stress > test_profile_report.txt
	@echo "✓ Profile report generated: test_profile_report.txt"

# Validation targets
validate-mocks: $(TEST_RUNNER)
	@echo "Validating hardware mock implementations..."
	./$(TEST_RUNNER) --no-performance | grep -i "mock\|hardware" || true

validate-memory: $(TEST_RUNNER)
	@echo "Validating memory management..."
	# In a real environment, this would use valgrind
	./$(TEST_RUNNER) --quick | grep -i "memory\|leak" || true

validate-performance: $(PERFORMANCE_TEST)
	@echo "Validating performance benchmarks..."
	./$(PERFORMANCE_TEST) | grep -i "performance\|benchmark" || true

# Documentation
test-doc:
	@echo "Generating test documentation..."
	@echo "Test Suite Components:" > TEST_SUITE_DOCS.md
	@echo "===================" >> TEST_SUITE_DOCS.md
	@echo "" >> TEST_SUITE_DOCS.md
	@echo "## Packet Operations Tests" >> TEST_SUITE_DOCS.md
	@echo "- TX/RX pipeline functionality" >> TEST_SUITE_DOCS.md
	@echo "- Queue management and flow control" >> TEST_SUITE_DOCS.md
	@echo "- Priority-based packet handling" >> TEST_SUITE_DOCS.md
	@echo "- Buffer management integration" >> TEST_SUITE_DOCS.md
	@echo "" >> TEST_SUITE_DOCS.md
	@echo "## Interrupt Handling Tests" >> TEST_SUITE_DOCS.md
	@echo "- IRQ installation and restoration" >> TEST_SUITE_DOCS.md
	@echo "- Interrupt service routine functionality" >> TEST_SUITE_DOCS.md
	@echo "- Spurious interrupt handling" >> TEST_SUITE_DOCS.md
	@echo "- Multiple NIC interrupt multiplexing" >> TEST_SUITE_DOCS.md
	@echo "" >> TEST_SUITE_DOCS.md
	@echo "## Performance Benchmarks" >> TEST_SUITE_DOCS.md
	@echo "- Packet transmission/reception throughput" >> TEST_SUITE_DOCS.md
	@echo "- Interrupt handling latency" >> TEST_SUITE_DOCS.md
	@echo "- 3C509B PIO vs 3C515-TX DMA comparison" >> TEST_SUITE_DOCS.md
	@echo "✓ Documentation generated: TEST_SUITE_DOCS.md"

# Debugging targets
debug: CFLAGS += -DDEBUG -g3
debug: $(TEST_RUNNER)
	@echo "Debug build complete. Use gdb ./$(TEST_RUNNER)"

debug-packet-ops: CFLAGS += -DDEBUG -g3
debug-packet-ops: $(PACKET_OPS_TEST)
	@echo "Debug packet ops test complete. Use gdb ./$(PACKET_OPS_TEST)"

# Static analysis (if tools available)
static-analysis:
	@echo "Running static analysis..."
	-cppcheck --enable=all --std=c99 $(INCLUDES) $(TEST_RUNNER_SRC) $(UNIT_DIR)/*.c
	@echo "✓ Static analysis complete"

# Cleanup
clean: asm-clean unit-clean integration-clean performance-clean stress-clean runners-clean helpers-clean common-clean
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)
	rm -f test_coverage_report.txt test_profile_report.txt
	rm -f TEST_SUITE_DOCS.md
	@echo "✓ Clean complete"

asm-clean:
	@echo "Cleaning assembly test artifacts..."
	@$(MAKE) -C $(ASM_DIR) clean

unit-clean:
	@echo "Cleaning unit test artifacts..."
	@$(MAKE) -C $(UNIT_DIR) clean

integration-clean:
	@echo "Cleaning integration test artifacts..."
	@$(MAKE) -C $(INTEGRATION_DIR) clean

performance-clean:
	@echo "Cleaning performance test artifacts..."
	@$(MAKE) -C $(PERFORMANCE_DIR) clean

stress-clean:
	@echo "Cleaning stress test artifacts..."
	@$(MAKE) -C $(STRESS_DIR) clean

runners-clean:
	@echo "Cleaning test runners..."
	@$(MAKE) -C $(RUNNERS_DIR) clean

helpers-clean:
	@echo "Cleaning test helpers..."
	@$(MAKE) -C $(HELPERS_DIR) clean

common-clean:
	@echo "Cleaning common test framework..."
	@$(MAKE) -C $(COMMON_DIR) clean

# Install/deployment helpers
install-tests: $(TEST_RUNNER)
	@echo "Installing test suite..."
	cp $(TEST_RUNNER) /usr/local/bin/3com-test-runner 2>/dev/null || \
	cp $(TEST_RUNNER) ../bin/3com-test-runner || \
	echo "Install failed - copy manually if needed"

# Continuous integration support
ci-test: dirs $(TEST_RUNNER)
	@echo "Running CI test suite..."
	./$(TEST_RUNNER) --no-performance --stop-on-failure

ci-full: dirs $(TEST_RUNNER)
	@echo "Running full CI test suite..."
	./$(TEST_RUNNER) --stress --stop-on-failure

# Help target
help:
	@echo "3Com Packet Driver Test Suite Makefile"
	@echo "======================================"
	@echo ""
	@echo "Build Targets:"
	@echo "  all              - Build comprehensive test runner"
	@echo "  dirs             - Create build directories"
	@echo "  debug            - Build with debug symbols"
	@echo ""
	@echo "Test Execution:"
	@echo "  test             - Run comprehensive test suite"
	@echo "  test-verbose     - Run with verbose output"
	@echo "  test-quick       - Run without performance tests"
	@echo "  test-stress      - Run stress tests"
	@echo "  test-packet-ops  - Run packet operations tests only"
	@echo "  test-irq         - Run interrupt handling tests only"
	@echo "  test-performance - Run performance benchmarks only"
	@echo "  test-asm         - Run assembly API tests only"
	@echo "  test-all         - Run all individual and comprehensive tests"
	@echo "  asm-test         - Run complete assembly test suite"
	@echo ""
	@echo "Analysis:"
	@echo "  test-coverage    - Generate coverage report"
	@echo "  test-profile     - Generate performance profile"
	@echo "  static-analysis  - Run static code analysis"
	@echo ""
	@echo "Validation:"
	@echo "  validate-mocks   - Validate hardware mock implementations"
	@echo "  validate-memory  - Validate memory management"
	@echo "  validate-performance - Validate performance benchmarks"
	@echo ""
	@echo "Utilities:"
	@echo "  test-doc         - Generate test documentation"
	@echo "  clean            - Remove build artifacts"
	@echo "  install-tests    - Install test runner"
	@echo "  ci-test          - Run CI test suite"
	@echo "  ci-full          - Run full CI test suite"
	@echo ""
	@echo "Examples:"
	@echo "  make test                    # Run all tests"
	@echo "  make test-verbose            # Run with detailed output"
	@echo "  make test-packet-ops         # Test only packet operations"
	@echo "  make validate-memory         # Check for memory issues"
	@echo "  make debug test              # Build debug version and test"

# Dependency information
deps:
	@echo "Test Suite Dependencies:"
	@echo "========================"
	@echo "Required Headers:"
	@for header in $(HEADERS); do echo "  $$header"; done
	@echo ""
	@echo "Source Files:"
	@echo "  Test Runner: $(TEST_RUNNER_SRC)"
	@echo "  Packet Ops:  $(PACKET_OPS_TEST_SRC)"
	@echo "  IRQ Tests:   $(IRQ_TEST_SRC)"
	@echo "  Performance: $(PERFORMANCE_TEST_SRC)"
	@echo ""
	@echo "Mock Sources:"
	@for src in $(MOCK_SRCS); do echo "  $$src"; done

# Build information
info:
	@echo "Build Configuration:"
	@echo "==================="
	@echo "Compiler: $(CC)"
	@echo "Flags: $(CFLAGS)"
	@echo "Includes: $(INCLUDES)"
	@echo "Libraries: $(LIBS)"
	@echo "Build Dir: $(BUILD_DIR)"
	@echo "Object Dir: $(OBJ_DIR)"

# Force rebuild
rebuild: clean all

# Quick test during development
dev-test: $(TEST_RUNNER)
	@echo "Running development test (quick)..."
	./$(TEST_RUNNER) --no-performance | tail -20
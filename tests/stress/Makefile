# Makefile for 3COM Packet Driver Stress Tests
# 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs

include ../common/common.mk

# Compiler and tools
CC = $(TEST_CC)
ASM = $(TEST_ASM)
LD = ld
AR = ar

# Directories
SRCDIR = ../../src/c
INCDIR = ../../include
ASMDIR = ../../src/asm
BUILDDIR = ../../build
OBJDIR = $(TEST_OBJ_DIR)/stress
BINDIR = $(BUILDDIR)/bin
COMMON_DIR = ../common
HELPERS_DIR = ../helpers
PERFORMANCE_DIR = ../performance

# Compiler flags - use common test settings with stress additions
CFLAGS = $(TEST_CFLAGS) -I$(INCDIR) -I. -I$(COMMON_DIR) -I$(HELPERS_DIR) -I$(PERFORMANCE_DIR) -DSTRESS_TESTING

# Assembler flags
ASMFLAGS = -f obj16

# Linker flags for DOS
LDFLAGS = -T dos.ld -m i8086

# Stress test source files
STRESS_SOURCES = test_stress_stability.c test_stress_resource.c
STRESS_OBJECTS = $(STRESS_SOURCES:%.c=$(OBJDIR)/%.o)

# Performance framework dependency
PERF_FRAMEWORK_OBJS = $(PERFORMANCE_DIR)/perf_framework.o $(PERFORMANCE_DIR)/perf_regression.o

# Required driver source files
DRIVER_SOURCES = $(SRCDIR)/timestamp.c $(SRCDIR)/stats.c $(SRCDIR)/diagnostics.c \
                 $(SRCDIR)/hardware.c $(SRCDIR)/memory.c $(SRCDIR)/buffer_alloc.c \
                 $(SRCDIR)/packet_ops.c $(SRCDIR)/logging.c $(SRCDIR)/3c509b.c $(SRCDIR)/3c515.c
DRIVER_OBJECTS = $(DRIVER_SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Test framework sources
FRAMEWORK_OBJECTS = $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)

# All object files
ALL_OBJECTS = $(STRESS_OBJECTS) $(FRAMEWORK_OBJECTS) $(PERF_FRAMEWORK_OBJS)

# Target executables
TARGETS = test_stress_stability test_stress_resource stress_suite stress_runner

# Default target
all: test-dirs $(FRAMEWORK_OBJECTS) $(PERF_FRAMEWORK_OBJS) $(TARGETS)
	@echo "All stress tests built successfully"

# Build common dependencies
$(COMMON_TEST_OBJS):
	@echo "Building common test framework..."
	@$(MAKE) -C $(COMMON_DIR)

$(HELPER_TEST_OBJS):
	@echo "Building test helpers..."
	@$(MAKE) -C $(HELPERS_DIR)

$(PERF_FRAMEWORK_OBJS):
	@echo "Building performance framework..."
	@$(MAKE) -C $(PERFORMANCE_DIR)

# Individual stress test executables
test_stress_stability: $(OBJDIR)/test_stress_stability.o $(FRAMEWORK_OBJECTS) $(PERF_FRAMEWORK_OBJS)
	@echo "Linking stability test executable..."
	$(CC) $(CFLAGS) -o $@ $^ -lm

test_stress_resource: $(OBJDIR)/test_stress_resource.o $(FRAMEWORK_OBJECTS) $(PERF_FRAMEWORK_OBJS)
	@echo "Linking resource stress test executable..."
	$(CC) $(CFLAGS) -o $@ $^ -lm

stress_suite: $(ALL_OBJECTS)
	@echo "Linking complete stress test suite..."
	$(CC) $(CFLAGS) -o $@ $^ -lm

stress_runner: $(ALL_OBJECTS)
	@echo "Linking stress test runner..."
	$(CC) $(CFLAGS) -o $@ $^ -lm

# Compile stress test sources
$(OBJDIR)/%.o: %.c $(COMMON_HEADERS)
	@echo "Compiling stress test: $<"
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Test execution targets
.PHONY: test test-all-stress run-tests

test: all
	@echo "Running stress tests..."
	@for test in $(TARGETS); do \
		echo "Running $$test..."; \
		./$$test || echo "$$test FAILED"; \
	done

test-all-stress: test

run-tests: test

# Stress test execution targets
.PHONY: test-stability test-resource test-all-stress run-long-stress

# Run individual stress tests
test-stability: $(BINDIR)/test_stress_stability.exe
	@echo "Running stability stress tests..."
	@echo "================================="
	@echo "NOTE: This test may run for several minutes"
	$(BINDIR)/test_stress_stability.exe
	@echo "Stability tests completed."

test-resource: $(BINDIR)/test_stress_resource.exe
	@echo "Running resource exhaustion tests..."
	@echo "===================================="
	$(BINDIR)/test_stress_resource.exe
	@echo "Resource stress tests completed."

# Run all stress tests
test-all-stress: $(TARGETS)
	@echo "Running complete stress test suite..."
	@echo "====================================="
	@echo ""
	@echo "WARNING: Stress tests may take significant time and system resources"
	@echo "Press Ctrl+C to interrupt if necessary"
	@echo ""
	@echo "1. Stability Stress Tests"
	@echo "========================="
	-$(BINDIR)/test_stress_stability.exe --quick-mode
	@echo ""
	@echo "2. Resource Exhaustion Tests"
	@echo "============================"
	-$(BINDIR)/test_stress_resource.exe --quick-mode
	@echo ""
	@echo "3. Comprehensive Stress Suite"
	@echo "============================="
	-$(BINDIR)/stress_suite.exe --comprehensive
	@echo ""
	@echo "Stress test suite completed."

# Long-duration stress testing
run-long-stress: $(BINDIR)/stress_suite.exe
	@echo "Running long-duration stress tests..."
	@echo "====================================="
	@echo "WARNING: This will run for extended periods (hours)"
	@echo "Ensure system is available for long-term testing"
	@echo ""
	$(BINDIR)/stress_suite.exe --long-duration --duration=7200 --save-logs
	@echo "Long-duration stress testing completed."

# Stress test modes and configurations
.PHONY: test-memory-stress test-interrupt-stress test-thermal-stress test-multi-nic-stress

test-memory-stress: $(BINDIR)/test_stress_resource.exe
	@echo "Running memory pressure stress tests..."
	@echo "======================================"
	$(BINDIR)/test_stress_resource.exe --memory-stress --aggressive-mode
	@echo "Memory stress tests completed."

test-interrupt-stress: $(BINDIR)/test_stress_resource.exe
	@echo "Running interrupt storm stress tests..."
	@echo "======================================"
	$(BINDIR)/test_stress_resource.exe --interrupt-stress --storm-intensity=high
	@echo "Interrupt stress tests completed."

test-thermal-stress: $(BINDIR)/test_stress_stability.exe
	@echo "Running thermal stress simulation..."
	@echo "==================================="
	$(BINDIR)/test_stress_stability.exe --thermal-stress --temperature-profile=aggressive
	@echo "Thermal stress tests completed."

test-multi-nic-stress: $(BINDIR)/test_stress_resource.exe
	@echo "Running multi-NIC stress tests..."
	@echo "================================="
	$(BINDIR)/test_stress_resource.exe --multi-nic-stress --nic-count=4
	@echo "Multi-NIC stress tests completed."

# Stress testing with different intensities
.PHONY: test-light-stress test-moderate-stress test-heavy-stress test-extreme-stress

test-light-stress: $(TARGETS)
	@echo "Running light stress tests..."
	@echo "============================"
	$(BINDIR)/stress_suite.exe --intensity=light --duration=300
	@echo "Light stress tests completed."

test-moderate-stress: $(TARGETS)
	@echo "Running moderate stress tests..."
	@echo "==============================="
	$(BINDIR)/stress_suite.exe --intensity=moderate --duration=600
	@echo "Moderate stress tests completed."

test-heavy-stress: $(TARGETS)
	@echo "Running heavy stress tests..."
	@echo "============================"
	$(BINDIR)/stress_suite.exe --intensity=heavy --duration=1200
	@echo "Heavy stress tests completed."

test-extreme-stress: $(TARGETS)
	@echo "Running extreme stress tests..."
	@echo "==============================="
	@echo "WARNING: Extreme stress testing - monitor system closely"
	$(BINDIR)/stress_suite.exe --intensity=extreme --duration=1800 --monitor-system
	@echo "Extreme stress tests completed."

# Stress testing with error injection
.PHONY: test-fault-injection test-error-recovery test-degraded-performance

test-fault-injection: $(TARGETS)
	@echo "Running fault injection stress tests..."
	@echo "======================================"
	$(BINDIR)/stress_suite.exe --fault-injection --error-rate=5 --duration=600
	@echo "Fault injection tests completed."

test-error-recovery: $(TARGETS)
	@echo "Running error recovery stress tests..."
	@echo "====================================="
	$(BINDIR)/stress_suite.exe --error-recovery --recovery-scenarios=all
	@echo "Error recovery tests completed."

test-degraded-performance: $(TARGETS)
	@echo "Running degraded performance tests..."
	@echo "===================================="
	$(BINDIR)/stress_suite.exe --degraded-mode --performance-limit=50
	@echo "Degraded performance tests completed."

# Automated stress testing
.PHONY: test-automated-stress test-scheduled-stress test-regression-stress

test-automated-stress: $(TARGETS)
	@echo "Running automated stress test sequence..."
	@echo "========================================"
	$(BINDIR)/stress_runner.exe --automated --sequence=standard --unattended
	@echo "Automated stress testing completed."

test-scheduled-stress: $(TARGETS)
	@echo "Running scheduled stress tests..."
	@echo "================================"
	$(BINDIR)/stress_runner.exe --scheduled --daily-tests --log-results
	@echo "Scheduled stress tests completed."

test-regression-stress: $(TARGETS)
	@echo "Running stress regression tests..."
	@echo "================================="
	$(BINDIR)/stress_runner.exe --regression-mode --compare-baselines
	@echo "Stress regression tests completed."

# Monitoring and analysis
.PHONY: monitor-stress analyze-stress-results generate-stress-report

monitor-stress: $(TARGETS)
	@echo "Starting stress test monitoring..."
	@echo "================================="
	$(BINDIR)/stress_runner.exe --monitor-mode --real-time-analysis
	@echo "Stress monitoring completed."

analyze-stress-results: $(TARGETS)
	@echo "Analyzing stress test results..."
	@echo "==============================="
	@mkdir -p $(BUILDDIR)/analysis
	$(BINDIR)/stress_suite.exe --analyze-results --output-dir=$(BUILDDIR)/analysis
	@echo "Stress analysis completed."

generate-stress-report: $(TARGETS)
	@echo "Generating stress test report..."
	@echo "==============================="
	@mkdir -p $(BUILDDIR)/reports
	$(BINDIR)/stress_suite.exe --generate-report --detailed > $(BUILDDIR)/reports/stress_report.txt
	@echo "Report generated: $(BUILDDIR)/reports/stress_report.txt"

# Development and debugging
.PHONY: debug-stress test-stress-debug profile-stress

debug-stress: CFLAGS += -g -DDEBUG -O0
debug-stress: clean all
	@echo "Debug stress build completed."

test-stress-debug: debug-stress
	@echo "Running stress tests in debug mode..."
	@echo "====================================="
	gdb --batch --ex run --ex bt $(BINDIR)/test_stress_stability.exe

profile-stress: $(TARGETS)
	@echo "Running stress test profiling..."
	@echo "==============================="
	time $(BINDIR)/test_stress_stability.exe --profile-mode --duration=300
	time $(BINDIR)/test_stress_resource.exe --profile-mode

# Validation and verification
.PHONY: validate-stress-tests verify-stress-accuracy test-stress-repeatability

validate-stress-tests: $(TARGETS)
	@echo "Validating stress test behavior..."
	@echo "================================="
	$(BINDIR)/stress_suite.exe --validate-behavior --check-invariants
	@echo "Stress test validation completed."

verify-stress-accuracy: $(TARGETS)
	@echo "Verifying stress test accuracy..."
	@echo "================================"
	$(BINDIR)/stress_suite.exe --verify-accuracy --controlled-conditions
	@echo "Stress accuracy verification completed."

test-stress-repeatability: $(TARGETS)
	@echo "Testing stress test repeatability..."
	@echo "==================================="
	$(BINDIR)/stress_runner.exe --repeatability-test --iterations=5
	@echo "Repeatability testing completed."

# Safety and system protection
.PHONY: test-safe-stress test-protected-stress test-monitored-stress

test-safe-stress: $(TARGETS)
	@echo "Running safe stress tests..."
	@echo "============================"
	$(BINDIR)/stress_suite.exe --safe-mode --resource-limits --timeout=1800
	@echo "Safe stress tests completed."

test-protected-stress: $(TARGETS)
	@echo "Running protected stress tests..."
	@echo "================================="
	$(BINDIR)/stress_suite.exe --protected-mode --system-monitoring --auto-abort
	@echo "Protected stress tests completed."

test-monitored-stress: $(TARGETS)
	@echo "Running monitored stress tests..."
	@echo "================================="
	$(BINDIR)/stress_runner.exe --monitored --health-checks --safety-limits
	@echo "Monitored stress tests completed."

# Continuous integration targets
.PHONY: ci-stress ci-quick-stress ci-nightly-stress

ci-stress: all
	@echo "Running CI stress tests..."
	@echo "=========================="
	$(BINDIR)/stress_suite.exe --ci-mode --quick-stress --timeout=300
	@echo "CI stress tests completed."

ci-quick-stress: all
	@echo "Running quick stress check..."
	@echo "============================"
	$(BINDIR)/stress_suite.exe --quick-check --sanity-only --timeout=60
	@echo "Quick stress check completed."

ci-nightly-stress: all
	@echo "Running nightly stress tests..."
	@echo "==============================="
	$(BINDIR)/stress_suite.exe --nightly --comprehensive --save-results --duration=3600
	@echo "Nightly stress tests completed."

# Stress test configuration and setup
.PHONY: configure-stress setup-stress-env calibrate-stress

configure-stress: $(TARGETS)
	@echo "Configuring stress test environment..."
	@echo "====================================="
	$(BINDIR)/stress_runner.exe --configure --optimize-settings
	@echo "Stress configuration completed."

setup-stress-env: $(TARGETS)
	@echo "Setting up stress test environment..."
	@echo "===================================="
	$(BINDIR)/stress_runner.exe --setup-environment --prepare-system
	@echo "Environment setup completed."

calibrate-stress: $(TARGETS)
	@echo "Calibrating stress test parameters..."
	@echo "===================================="
	$(BINDIR)/stress_suite.exe --calibrate --baseline-system --save-config
	@echo "Stress calibration completed."

# Help target
.PHONY: help

help:
	@echo "3COM Packet Driver Stress Test Makefile"
	@echo "======================================="
	@echo ""
	@echo "Build Targets:"
	@echo "  all                      - Build all stress test executables"
	@echo "  debug-stress            - Build with debug symbols"
	@echo ""
	@echo "Basic Stress Tests:"
	@echo "  test-stability          - Run stability stress tests"
	@echo "  test-resource           - Run resource exhaustion tests"
	@echo "  test-all-stress         - Run complete stress test suite"
	@echo "  run-long-stress         - Run long-duration stress tests (hours)"
	@echo ""
	@echo "Specific Stress Types:"
	@echo "  test-memory-stress      - Memory pressure stress tests"
	@echo "  test-interrupt-stress   - Interrupt storm stress tests"
	@echo "  test-thermal-stress     - Thermal stress simulation"
	@echo "  test-multi-nic-stress   - Multi-NIC stress tests"
	@echo ""
	@echo "Stress Intensity Levels:"
	@echo "  test-light-stress       - Light stress testing (5 min)"
	@echo "  test-moderate-stress    - Moderate stress testing (10 min)"
	@echo "  test-heavy-stress       - Heavy stress testing (20 min)"
	@echo "  test-extreme-stress     - Extreme stress testing (30 min)"
	@echo ""
	@echo "Error and Fault Testing:"
	@echo "  test-fault-injection    - Fault injection stress tests"
	@echo "  test-error-recovery     - Error recovery stress tests"
	@echo "  test-degraded-performance - Degraded performance tests"
	@echo ""
	@echo "Automated Testing:"
	@echo "  test-automated-stress   - Automated stress test sequence"
	@echo "  test-scheduled-stress   - Scheduled stress tests"
	@echo "  test-regression-stress  - Stress regression tests"
	@echo ""
	@echo "Safety and Protection:"
	@echo "  test-safe-stress        - Safe stress tests with limits"
	@echo "  test-protected-stress   - Protected stress tests with monitoring"
	@echo "  test-monitored-stress   - Monitored stress tests with health checks"
	@echo ""
	@echo "Analysis and Reporting:"
	@echo "  monitor-stress          - Real-time stress monitoring"
	@echo "  analyze-stress-results  - Analyze stress test results"
	@echo "  generate-stress-report  - Generate comprehensive stress report"
	@echo ""
	@echo "Validation:"
	@echo "  validate-stress-tests   - Validate stress test behavior"
	@echo "  verify-stress-accuracy  - Verify stress test accuracy"
	@echo "  test-stress-repeatability - Test repeatability of stress tests"
	@echo ""
	@echo "Configuration:"
	@echo "  configure-stress        - Configure stress test environment"
	@echo "  setup-stress-env        - Setup stress test environment"
	@echo "  calibrate-stress        - Calibrate stress test parameters"
	@echo ""
	@echo "Continuous Integration:"
	@echo "  ci-stress              - Run CI stress tests"
	@echo "  ci-quick-stress        - Quick stress sanity check"
	@echo "  ci-nightly-stress      - Comprehensive nightly stress tests"
	@echo ""
	@echo "Development:"
	@echo "  test-stress-debug      - Run stress tests with debugger"
	@echo "  profile-stress         - Profile stress test execution"
	@echo ""
	@echo "Cleanup:"
	@echo "  clean                  - Remove build artifacts"
	@echo "  clean-reports          - Remove generated reports"
	@echo ""
	@echo "WARNINGS:"
	@echo "  - Stress tests may consume significant system resources"
	@echo "  - Long-duration tests may run for hours"
	@echo "  - Extreme stress tests should be monitored closely"
	@echo "  - Use safe/protected modes for unattended testing"
	@echo ""

# Cleanup targets
.PHONY: clean clean-all

clean:
	@echo "Cleaning stress test build artifacts..."
	@rm -rf $(OBJDIR)
	@rm -f $(TARGETS) *.o
	@$(MAKE) -C $(COMMON_DIR) clean
	@$(MAKE) -C $(HELPERS_DIR) clean
	@$(MAKE) -C $(PERFORMANCE_DIR) clean
	@echo "Cleanup completed."

clean-all: clean
	@echo "Complete stress test cleanup performed."

# Dependency tracking (simplified)
$(OBJDIR)/test_stress_stability.o: test_stress_stability.c ../performance/perf_framework.h $(INCDIR)/test_framework.h
$(OBJDIR)/test_stress_resource.o: test_stress_resource.c ../performance/perf_framework.h $(INCDIR)/test_framework.h
$(OBJDIR)/perf_framework.o: ../performance/perf_framework.c ../performance/perf_framework.h
$(OBJDIR)/perf_regression.o: ../performance/perf_regression.c ../performance/perf_framework.h

# Include dependency files if they exist
-include $(OBJDIR)/*.d
# Makefile for 3COM Packet Driver Performance Tests
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
OBJDIR = $(TEST_OBJ_DIR)/performance
BINDIR = $(BUILDDIR)/bin
COMMON_DIR = ../common
HELPERS_DIR = ../helpers

# Compiler flags - use common test settings with performance additions
CFLAGS = $(TEST_CFLAGS) -I$(INCDIR) -I. -I$(COMMON_DIR) -I$(HELPERS_DIR) -DPERFORMANCE_TESTING

# Assembler flags
ASMFLAGS = -f obj16

# Linker flags for DOS
LDFLAGS = -T dos.ld -m i8086

# Performance test source files
PERF_SOURCES = test_perf_basic.c test_perf_latency.c test_perf_throughput.c perf_framework.c perf_regression.c
PERF_OBJECTS = $(PERF_SOURCES:%.c=$(OBJDIR)/%.o)

# Required driver source files
DRIVER_SOURCES = $(SRCDIR)/timestamp.c $(SRCDIR)/stats.c $(SRCDIR)/diagnostics.c \
                 $(SRCDIR)/hardware.c $(SRCDIR)/memory.c $(SRCDIR)/buffer_alloc.c \
                 $(SRCDIR)/packet_ops.c $(SRCDIR)/logging.c
DRIVER_OBJECTS = $(DRIVER_SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Test framework sources
FRAMEWORK_OBJECTS = $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)

# All object files
ALL_OBJECTS = $(PERF_OBJECTS) $(DRIVER_OBJECTS) $(FRAMEWORK_OBJECTS)

# Target executables
TARGETS = test_perf_basic test_perf_latency test_perf_throughput perf_suite perf_benchmark

# Default target
all: test-dirs $(FRAMEWORK_OBJECTS) $(TARGETS)
	@echo "All performance tests built successfully"

# Build common dependencies
$(COMMON_TEST_OBJS):
	@echo "Building common test framework..."
	@$(MAKE) -C $(COMMON_DIR)

$(HELPER_TEST_OBJS):
	@echo "Building test helpers..."
	@$(MAKE) -C $(HELPERS_DIR)

# Individual test executables
test_perf_basic: $(OBJDIR)/test_perf_basic.o $(OBJDIR)/perf_framework.o $(FRAMEWORK_OBJECTS)
	@echo "Linking basic performance test..."
	$(CC) $(CFLAGS) -o $@ $^ -lm

test_perf_latency: $(OBJDIR)/test_perf_latency.o $(OBJDIR)/perf_framework.o $(FRAMEWORK_OBJECTS)
	@echo "Linking latency test..."
	$(CC) $(CFLAGS) -o $@ $^ -lm

test_perf_throughput: $(OBJDIR)/test_perf_throughput.o $(OBJDIR)/perf_framework.o $(FRAMEWORK_OBJECTS)
	@echo "Linking throughput test..."
	$(CC) $(CFLAGS) -o $@ $^ -lm

perf_suite: $(PERF_OBJECTS) $(FRAMEWORK_OBJECTS)
	@echo "Linking complete performance suite..."
	$(CC) $(CFLAGS) -o $@ $^ -lm

perf_benchmark: $(OBJDIR)/perf_framework.o $(OBJDIR)/perf_regression.o $(FRAMEWORK_OBJECTS)
	@echo "Linking performance benchmark tool..."
	$(CC) $(CFLAGS) -o $@ $^ -lm

# Compile performance test sources
$(OBJDIR)/%.o: %.c $(COMMON_HEADERS)
	@echo "Compiling performance test: $<"
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Test execution targets
.PHONY: test test-all-performance run-tests

test: all
	@echo "Running performance tests..."
	@for test in $(TARGETS); do \
		echo "Running $$test..."; \
		./$$test || echo "$$test FAILED"; \
	done

test-all-performance: test

run-tests: test

# Individual performance test targets
.PHONY: test-basic test-latency test-throughput run-benchmarks

test-basic: test_perf_basic
	@echo "Running basic performance tests..."
	@echo "=================================="
	./test_perf_basic
	@echo "Basic performance tests completed."

test-latency: test_perf_latency
	@echo "Running latency performance tests..."
	@echo "===================================="
	./test_perf_latency
	@echo "Latency tests completed."

test-throughput: test_perf_throughput
	@echo "Running throughput performance tests..."
	@echo "======================================="
	./test_perf_throughput
	@echo "Throughput tests completed."

# Run all performance tests
test-all-performance: $(TARGETS)
	@echo "Running complete performance test suite..."
	@echo "=========================================="
	@echo ""
	@echo "1. Throughput Performance Tests"
	@echo "==============================="
	-$(BINDIR)/throughput_test.exe
	@echo ""
	@echo "2. Latency Performance Tests"
	@echo "============================"
	-$(BINDIR)/latency_test.exe
	@echo ""
	@echo "3. Comprehensive Performance Suite"
	@echo "==================================="
	-$(BINDIR)/perf_suite.exe
	@echo ""
	@echo "Performance test suite completed."

# Run performance benchmarks with detailed reporting
run-benchmarks: $(BINDIR)/perf_benchmark.exe
	@echo "Running performance benchmarks..."
	@echo "================================="
	$(BINDIR)/perf_benchmark.exe --detailed-report --save-baseline
	@echo "Benchmarks completed."

# Performance regression testing
.PHONY: test-regression baseline-create baseline-compare

test-regression: $(BINDIR)/perf_benchmark.exe
	@echo "Running performance regression tests..."
	@echo "======================================"
	$(BINDIR)/perf_benchmark.exe --regression-test --threshold=5
	@echo "Regression testing completed."

baseline-create: $(BINDIR)/perf_benchmark.exe
	@echo "Creating performance baseline..."
	@echo "==============================="
	$(BINDIR)/perf_benchmark.exe --create-baseline --name="current_build"
	@echo "Baseline creation completed."

baseline-compare: $(BINDIR)/perf_benchmark.exe
	@echo "Comparing against performance baseline..."
	@echo "========================================"
	$(BINDIR)/perf_benchmark.exe --compare-baseline --name="current_build"
	@echo "Baseline comparison completed."

# Stress testing integration
.PHONY: test-performance-stress run-stability-check

test-performance-stress: $(TARGETS)
	@echo "Running performance tests under stress conditions..."
	@echo "==================================================="
	# Run with reduced expectations under stress
	PERF_STRESS_MODE=1 $(BINDIR)/throughput_test.exe --stress-mode
	PERF_STRESS_MODE=1 $(BINDIR)/latency_test.exe --stress-mode
	@echo "Performance stress testing completed."

run-stability-check: $(TARGETS)
	@echo "Running performance stability check..."
	@echo "====================================="
	$(BINDIR)/perf_suite.exe --stability-check --duration=300
	@echo "Stability check completed."

# Documentation and reporting
.PHONY: generate-report clean-reports

generate-report: $(TARGETS)
	@echo "Generating performance test report..."
	@echo "===================================="
	@mkdir -p $(BUILDDIR)/reports
	$(BINDIR)/perf_suite.exe --generate-report > $(BUILDDIR)/reports/performance_report.txt
	@echo "Report generated: $(BUILDDIR)/reports/performance_report.txt"

clean-reports:
	@echo "Cleaning performance reports..."
	@rm -f $(BUILDDIR)/reports/performance_*.txt
	@rm -f $(BUILDDIR)/reports/baseline_*.dat

# Development and debugging targets
.PHONY: debug-build test-debug profile-performance

debug-build: CFLAGS += -g -DDEBUG -O0
debug-build: clean all
	@echo "Debug build completed."

test-debug: debug-build
	@echo "Running performance tests in debug mode..."
	@echo "=========================================="
	gdb --batch --ex run --ex bt $(BINDIR)/throughput_test.exe

profile-performance: $(TARGETS)
	@echo "Running performance profiling..."
	@echo "==============================="
	# Simple profiling - would use actual profiler in real implementation
	time $(BINDIR)/throughput_test.exe --profile-mode
	time $(BINDIR)/latency_test.exe --profile-mode

# Validation and verification
.PHONY: validate-tests verify-accuracy test-precision

validate-tests: $(TARGETS)
	@echo "Validating performance test accuracy..."
	@echo "======================================"
	$(BINDIR)/perf_suite.exe --validate-tests --iterations=100
	@echo "Test validation completed."

verify-accuracy: $(TARGETS)
	@echo "Verifying measurement accuracy..."
	@echo "================================"
	$(BINDIR)/perf_benchmark.exe --verify-accuracy --known-values
	@echo "Accuracy verification completed."

test-precision: $(TARGETS)
	@echo "Testing measurement precision..."
	@echo "==============================="
	$(BINDIR)/perf_benchmark.exe --test-precision --repeated-measurements=1000
	@echo "Precision testing completed."

# Continuous integration targets
.PHONY: ci-performance ci-quick-perf ci-nightly-perf

ci-performance: all
	@echo "Running CI performance tests..."
	@echo "==============================="
	# Quick performance check for CI
	$(BINDIR)/throughput_test.exe --quick-mode --ci-mode
	$(BINDIR)/latency_test.exe --quick-mode --ci-mode
	@echo "CI performance tests completed."

ci-quick-perf: all
	@echo "Running quick performance check..."
	@echo "================================="
	# Very fast performance sanity check
	$(BINDIR)/perf_suite.exe --quick-check --timeout=60
	@echo "Quick performance check completed."

ci-nightly-perf: all
	@echo "Running nightly performance tests..."
	@echo "===================================="
	# Comprehensive nightly performance testing
	$(BINDIR)/perf_suite.exe --comprehensive --save-results --duration=1800
	@echo "Nightly performance tests completed."

# Performance comparison and analysis
.PHONY: compare-nics analyze-performance generate-charts

compare-nics: $(TARGETS)
	@echo "Comparing NIC performance..."
	@echo "==========================="
	$(BINDIR)/perf_suite.exe --compare-nics --detailed-analysis
	@echo "NIC comparison completed."

analyze-performance: $(TARGETS)
	@echo "Analyzing performance characteristics..."
	@echo "======================================"
	$(BINDIR)/perf_benchmark.exe --analyze-performance --statistical-analysis
	@echo "Performance analysis completed."

generate-charts: $(TARGETS)
	@echo "Generating performance charts..."
	@echo "==============================="
	@mkdir -p $(BUILDDIR)/charts
	$(BINDIR)/perf_suite.exe --generate-charts --output-dir=$(BUILDDIR)/charts
	@echo "Charts generated in $(BUILDDIR)/charts/"

# Help target
.PHONY: help

help:
	@echo "3COM Packet Driver Performance Test Makefile"
	@echo "============================================="
	@echo ""
	@echo "Build Targets:"
	@echo "  all                    - Build all performance test executables"
	@echo "  debug-build           - Build with debug symbols and optimizations disabled"
	@echo ""
	@echo "Test Execution:"
	@echo "  test-throughput       - Run throughput performance tests"
	@echo "  test-latency          - Run latency performance tests"
	@echo "  test-all-performance  - Run complete performance test suite"
	@echo "  run-benchmarks        - Run detailed performance benchmarks"
	@echo ""
	@echo "Stress Testing:"
	@echo "  test-performance-stress - Run performance tests under stress"
	@echo "  run-stability-check    - Run long-duration stability check"
	@echo ""
	@echo "Regression Testing:"
	@echo "  test-regression       - Run performance regression tests"
	@echo "  baseline-create       - Create new performance baseline"
	@echo "  baseline-compare      - Compare against existing baseline"
	@echo ""
	@echo "Analysis and Reporting:"
	@echo "  generate-report       - Generate comprehensive performance report"
	@echo "  compare-nics          - Compare performance between NIC types"
	@echo "  analyze-performance   - Perform detailed performance analysis"
	@echo "  generate-charts       - Generate performance visualization charts"
	@echo ""
	@echo "Validation:"
	@echo "  validate-tests        - Validate test accuracy and reliability"
	@echo "  verify-accuracy       - Verify measurement accuracy"
	@echo "  test-precision        - Test measurement precision"
	@echo ""
	@echo "Continuous Integration:"
	@echo "  ci-performance        - Run CI performance tests"
	@echo "  ci-quick-perf         - Quick performance sanity check"
	@echo "  ci-nightly-perf       - Comprehensive nightly performance tests"
	@echo ""
	@echo "Development:"
	@echo "  test-debug            - Run tests with debugger"
	@echo "  profile-performance   - Profile performance test execution"
	@echo ""
	@echo "Cleanup:"
	@echo "  clean                 - Remove build artifacts"
	@echo "  clean-reports         - Remove generated reports"
	@echo ""

# Cleanup targets
.PHONY: clean clean-all

clean:
	@echo "Cleaning performance test build artifacts..."
	@rm -rf $(OBJDIR)
	@rm -f $(TARGETS) *.o
	@$(MAKE) -C $(COMMON_DIR) clean
	@$(MAKE) -C $(HELPERS_DIR) clean
	@echo "Cleanup completed."

clean-all: clean
	@echo "Complete cleanup performed."

# Dependency tracking (simplified)
$(OBJDIR)/throughput_test.o: throughput_test.c perf_framework.h $(INCDIR)/test_framework.h
$(OBJDIR)/latency_test.o: latency_test.c perf_framework.h $(INCDIR)/test_framework.h
$(OBJDIR)/perf_framework.o: perf_framework.c perf_framework.h $(SRCDIR)/timestamp.c
$(OBJDIR)/perf_regression.o: perf_regression.c perf_framework.h

# Include dependency files if they exist
-include $(OBJDIR)/*.d
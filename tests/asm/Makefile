# Makefile for Assembly Test Framework and CPU Detection Tests
# 3Com Packet Driver - Assembly testing infrastructure

include ../common/common.mk

# Tools
NASM = $(TEST_ASM)
CC = $(TEST_CC)
LD = ld

# Directories
ASM_SRC_DIR = .
C_SRC_DIR = ../unit
INCLUDE_DIR = ../../include
SRC_DIR = ../../src
BUILD_DIR = $(TEST_BUILD_DIR)/asm
OBJ_DIR = $(TEST_OBJ_DIR)/asm
COMMON_DIR = ../common
HELPERS_DIR = ../helpers

# Flags
NASMFLAGS = $(TEST_ASMFLAGS)
CFLAGS = $(TEST_CFLAGS) -m32
INCLUDES = -I$(INCLUDE_DIR) -I$(SRC_DIR)/c -I$(COMMON_DIR) -I$(HELPERS_DIR)
LDFLAGS = -m elf_i386

# Assembly source files
ASM_FRAMEWORK_SRC = test_framework.asm
CPU_DETECT_TEST_SRC = test_cpu_detect.asm

# C source files that integrate with assembly
ASM_API_TEST_SRC = $(C_SRC_DIR)/test_asm_api.c

# Required assembly modules from main project
CPU_DETECT_ASM = $(SRC_DIR)/asm/cpu_detect.asm
PACKET_API_ASM = $(SRC_DIR)/asm/packet_api.asm
MAIN_ASM = $(SRC_DIR)/asm/main.asm

# Object files
ASM_FRAMEWORK_OBJ = $(OBJ_DIR)/asm_test_framework.o
CPU_DETECT_TEST_OBJ = $(OBJ_DIR)/cpu_detect_test.o
ASM_API_TEST_OBJ = $(OBJ_DIR)/asm_api_test.o

# Main project assembly objects
CPU_DETECT_OBJ = $(OBJ_DIR)/cpu_detect.o
PACKET_API_OBJ = $(OBJ_DIR)/packet_api.o
MAIN_OBJ = $(OBJ_DIR)/main.o

# Support C objects (simplified versions for testing)
TEST_FRAMEWORK_OBJ = $(OBJ_DIR)/test_framework_support.o
LOGGING_OBJ = $(OBJ_DIR)/logging_support.o

# Executables
CPU_TEST_RUNNER = $(BUILD_DIR)/cpu_test_runner
ASM_FRAMEWORK_TEST = $(BUILD_DIR)/asm_framework_test
ASM_API_TEST = $(BUILD_DIR)/asm_api_test

# Header dependencies
HEADERS = $(COMMON_HEADERS) \
          $(INCLUDE_DIR)/cpu_detect.h \
          $(INCLUDE_DIR)/packet_api.h \
          $(INCLUDE_DIR)/common.h

.PHONY: all clean test test-cpu test-framework test-api test-all help

# Default target
all: test-dirs $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS) $(CPU_TEST_RUNNER) $(ASM_FRAMEWORK_TEST) $(ASM_API_TEST)
	@echo "All assembly tests built successfully"

# Build common dependencies
$(COMMON_TEST_OBJS):
	@echo "Building common test framework..."
	@$(MAKE) -C $(COMMON_DIR)

$(HELPER_TEST_OBJS):
	@echo "Building test helpers..."
	@$(MAKE) -C $(HELPERS_DIR)

# Main CPU test runner (pure assembly)
$(CPU_TEST_RUNNER): $(CPU_DETECT_TEST_OBJ) $(CPU_DETECT_OBJ) $(ASM_FRAMEWORK_OBJ)
	@echo "Linking CPU test runner..."
	$(LD) $(LDFLAGS) -o $@ $^
	@echo "✓ CPU test runner built successfully"

# Assembly framework test
$(ASM_FRAMEWORK_TEST): $(ASM_FRAMEWORK_OBJ) $(TEST_FRAMEWORK_OBJ)
	@echo "Linking assembly framework test..."
	$(CC) $(CFLAGS) -o $@ $^
	@echo "✓ Assembly framework test built successfully"

# Assembly API test (C calling assembly)
$(ASM_API_TEST): $(ASM_API_TEST_OBJ) $(CPU_DETECT_OBJ) $(ASM_FRAMEWORK_OBJ) $(LOGGING_OBJ)
	@echo "Linking assembly API test..."
	$(CC) $(CFLAGS) -o $@ $^
	@echo "✓ Assembly API test built successfully"

# Assembly object compilation rules
$(OBJ_DIR)/test_framework.o: $(ASM_FRAMEWORK_SRC)
	@echo "Assembling test framework..."
	@mkdir -p $(OBJ_DIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(OBJ_DIR)/test_cpu_detect.o: $(CPU_DETECT_TEST_SRC)
	@echo "Assembling CPU detection tests..."
	@mkdir -p $(OBJ_DIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(OBJ_DIR)/cpu_detect.o: $(CPU_DETECT_ASM)
	@echo "Assembling CPU detection module..."
	$(NASM) $(NASMFLAGS) $< -o $@

$(OBJ_DIR)/packet_api.o: $(PACKET_API_ASM)
	@echo "Assembling packet API module..."
	$(NASM) $(NASMFLAGS) $< -o $@

$(OBJ_DIR)/main.o: $(MAIN_ASM)
	@echo "Assembling main module..."
	$(NASM) $(NASMFLAGS) $< -o $@

# C object compilation rules
$(OBJ_DIR)/test_asm_api.o: $(ASM_API_TEST_SRC) $(HEADERS)
	@echo "Compiling assembly API test..."
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/test_framework_support.o: test_framework_support.c
	@echo "Compiling test framework support..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/logging_support.o: logging_support.c
	@echo "Compiling logging support..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Test execution targets
test: test-all

test-all: $(CPU_TEST_RUNNER) $(ASM_FRAMEWORK_TEST) $(ASM_API_TEST)
	@echo ""
	@echo "==========================================" 
	@echo "Running Complete Assembly Test Suite"
	@echo "==========================================" 
	@echo ""
	@echo "1. CPU Detection Tests:"
	@echo "----------------------"
	-./$(CPU_TEST_RUNNER)
	@echo ""
	@echo "2. Assembly Framework Tests:"
	@echo "---------------------------"
	-./$(ASM_FRAMEWORK_TEST)
	@echo ""
	@echo "3. Assembly API Integration Tests:"
	@echo "---------------------------------"
	-./$(ASM_API_TEST)
	@echo ""
	@echo "==========================================" 
	@echo "Assembly Test Suite Complete"
	@echo "=========================================="

# Test execution targets
test: test-all

test-cpu: $(CPU_TEST_RUNNER)
	@echo ""
	@echo "=========================================="
	@echo "Running CPU Detection Tests"
	@echo "=========================================="
	./$(CPU_TEST_RUNNER)

test-framework: $(ASM_FRAMEWORK_TEST)
	@echo ""
	@echo "=========================================="
	@echo "Running Assembly Framework Tests"
	@echo "=========================================="
	./$(ASM_FRAMEWORK_TEST)

test-api: $(ASM_API_TEST)
	@echo ""
	@echo "=========================================="
	@echo "Running Assembly API Tests"
	@echo "=========================================="
	./$(ASM_API_TEST)

test-all: $(CPU_TEST_RUNNER) $(ASM_FRAMEWORK_TEST) $(ASM_API_TEST)
	@echo ""
	@echo "=========================================="
	@echo "Running Complete Assembly Test Suite"
	@echo "=========================================="
	@echo ""
	@echo "1. CPU Detection Tests:"
	@echo "----------------------"
	-./$(CPU_TEST_RUNNER)
	@echo ""
	@echo "2. Assembly Framework Tests:"
	@echo "---------------------------"
	-./$(ASM_FRAMEWORK_TEST)
	@echo ""
	@echo "3. Assembly API Integration Tests:"
	@echo "---------------------------------"
	-./$(ASM_API_TEST)
	@echo ""
	@echo "=========================================="
	@echo "Assembly Test Suite Complete"
	@echo "=========================================="

# Validation targets
validate-asm: $(CPU_TEST_RUNNER)
	@echo "Validating assembly modules..."
	@echo "✓ Assembly modules compile successfully"

validate-integration: $(ASM_API_TEST)
	@echo "Validating C-Assembly integration..."
	./$(ASM_API_TEST) | grep -i "integration\|api" || true

# Debug builds
debug: NASMFLAGS += -g -F dwarf
debug: CFLAGS += -DDEBUG -g3
debug: dirs $(CPU_TEST_RUNNER) $(ASM_API_TEST)
	@echo "Debug builds complete"

# Generate assembly listings
listings: dirs
	@echo "Generating assembly listings..."
	$(NASM) $(NASMFLAGS) -l $(BUILD_DIR)/asm_test_framework.lst $(ASM_FRAMEWORK_SRC)
	$(NASM) $(NASMFLAGS) -l $(BUILD_DIR)/cpu_detect_test.lst $(CPU_DETECT_TEST_SRC)
	@echo "✓ Assembly listings generated in $(BUILD_DIR)/"

# Documentation
doc:
	@echo "Assembly Test Framework Documentation" > ASM_TEST_DOCS.md
	@echo "====================================" >> ASM_TEST_DOCS.md
	@echo "" >> ASM_TEST_DOCS.md
	@echo "## Overview" >> ASM_TEST_DOCS.md
	@echo "This framework provides comprehensive testing for assembly code modules." >> ASM_TEST_DOCS.md
	@echo "" >> ASM_TEST_DOCS.md
	@echo "## Test Components" >> ASM_TEST_DOCS.md
	@echo "- **CPU Detection Tests**: Validate CPU type and feature detection" >> ASM_TEST_DOCS.md
	@echo "- **Assembly Framework**: Core testing infrastructure in assembly" >> ASM_TEST_DOCS.md
	@echo "- **API Integration**: C-Assembly interface validation" >> ASM_TEST_DOCS.md
	@echo "" >> ASM_TEST_DOCS.md
	@echo "## Usage" >> ASM_TEST_DOCS.md
	@echo "\`\`\`bash" >> ASM_TEST_DOCS.md
	@echo "make test-all    # Run all assembly tests" >> ASM_TEST_DOCS.md
	@echo "make test-cpu    # Run CPU detection tests only" >> ASM_TEST_DOCS.md
	@echo "make test-api    # Run API integration tests only" >> ASM_TEST_DOCS.md
	@echo "\`\`\`" >> ASM_TEST_DOCS.md
	@echo "✓ Documentation generated: ASM_TEST_DOCS.md"

# Cleanup
clean:
	@echo "Cleaning assembly test artifacts..."
	@rm -rf $(BUILD_DIR) $(OBJ_DIR)
	@rm -f $(CPU_TEST_RUNNER) $(ASM_FRAMEWORK_TEST) $(ASM_API_TEST) *.o
	@$(MAKE) -C $(COMMON_DIR) clean
	@$(MAKE) -C $(HELPERS_DIR) clean
	@echo "Assembly test clean complete"

# Help
help:
	@echo "Assembly Test Framework Makefile"
	@echo "==============================="
	@echo ""
	@echo "Build Targets:"
	@echo "  all              - Build all assembly tests"
	@echo "  dirs             - Create build directories"
	@echo "  debug            - Build with debug symbols"
	@echo "  listings         - Generate assembly listings"
	@echo ""
	@echo "Test Execution:"
	@echo "  test             - Run all assembly tests"
	@echo "  test-cpu         - Run CPU detection tests"
	@echo "  test-framework   - Run assembly framework tests"
	@echo "  test-api         - Run assembly API tests"
	@echo "  test-all         - Run complete assembly test suite"
	@echo ""
	@echo "Validation:"
	@echo "  validate-asm     - Validate assembly modules"
	@echo "  validate-integration - Validate C-Assembly integration"
	@echo ""
	@echo "Utilities:"
	@echo "  doc              - Generate documentation"
	@echo "  clean            - Remove build artifacts"
	@echo ""
	@echo "Examples:"
	@echo "  make test-all                # Run all assembly tests"
	@echo "  make debug test-cpu          # Debug CPU detection tests"
	@echo "  make listings                # Generate assembly listings"

# Build info
info:
	@echo "Assembly Test Build Configuration:"
	@echo "================================="
	@echo "Assembler: $(NASM)"
	@echo "NASM Flags: $(NASMFLAGS)"
	@echo "C Compiler: $(CC)"
	@echo "C Flags: $(CFLAGS)"
	@echo "Linker Flags: $(LDFLAGS)"
	@echo "Build Dir: $(BUILD_DIR)"
# Common test definitions and variables
# This file contains shared definitions used across all test directories

# Common compiler settings for tests
TEST_CC = gcc
TEST_CFLAGS = -Wall -Wextra -std=c99 -g -O0 -DTESTING -I../../include -I../common -I../helpers
TEST_ASM = nasm
TEST_ASMFLAGS = -f elf32 -g -F dwarf -I../../include/

# Common directories
SRC_DIR = ../../src
SRC_C_DIR = ../../src/c
SRC_ASM_DIR = ../../src/asm
INCLUDE_DIR = ../../include
BUILD_DIR = ../../build
TEST_BUILD_DIR = ../build
TEST_OBJ_DIR = ../build/obj
TEST_COMMON_DIR = ../common
TEST_HELPERS_DIR = ../helpers
TEST_RUNNERS_DIR = ../runners

# Common object files that tests might need
COMMON_TEST_OBJS = $(TEST_COMMON_DIR)/test_framework.o
HELPER_TEST_OBJS = $(TEST_HELPERS_DIR)/helper_mock_hardware.o $(TEST_HELPERS_DIR)/helper_network_sim.o

# Common header files
COMMON_HEADERS = $(TEST_COMMON_DIR)/test_common.h \
                 $(TEST_COMMON_DIR)/test_framework.h \
                 $(TEST_COMMON_DIR)/test_hardware.h \
                 $(TEST_COMMON_DIR)/test_macros.h

# Default test rules
%.o: %.c $(COMMON_HEADERS)
	@echo "Compiling test source: $<"
	$(TEST_CC) $(TEST_CFLAGS) -c $< -o $@

%.o: %.asm
	@echo "Assembling test source: $<"
	$(TEST_ASM) $(TEST_ASMFLAGS) $< -o $@

# Ensure test directories exist
test-dirs:
	@mkdir -p $(TEST_BUILD_DIR)
	@mkdir -p $(TEST_OBJ_DIR)

# Clean rule for common objects
clean-common:
	@echo "Cleaning common test objects..."
	@rm -f $(TEST_COMMON_DIR)/*.o
	@rm -f $(TEST_HELPERS_DIR)/*.o
	@rm -f $(TEST_OBJ_DIR)/*.o

# Run target for subdirectory tests
test:
	@echo "Running tests in $(shell pwd)..."

.PHONY: clean-common test test-dirs
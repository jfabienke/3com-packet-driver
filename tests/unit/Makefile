# Unit Tests Makefile
# Tests individual modules in isolation

include ../common/common.mk

# Project directories  
ROOTDIR=../..
SRCDIR=$(ROOTDIR)/src/c
INCDIR=$(ROOTDIR)/include
TESTDIR=..
COMMON_DIR=../common
HELPERS_DIR=../helpers

# Tools - Use common test compiler settings
CC=$(TEST_CC)
CFLAGS=$(TEST_CFLAGS) -I$(INCDIR) -I$(TESTDIR) -I$(COMMON_DIR) -I$(HELPERS_DIR)

# Test targets - updated for new file names
TESTS=test_api test_memory test_3c509b test_3c515 test_hardware test_irq test_packet_ops test_routing test_asm_api test_xms
TEST_EXES=$(TESTS:%=%.exe)

.PHONY: all clean test test-all run-tests

all: test-dirs $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS) $(TESTS)
	@echo "All unit tests built successfully"

test: all
	@echo "Running unit tests..."
	@for test in $(TESTS); do \
		echo "Running $$test..."; \
		./$$test || echo "$$test FAILED"; \
	done

test-all: test

run-tests: test

# API Test
test_api: test_api.o $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)
	@echo "Linking API test..."
	$(CC) $(CFLAGS) $^ -o $@

test_api.o: test_api.c $(COMMON_HEADERS)
	@echo "Compiling API test..."
	$(CC) $(CFLAGS) -c $< -o $@

# Memory Test  
test_memory: test_memory.o $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)
	@echo "Linking memory test..."
	$(CC) $(CFLAGS) $^ -o $@

test_memory.o: test_memory.c $(COMMON_HEADERS)
	@echo "Compiling memory test..."
	$(CC) $(CFLAGS) -c $< -o $@

# Hardware Test Suite
test_hardware: test_hardware.o $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)
	@echo "Linking hardware test..."
	$(CC) $(CFLAGS) $^ -o $@

test_hardware.o: test_hardware.c $(COMMON_HEADERS)
	@echo "Compiling hardware test..."
	$(CC) $(CFLAGS) -c $< -o $@

# 3C509B Test Suite
test_3c509b: test_3c509b.o $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)
	@echo "Linking 3C509B test..."
	$(CC) $(CFLAGS) $^ -o $@

test_3c509b.o: test_3c509b.c $(COMMON_HEADERS)
	@echo "Compiling 3C509B test..."
	$(CC) $(CFLAGS) -c $< -o $@

# 3C515-TX Test Suite
test_3c515: test_3c515.o $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)
	@echo "Linking 3C515 test..."
	$(CC) $(CFLAGS) $^ -o $@

test_3c515.o: test_3c515.c $(COMMON_HEADERS)
	@echo "Compiling 3C515 test..."
	$(CC) $(CFLAGS) -c $< -o $@

# IRQ Test Suite
test_irq: test_irq.o $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)
	@echo "Linking IRQ test..."
	$(CC) $(CFLAGS) $^ -o $@

test_irq.o: test_irq.c $(COMMON_HEADERS)
	@echo "Compiling IRQ test..."
	$(CC) $(CFLAGS) -c $< -o $@

# Packet Operations Test Suite
test_packet_ops: test_packet_ops.o $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)
	@echo "Linking packet operations test..."
	$(CC) $(CFLAGS) $^ -o $@

test_packet_ops.o: test_packet_ops.c $(COMMON_HEADERS)
	@echo "Compiling packet operations test..."
	$(CC) $(CFLAGS) -c $< -o $@

# Routing Test Suite
test_routing: test_routing.o $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)
	@echo "Linking routing test..."
	$(CC) $(CFLAGS) $^ -o $@

test_routing.o: test_routing.c $(COMMON_HEADERS)
	@echo "Compiling routing test..."
	$(CC) $(CFLAGS) -c $< -o $@

# Assembly API Test Suite
test_asm_api: test_asm_api.o $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)
	@echo "Linking assembly API test..."
	$(CC) $(CFLAGS) $^ -o $@

test_asm_api.o: test_asm_api.c $(COMMON_HEADERS)
	@echo "Compiling assembly API test..."
	$(CC) $(CFLAGS) -c $< -o $@

# XMS Test Suite
test_xms: test_xms.o $(COMMON_TEST_OBJS) $(HELPER_TEST_OBJS)
	@echo "Linking XMS test..."
	$(CC) $(CFLAGS) $^ -o $@

test_xms.o: test_xms.c $(COMMON_HEADERS)
	@echo "Compiling XMS test..."
	$(CC) $(CFLAGS) -c $< -o $@

# Build common dependencies
$(COMMON_TEST_OBJS):
	@echo "Building common test framework..."
	@$(MAKE) -C $(COMMON_DIR)

$(HELPER_TEST_OBJS):
	@echo "Building test helpers..."
	@$(MAKE) -C $(HELPERS_DIR)

clean:
	@echo "Cleaning unit test artifacts..."
	@rm -f *.o *.exe $(TESTS)
	@$(MAKE) -C $(COMMON_DIR) clean
	@$(MAKE) -C $(HELPERS_DIR) clean
# Helper compilation rules
# This file contains rules for compiling test helper functions and utilities

include common/common.mk

# Helper-specific settings
HELPER_CFLAGS = $(TEST_CFLAGS) -DTEST_HELPERS
HELPER_SOURCES = helper_mock_hardware.c helper_network_sim.c
HELPER_OBJECTS = $(HELPER_SOURCES:.c=.o)
HELPER_ASM_SOURCES = $(wildcard *.asm)
HELPER_ASM_OBJECTS = $(HELPER_ASM_SOURCES:.asm=.o)

# All helper objects
ALL_HELPER_OBJS = $(HELPER_OBJECTS) $(HELPER_ASM_OBJECTS)

# Build all helpers
all: helpers

helpers: test-dirs $(ALL_HELPER_OBJS)
	@echo "All test helpers built successfully"

# Helper-specific object compilation
%.o: %.c $(COMMON_HEADERS)
	@echo "Compiling helper: $<"
	$(TEST_CC) $(HELPER_CFLAGS) -c $< -o $@

# Specific helper targets
helper_mock_hardware.o: helper_mock_hardware.c $(COMMON_HEADERS)
	@echo "Compiling mock hardware helper..."
	$(TEST_CC) $(HELPER_CFLAGS) -c $< -o $@

helper_network_sim.o: helper_network_sim.c $(COMMON_HEADERS)
	@echo "Compiling network simulation helper..."
	$(TEST_CC) $(HELPER_CFLAGS) -c $< -o $@

# Clean helpers
clean:
	@echo "Cleaning helper objects..."
	@rm -f *.o
	@rm -f *.a

# Install helpers (copy to common build directory)
install: helpers
	@echo "Installing helper objects to build directory..."
	@mkdir -p $(TEST_OBJ_DIR)
	@cp *.o $(TEST_OBJ_DIR)/ 2>/dev/null || true

.PHONY: all helpers clean install
# CRUSH.md - 3Com Packet Driver Development Guide

## Build Commands
- `wmake` or `wmake release` - Build optimized release version
- `wmake debug` - Build debug version with symbols  
- `wmake clean` - Clean build directory
- `wmake test` - Build and run comprehensive test suite
- `wmake test-quick` - Run quick test subset (no performance tests)
- `wmake test-unit` - Run unit tests only
- `wmake info` - Show build configuration and available targets

## Test Commands
- `cd tests && ./run_tests.sh` - Run unified test suite
- `cd tests && ./run_tests.sh -r unit -v` - Run unit tests with verbose output
- `cd tests && ./run_tests.sh -r drivers -c 3c509b` - Run 3C509B driver tests only
- `cd tests && make test-packet-ops` - Run single test category
- `cd tests && make test ARGS="--verbose"` - Run tests with specific arguments

## Code Style Guidelines
- **Headers**: Use relative paths from src/c: `#include "../include/header.h"`
- **Types**: Use stdint.h types (uint8_t, uint16_t, uint32_t), bool from stdbool.h
- **Naming**: snake_case for functions/variables, UPPER_CASE for constants/macros
- **Structs**: typedef with _t suffix: `typedef struct { ... } driver_state_t;`
- **Functions**: Descriptive names with module prefix: `hardware_init()`, `packet_send()`
- **Constants**: #define with descriptive names: `#define MAX_PACKET_SIZE 1514`
- **Comments**: Doxygen style for functions: `/** @brief Description */`
- **Error Handling**: Use defined error codes from common.h, check all return values
- **Memory**: Always check malloc/allocation results, free resources in reverse order
- **Assembly**: Use NASM syntax, include guards, proper extern declarations

## Architecture Notes
- TSR (Terminate and Stay Resident) program for DOS
- Modular design: resident code stays in memory, init code is discarded
- Hardware abstraction layer supports 3C515-TX (100Mbps) and 3C509B (10Mbps)
- Uses Open Watcom C/C++ compiler with specific flags for DOS real mode
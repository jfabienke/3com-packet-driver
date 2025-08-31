# Assembly Testing Framework

## Overview

This directory contains a comprehensive assembly language testing framework for the 3Com Packet Driver project. The framework provides complete testing infrastructure for assembly code modules, CPU detection, and assembly-C integration.

## Components

### 1. Assembly Test Framework (`asm_test_framework.asm`)

A complete test harness written in assembly language that provides:

- **Test Infrastructure**: Complete test runner and reporting system
- **Register Validation**: Comprehensive register state checking
- **Memory Testing**: Walking patterns, checkerboard, and address-in-address tests
- **Performance Measurement**: Timer-based performance validation
- **Result Reporting**: Detailed test results and statistics

#### Key Features:
- Support for up to 64 tests with comprehensive tracking
- Memory pattern testing with multiple algorithms
- Performance threshold validation
- Register preservation checking
- Detailed error reporting

### 2. CPU Detection Tests (`cpu_detect_test.asm`)

Comprehensive validation of CPU detection and feature identification:

- **CPU Type Detection**: 286/386/486/Pentium identification
- **Feature Validation**: PUSHA, 32-bit, CPUID, FPU testing
- **Instruction Set Testing**: CPU-specific instruction validation
- **Performance Testing**: Feature-based optimization validation
- **Integration Testing**: Cross-module compatibility

#### Test Categories:
- Basic CPU type detection accuracy
- 286-specific features (PUSHA/POPA, FLAGS behavior)
- 386-specific features (32-bit operations, extended addressing)
- 486-specific features (CPUID instruction, cache instructions)
- Pentium-specific features (TSC, advanced CPUID)
- FPU detection and validation
- Performance optimization validation

### 3. Assembly API Tests (`asm_api_test.c`)

C-based tests for assembly API integration:

- **Calling Convention Testing**: Parameter passing and return values
- **Register Preservation**: Validation of calling convention compliance
- **Data Type Conversion**: C-Assembly data interface validation
- **Performance Testing**: Assembly function performance measurement
- **Integration Testing**: Cross-language module integration

#### Test Areas:
- CPU detection API consistency
- Feature flag validation
- Calling convention compliance
- Memory access patterns
- Error handling integration

## Build System

### Assembly-Specific Makefile

The `Makefile` in this directory provides:

```bash
# Build all assembly tests
make all

# Run individual test suites
make test-cpu         # CPU detection tests
make test-framework   # Assembly framework tests
make test-api         # API integration tests
make test-all         # Complete test suite

# Debug builds
make debug           # Build with debug symbols
make listings        # Generate assembly listings

# Utilities
make doc             # Generate documentation
make clean           # Clean build artifacts
```

### Integration with Main Test Suite

The main tests Makefile includes assembly test integration:

```bash
# From main tests directory
make test-asm        # Run assembly API tests
make asm-test        # Run complete assembly test suite
make test-all        # Run all tests including assembly
```

## Usage Examples

### Running CPU Detection Tests

```bash
cd tests/asm
make test-cpu
```

Expected output:
```
==========================================
Running CPU Detection Tests
==========================================
Assembly Test Framework v1.0
----------------------------------------
Starting test: Basic CPU Type Detection [PASS]
Starting test: 286 Instruction Set Test [PASS]
Starting test: 386 Instruction Set Test [PASS]
...
TEST SUMMARY:
Total tests: 14
Passed: 12
Failed: 0
Skipped: 2
```

### Running Complete Assembly Test Suite

```bash
cd tests
make asm-test
```

This runs all assembly tests including:
1. CPU detection and validation
2. Assembly framework functionality
3. C-Assembly API integration

### Integration Testing

```bash
cd tests
make test-all
```

Runs comprehensive testing including C unit tests, integration tests, and assembly tests.

## Test Categories

### 1. CPU Detection Tests
- **Basic Detection**: Validates CPU type identification
- **Feature Testing**: Validates feature flag accuracy
- **Instruction Testing**: Tests CPU-specific instructions
- **Performance Testing**: Validates optimization opportunities

### 2. Register and Memory Tests
- **Register Preservation**: Ensures calling conventions
- **Memory Patterns**: Walking 1s, 0s, checkerboard patterns
- **Address Testing**: Address-in-address validation
- **Stack Management**: Stack integrity validation

### 3. Performance Tests
- **16-bit Operations**: Basic operation performance
- **32-bit Operations**: Extended operation performance (386+)
- **PUSHA/POPA**: Register save/restore performance (286+)
- **Code Patching**: Runtime optimization validation

### 4. Integration Tests
- **C-Assembly Interface**: Parameter passing validation
- **Error Handling**: Cross-language error propagation
- **Data Consistency**: Data integrity across interfaces
- **Feature Consistency**: Feature detection consistency

## Test Results and Reporting

### Assembly Framework Output
```
Assembly Test Framework v1.0
----------------------------------------
Starting test: Memory Walking Ones Test [PASS]
Starting test: Memory Checkerboard Test [PASS]
Starting test: Performance Threshold Test [PASS]

TEST SUMMARY:
Total tests: 15
Passed: 15
Failed: 0
Skipped: 0
```

### CPU Detection Output
```
CPU Detection and Validation Tests
Detected CPU type: 2 for test filtering
Running test: Basic CPU Type Detection [PASS]
Running test: 386 Instruction Set Test [PASS]
Running test: FPU Detection Test [PASS]
```

### API Integration Output
```
=== Assembly API Interface Test Suite ===
Running test: CPU Detect API Basic [PASS]
Running test: Calling Convention CDECL [PASS]
Running test: Register Preservation [PASS]
Total tests: 15
Passed: 15
Failed: 0
```

## Requirements

### Build Tools
- **NASM**: Netwide Assembler for assembly compilation
- **GCC**: GNU Compiler Collection for C compilation
- **GNU Make**: Build system support

### Target Platform
- **x86 Architecture**: 286 minimum, 386+ recommended
- **DOS/Real Mode**: Real mode or compatible environment
- **Memory**: Minimum 640KB conventional memory

## Error Handling

The framework provides comprehensive error detection:

### Assembly Test Framework Errors
- Memory test failures with pattern details
- Register preservation violations
- Performance threshold failures
- Stack corruption detection

### CPU Detection Errors
- Unsupported CPU detection
- Feature inconsistency detection
- Instruction execution failures
- CPUID validation errors

### API Integration Errors
- Calling convention violations
- Data corruption detection
- Parameter passing failures
- Cross-language consistency errors

## Performance Benchmarks

The framework includes performance validation:

### CPU Detection Performance
- Multiple detection calls per second
- Feature flag access performance
- CPUID instruction timing

### Memory Operation Performance
- Pattern write/verify speed
- Address calculation performance
- Stack operation efficiency

### API Call Performance
- C-Assembly call overhead
- Parameter passing efficiency
- Return value processing speed

## Debugging Support

### Debug Builds
```bash
make debug          # Build with debug symbols
gdb ./cpu_test_runner   # Debug CPU tests
```

### Assembly Listings
```bash
make listings       # Generate assembly listings
```

Produces detailed assembly listings with source correlation.

### Verbose Output
Most tests support verbose output for detailed debugging information.

## Limitations

### Platform Limitations
- Requires x86 compatible processor
- Real mode or compatible environment
- Limited to 16-bit addressing in some tests

### Test Limitations
- Some privileged instructions cannot be tested
- Hardware-specific features may not be testable
- Performance results are platform-dependent

## Future Enhancements

### Planned Features
- Extended CPU family support (Pentium Pro, MMX)
- 64-bit compatibility testing
- Protected mode test support
- Enhanced performance profiling

### Integration Improvements
- Automated CI/CD integration
- Cross-platform build support
- Enhanced reporting formats
- Performance regression testing

## Contributing

When adding new assembly tests:

1. Follow the existing test structure
2. Use the assembly test framework macros
3. Include comprehensive error checking
4. Document test purpose and expected results
5. Update this README with new test information

### Test Naming Convention
- `test_*_basic`: Basic functionality tests
- `test_*_features`: Feature-specific tests
- `test_*_performance`: Performance validation tests
- `test_*_integration`: Cross-module integration tests

### Error Reporting
All tests should provide detailed error information including:
- Expected vs actual values
- Test context and parameters
- Failure location and cause
- Recovery suggestions when possible
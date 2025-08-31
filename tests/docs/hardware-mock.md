# Hardware Abstraction Layer Test Suite

## Overview

The Hardware Abstraction Layer Test Suite provides comprehensive testing for the 3Com Packet Driver's hardware abstraction layer and multi-NIC management capabilities. This test suite validates the polymorphic vtable operations, multi-NIC scenarios, error recovery mechanisms, and failover functionality.

## Features Tested

### 1. Polymorphic NIC Operations (Vtable Dispatch)
- **3C509B Operations**: Tests PIO-based operations for the ISA 3C509B card
- **3C515-TX Operations**: Tests DMA-based operations for the PCI 3C515-TX card
- **Vtable Validation**: Ensures all required function pointers are present and valid
- **Polymorphic Selection**: Tests `get_nic_ops()` function for correct vtable selection

### 2. Multi-NIC Detection and Enumeration
- **Hardware Detection**: Tests `hardware_detect_all()` with multiple simulated NICs
- **NIC Count Validation**: Verifies correct detection of all available NICs
- **Enumeration Functions**: Tests `hardware_enumerate_nics()` and related functions
- **Search Functions**: Tests find by type, MAC address, and other criteria

### 3. Hardware Error Recovery Mechanisms
- **Link Loss Recovery**: Simulates and tests recovery from link failures
- **Hardware Reset**: Tests NIC reset functionality and post-reset validation
- **Self-Test Recovery**: Tests hardware self-test and validation procedures
- **Error State Management**: Validates proper error state tracking and recovery

### 4. Failover Between NICs
- **Primary/Backup Setup**: Tests failover between designated primary and backup NICs
- **Automatic Failover**: Tests automatic failover on primary NIC failure
- **Manual Failover**: Tests `hardware_test_failover()` function
- **Failover Validation**: Ensures backup NIC functionality after failover

### 5. Resource Allocation and Deallocation
- **NIC Initialization**: Tests resource allocation during NIC initialization
- **Memory Management**: Tests packet buffer allocation and deallocation
- **Resource Cleanup**: Tests proper resource cleanup during NIC shutdown
- **Memory Leak Detection**: Validates no memory leaks in allocation cycles

### 6. Hardware Capability Detection
- **3C509B Capabilities**: Tests ISA card capabilities (no DMA, no bus mastering)
- **3C515-TX Capabilities**: Tests PCI card capabilities (DMA, bus mastering, etc.)
- **Feature Detection**: Tests capability-based feature enablement
- **Capability Validation**: Ensures capabilities match hardware specifications

### 7. Resource Contention Scenarios
- **Concurrent Operations**: Tests simultaneous operations on multiple NICs
- **Resource Competition**: Tests behavior under resource contention
- **Load Balancing**: Tests packet distribution across multiple NICs
- **Performance Under Load**: Tests performance during high contention

### 8. Hardware Failure Injection
- **Error Injection**: Uses hardware mock framework to inject various error types
- **Failure Response**: Tests driver response to injected failures
- **Recovery Validation**: Tests recovery after failure injection is cleared
- **Error Types**: TX timeout, RX overrun, CRC errors, DMA errors, etc.

## Test Architecture

### Test Structure
```
hardware_test.c
├── Test Setup and Cleanup
├── Mock NIC Creation (up to 4 NICs)
├── Individual Test Functions
├── Helper Functions
└── Main Test Runner
```

### Mock Framework Integration
- Uses the existing hardware mock framework (`hardware_mock.h/c`)
- Creates up to 4 simulated NICs with different configurations
- Supports both 3C509B and 3C515-TX simulation
- Provides error injection and state manipulation

### Test Framework Integration
- Integrates with the comprehensive test framework (`test_framework.h/c`)
- Provides structured test results and reporting
- Supports test categorization and filtering
- Generates detailed test reports

## Test Configuration

### Default Configuration
- **Max NICs**: 4 simulated NICs (alternating 3C509B and 3C515-TX)
- **I/O Addresses**: 0x200, 0x220, 0x240, 0x260
- **IRQ Assignment**: 10, 11, 12, 13
- **Link Status**: All NICs initially link up
- **Test Timeout**: 5 seconds per test

### Customizable Parameters
- Number of simulated NICs
- NIC types (3C509B vs 3C515-TX)
- Hardware addresses and IRQ assignments
- Test timeout values
- Verbose output control

## Test Execution

### Standalone Execution
```bash
# Build the hardware test
make hardware_test.exe

# Run standalone hardware tests
./hardware_test.exe
```

### Integration with Test Suite
```c
// Include in comprehensive test suite
#include "../common/test_hardware.h"

int result = run_hardware_tests();
if (result != HW_TEST_SUCCESS) {
    // Handle test failures
}
```

### Test Categories
Individual test categories can be run separately:
```c
// Run only vtable tests
run_hardware_test_category(HW_TEST_VTABLE);

// Run only multi-NIC tests
run_hardware_test_category(HW_TEST_DETECTION);
```

## Expected Results

### Successful Test Run
```
3Com Packet Driver - Hardware Abstraction Layer Test Suite
==========================================================

=== Initializing Hardware Test Environment ===
Hardware test environment initialized successfully
Created 3 mock NICs for testing

=== Starting Hardware Abstraction Layer Tests ===

Running test: Vtable Operations
Testing vtable polymorphic operations
Vtable polymorphic operations test passed
PASS: Vtable Operations (duration: 12)

Running test: Multi-NIC Detection
Testing multi-NIC detection
Created 3 mock NICs for detection test
Multi-NIC detection test passed - detected 3 NICs
PASS: Multi-NIC Detection (duration: 25)

...

=== Hardware Test Summary ===
Total tests: 11
Passed: 11
Failed: 0
Skipped: 0

=== ALL HARDWARE TESTS PASSED ===
```

### Test Results Details
Each test provides:
- **Test Name**: Descriptive name of the test
- **Test Result**: PASS/FAIL/SKIP
- **Duration**: Execution time in arbitrary units
- **Details**: Success/failure reasons and additional information

## Error Handling

### Test Failures
- **Setup Failures**: If mock framework or hardware layer initialization fails
- **NIC Validation Failures**: If simulated NICs don't meet expected criteria
- **Operation Failures**: If vtable operations don't work as expected
- **Resource Failures**: If resource allocation/deallocation doesn't work properly

### Failure Recovery
- Tests are designed to be independent
- Failed tests don't prevent subsequent tests from running
- Comprehensive cleanup ensures clean state between tests
- Mock framework is reset between test categories

## Dependencies

### Required Components
- **Hardware Abstraction Layer**: `hardware.h/c`
- **Hardware Mock Framework**: `hardware_mock.h/c`
- **Test Framework**: `test_framework.h/c`
- **NIC Drivers**: `3c509b.h/c`, `3c515.h/c`
- **Supporting Systems**: Memory management, logging, diagnostics

### Build Dependencies
- **Compiler**: Open Watcom C compiler (wcc)
- **Target**: DOS real mode (16-bit)
- **Memory Model**: Small model (-ms)
- **Build System**: Makefile with DOS-style commands

## Integration Points

### With Test Framework
- Uses `test_result_t` enumeration for results
- Integrates with test timing and reporting
- Supports test configuration structures
- Provides detailed test result entries

### With Hardware Layer
- Tests actual hardware abstraction functions
- Uses real vtable operations
- Tests actual multi-NIC management functions
- Validates real hardware capability detection

### With Mock Framework
- Creates realistic hardware simulations
- Injects controlled errors for testing
- Simulates multiple NIC types simultaneously
- Provides controllable hardware state

## Maintenance

### Adding New Tests
1. Add test function following naming convention `hw_test_*`
2. Add test case entry to the test_cases array in `run_hardware_tests()`
3. Ensure proper setup and cleanup
4. Add test category if needed

### Modifying Mock Behavior
- Extend `hardware_mock.h/c` for new simulation capabilities
- Add new error injection types as needed
- Extend NIC type simulation as hardware support grows

### Updating for New Hardware
- Add new NIC types to test configuration
- Create new vtable validation for new NIC types
- Add hardware-specific capability tests
- Update mock framework for new hardware simulation

## Performance Considerations

### Test Execution Time
- Individual tests run in seconds
- Complete suite runs in under a minute
- Mock operations are lightweight
- No actual hardware I/O delays

### Memory Usage
- Small memory footprint (suitable for DOS environment)
- Controlled resource allocation testing
- Memory leak detection built-in
- Proper cleanup prevents accumulation

### Scalability
- Supports up to 8 simulated NICs
- Configurable test intensity
- Parallel operation simulation
- Resource contention testing

This comprehensive test suite ensures the reliability and robustness of the hardware abstraction layer under various conditions and multi-NIC scenarios.
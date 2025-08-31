# Standalone Test Files

This directory contains standalone test files that were developed during the implementation of various features and capabilities.

## Test Categories

### Sprint-Specific Tests
- `test_eeprom_sprint0b1.c` - EEPROM implementation testing (Sprint 0B1)
- `test_error_handling_sprint0b2.c` - Error handling testing (Sprint 0B2)  
- `test_enhanced_ring_sprint0b3.c` - Enhanced ring buffer testing (Sprint 0B3)
- `test_complete_initialization_sprint0b4.c` - Hardware initialization testing (Sprint 0B4)
- `test_busmaster_sprint0b5.c` - Bus mastering testing (Sprint 0B5)

### Feature-Specific Tests
- `test_rx_copybreak.c` - RX copybreak optimization testing
- `test_direct_pio.c` - Direct PIO implementation testing
- `test_interrupt_mitigation.c` - Interrupt mitigation testing
- `test_capabilities.c` - Hardware capability testing
- `test_hw_checksum.c` - Hardware checksumming testing
- `test_flow_control.c` - Flow control testing
- `test_media_control.c` - Media control testing
- `test_nic_buffer_pools.c` - NIC buffer pool testing
- `test_scatter_gather_dma.c` - Scatter-gather DMA testing

### Hardware-Specific Tests
- `hardware_detection_test.c` - Hardware detection validation
- `test_enhanced_cpu_detect.c` - Enhanced CPU detection testing

### Integration Tests
- `phase0a_compatibility_test.c` - Phase 0A compatibility validation
- `sprint0a_simplified_test.c` - Sprint 0A simplified testing
- `sprint0a_test_validation.c` - Sprint 0A validation testing
- `simple_media_test.c` - Simple media testing
- `validate_eeprom_implementation.c` - EEPROM implementation validation

## Purpose

These standalone tests were developed during the iterative implementation process to validate specific features and ensure proper functionality. They complement the comprehensive test framework in the main `tests/` directory by providing focused testing for individual components and features.

Each test file is self-contained and can be compiled and run independently to verify specific functionality during development and debugging.
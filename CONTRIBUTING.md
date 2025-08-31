# Contributing to 3Com Packet Driver

Thank you for your interest in contributing to the 3Com Packet Driver project! This document outlines the development workflow, coding standards, and guidelines for contributing to this production-quality DOS networking driver.

## 📋 Table of Contents

- [Getting Started](#getting-started)
- [Development Environment](#development-environment)
- [Coding Standards](#coding-standards)
- [Pull Request Process](#pull-request-process)
- [Testing Requirements](#testing-requirements)
- [Documentation Standards](#documentation-standards)
- [Community Guidelines](#community-guidelines)

## Getting Started

### Prerequisites

- **Development Tools**: Open Watcom C/C++ 1.9+, NASM assembler
- **Testing Hardware**: 3Com network interface cards for validation
- **DOS Environment**: DOS 5.0+ or DOSBox for testing
- **Version Control**: Git with GitHub account

### Project Structure
```
3com-packet-driver/
├── src/                    # Source code
│   ├── core/              # Core driver functionality
│   ├── modules/           # Hardware and feature modules
│   └── tests/             # Test suites
├── docs/                  # Documentation
│   ├── user/              # End-user guides
│   ├── developer/         # Developer documentation
│   ├── architecture/      # Technical specifications
│   └── archive/           # Historical documents
├── tools/                 # Build and development tools
├── examples/              # Usage examples and demos
└── tests/                 # Hardware test suites
```

## Development Environment

### Setting Up Your Environment

1. **Clone the Repository**
   ```bash
   git clone https://github.com/yourusername/3com-packet-driver.git
   cd 3com-packet-driver
   ```

2. **Install Development Tools**
   ```bash
   # Install Open Watcom C/C++
   # Download from: http://www.openwatcom.org/
   
   # Install NASM
   # Download from: https://www.nasm.us/
   ```

3. **Build the Project**
   ```bash
   wmake all                    # Build everything
   wmake test                   # Run test suite
   wmake release-standard       # Create release package
   ```

### Development Workflow

1. **Fork and Branch**
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Make Changes**
   - Follow coding standards
   - Add tests for new functionality
   - Update documentation as needed

3. **Test Thoroughly**
   ```bash
   wmake test-all              # Complete test matrix
   wmake test-hardware         # Hardware compatibility
   ```

4. **Submit Pull Request**
   - Ensure all tests pass
   - Update documentation
   - Follow PR template

## Coding Standards

### DOS Development Conventions

#### Memory Model and Segmentation
```c
/* Use small memory model for modules */
#pragma aux default far

/* Explicit far pointers for data crossing segments */
char __far *global_buffer;

/* Near pointers for local data */
char __near local_buffer[256];
```

#### Function Declarations
```c
/* Standard DOS calling convention */
int __pascal network_init(void);

/* Far function for module interfaces */
int __far __pascal module_entry_point(void __far *params);

/* Interrupt handlers */
void __interrupt __far packet_interrupt_handler(void);
```

### Code Style Guidelines

#### Naming Conventions
```c
/* Constants: ALL_CAPS with underscores */
#define MAX_PACKET_SIZE     1514
#define ERROR_TIMEOUT       0x8001

/* Types: snake_case with _t suffix */
typedef struct network_adapter {
    uint16_t    base_address;
    uint8_t     interrupt_line;
    char        name[32];
} network_adapter_t;

/* Functions: snake_case, descriptive names */
int initialize_network_adapter(network_adapter_t *adapter);
void cleanup_adapter_resources(network_adapter_t *adapter);

/* Variables: snake_case */
network_adapter_t primary_adapter;
uint16_t packet_count = 0;
```

#### Error Handling
```c
/* Consistent error code system */
typedef enum {
    DRIVER_SUCCESS          = 0x0000,
    DRIVER_ERROR_NO_MEMORY  = 0x0001,
    DRIVER_ERROR_NO_HARDWARE = 0x0002,
    DRIVER_ERROR_TIMEOUT    = 0x0003,
    DRIVER_ERROR_INVALID_PARAM = 0x0004
} driver_error_t;

/* Always check return values */
int result = initialize_hardware();
if (result != DRIVER_SUCCESS) {
    log_error("Hardware initialization failed: 0x%04X", result);
    return result;
}

/* Resource cleanup on error */
int allocate_resources(void) {
    void *buffer1 = NULL, *buffer2 = NULL;
    
    buffer1 = malloc(1024);
    if (buffer1 == NULL) goto cleanup_and_fail;
    
    buffer2 = malloc(2048);
    if (buffer2 == NULL) goto cleanup_and_fail;
    
    return DRIVER_SUCCESS;

cleanup_and_fail:
    if (buffer1) free(buffer1);
    if (buffer2) free(buffer2);
    return DRIVER_ERROR_NO_MEMORY;
}
```

#### Hardware Interaction
```c
/* I/O port access with proper delays */
void write_nic_register(uint16_t base, uint16_t reg, uint16_t value) {
    outp(base + reg, value);
    /* Ensure write completes before continuing */
    for (volatile int i = 0; i < IO_DELAY_CYCLES; i++);
}

/* EEPROM access with timeout protection */
uint16_t read_eeprom_word(uint16_t base, uint8_t address) {
    uint16_t timeout = EEPROM_TIMEOUT;
    
    /* Send read command */
    outp(base + EEPROM_CMD, EEPROM_READ | address);
    
    /* Wait for completion with timeout */
    while (timeout--) {
        if (!(inp(base + EEPROM_CMD) & EEPROM_BUSY)) {
            return inp(base + EEPROM_DATA);
        }
        delay_microseconds(10);
    }
    
    log_warning("EEPROM read timeout at address 0x%02X", address);
    return 0xFFFF;  /* Timeout indicator */
}
```

### Documentation in Code

#### Function Documentation
```c
/**
 * Initialize network adapter with comprehensive error handling
 * 
 * @param adapter       Pointer to adapter structure
 * @param config_flags  Configuration options (see ADAPTER_CONFIG_*)
 * @return              DRIVER_SUCCESS or error code
 * 
 * This function performs complete adapter initialization including:
 * - Hardware detection and validation
 * - EEPROM reading and MAC address extraction
 * - Interrupt configuration and testing
 * - Buffer allocation and ring setup
 * 
 * Memory Requirements: ~4KB conventional memory
 * Hardware Requirements: ISA or PCI 3Com NIC
 */
int initialize_network_adapter(network_adapter_t *adapter, uint16_t config_flags);
```

#### Module Headers
```c
/*
 * WOL.MOD - Wake-on-LAN Enterprise Module
 * 
 * Implements complete Wake-on-LAN functionality including:
 * - Magic Packet detection and validation
 * - ACPI power management integration
 * - APM BIOS interface for older systems
 * - Network activity-based wake patterns
 * 
 * Memory Usage: ~4KB XMS preferred, UMB fallback
 * Dependencies: Core Services API v1.0+, Power management BIOS
 * Compatible: All Cyclone and Tornado generation NICs
 * 
 * Author: 3Com Packet Driver Project
 * Version: 1.0 (Phase 3B)
 */
```

## Pull Request Process

### Before Submitting

1. **Code Quality Checklist**
   - [ ] Follows DOS coding conventions
   - [ ] All functions have proper documentation
   - [ ] Error handling is comprehensive
   - [ ] Memory management is correct (no leaks)
   - [ ] Hardware access uses proper timing

2. **Testing Checklist**
   - [ ] Unit tests pass for new functionality
   - [ ] Hardware compatibility tests pass
   - [ ] No regression in existing features
   - [ ] Memory usage within acceptable limits
   - [ ] Performance impact analyzed

3. **Documentation Checklist**
   - [ ] User documentation updated if needed
   - [ ] API documentation updated for public interfaces
   - [ ] Code comments explain complex algorithms
   - [ ] Architecture docs updated for major changes

### Pull Request Template

```markdown
## Description
Brief description of the changes and their purpose.

## Type of Change
- [ ] Bug fix (non-breaking change that fixes an issue)
- [ ] New feature (non-breaking change that adds functionality)
- [ ] Breaking change (fix or feature that would cause existing functionality to not work as expected)
- [ ] Documentation update
- [ ] Performance improvement

## Hardware Tested
- [ ] 3C509 family (ISA)
- [ ] 3C515 (ISA bus mastering)
- [ ] 3C590/3C595 (PCI Vortex)
- [ ] 3C900/3C905 (PCI Boomerang)
- [ ] Other: _______________

## Testing Performed
- [ ] Unit tests pass
- [ ] Hardware compatibility verified
- [ ] Memory leak testing completed
- [ ] Performance benchmarking completed
- [ ] Integration testing with common applications

## Checklist
- [ ] My code follows the DOS coding standards
- [ ] I have performed a self-review of my code
- [ ] I have commented my code, particularly in hard-to-understand areas
- [ ] I have made corresponding changes to the documentation
- [ ] My changes generate no new compiler warnings
- [ ] I have added tests that prove my fix is effective or that my feature works
- [ ] New and existing unit tests pass locally with my changes
```

## Testing Requirements

### Unit Testing
```c
/* Example unit test structure */
int test_eeprom_reading(void) {
    network_adapter_t test_adapter = {0};
    test_adapter.base_address = 0x300;
    
    /* Test valid EEPROM read */
    uint16_t mac_word = read_eeprom_word(test_adapter.base_address, 0x00);
    if (mac_word == 0xFFFF) {
        log_test_failure("EEPROM read returned timeout value");
        return TEST_FAILED;
    }
    
    /* Test invalid address handling */
    uint16_t invalid_read = read_eeprom_word(test_adapter.base_address, 0xFF);
    if (invalid_read != 0xFFFF) {
        log_test_failure("Invalid EEPROM address should return timeout");
        return TEST_FAILED;
    }
    
    log_test_success("EEPROM reading tests passed");
    return TEST_PASSED;
}
```

### Hardware Testing
```bash
# Required hardware test suite
./hardware_detection_test       # Test NIC detection
./media_type_test              # Test media auto-detection
./packet_transmission_test     # Test basic packet I/O
./interrupt_handling_test      # Test interrupt processing
./memory_management_test       # Test for memory leaks
./performance_benchmark_test   # Measure throughput
```

### Integration Testing
- Test with popular DOS networking applications
- Verify compatibility with various DOS versions
- Test memory usage under different configurations
- Validate enterprise features with real network infrastructure

## Documentation Standards

### User Documentation
- **Audience**: End users installing and configuring the driver
- **Style**: Clear, step-by-step instructions with examples
- **Format**: Markdown with consistent heading structure
- **Testing**: Instructions must be tested on real hardware

### Developer Documentation
- **Audience**: Contributors and module developers
- **Style**: Technical reference with code examples
- **Format**: Markdown with syntax highlighting
- **Coverage**: All public APIs and integration points

### Architecture Documentation
- **Audience**: System architects and advanced developers
- **Style**: Comprehensive technical specifications
- **Format**: Markdown with diagrams and technical details
- **Maintenance**: Updated with major architectural changes

## Community Guidelines

### Communication Standards
- **Be Respectful**: Treat all community members with respect
- **Be Constructive**: Provide helpful feedback and suggestions
- **Be Patient**: Remember that contributors have varying experience levels
- **Be Collaborative**: Work together to improve the project

### Issue Reporting
```markdown
## Bug Report Template

### Environment
- DOS Version: 
- Hardware: 
- Network Card: 
- Memory Manager: 

### Expected Behavior
Clear description of what should happen.

### Actual Behavior
Clear description of what actually happened.

### Steps to Reproduce
1. Step one
2. Step two
3. Step three

### Additional Context
Any additional information, logs, or screenshots.
```

### Feature Requests
- Discuss major features in GitHub Discussions first
- Provide clear use cases and justification
- Consider implementation complexity and maintenance impact
- Be willing to contribute to implementation if possible

## Development Resources

### Essential References
- **Packet Driver Specification**: FTP Software Inc. standards
- **3Com Technical Documentation**: Official NIC specifications  
- **Donald Becker's Linux Drivers**: Reference implementations
- **DOS Programming References**: DOS internals and memory management

### Tools and Utilities
- **Open Watcom C/C++**: Primary development compiler
- **NASM**: Assembly language components
- **DOSBox**: Testing and debugging environment
- **Hardware Debuggers**: Logic analyzers for hardware debugging

### Getting Help

- **GitHub Issues**: Bug reports and technical questions
- **GitHub Discussions**: Design discussions and general questions
- **Documentation**: Comprehensive guides in docs/ directory
- **Code Examples**: Reference implementations in examples/ directory

---

## Recognition

Contributors to this project are helping preserve and enhance DOS networking capabilities for retro computing, industrial systems, and educational environments. All contributions are valued, from bug reports to major feature implementations.

Thank you for contributing to the 3Com Packet Driver project!

---

*For questions about contributing, please open an issue or start a discussion on GitHub.*
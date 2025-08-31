# Core Services API Reference

This document provides complete technical reference for the Core Services API that the 3CPD loader provides to modules. This API enables third-party module development and integration with the modular packet driver architecture.

## API Overview

The Core Services API provides modules with:
- **Hardware Access**: Safe register and memory operations
- **Memory Management**: Conventional, UMB, and XMS allocation
- **Logging & Diagnostics**: Standardized debug and error reporting
- **Configuration**: Runtime parameter access and validation
- **Inter-Module Communication**: Service registration and discovery

All API functions use standard DOS calling conventions with error codes returned in AX register.

## Header File

```c
/* core_services.h - Core Services API for 3CPD modules */

#ifndef CORE_SERVICES_H
#define CORE_SERVICES_H

/* API Version */
#define CORE_API_VERSION        0x0100  /* Version 1.0 */

/* Standard Error Codes */
#define CS_SUCCESS              0x0000
#define CS_ERROR_INVALID_PARAM  0x0001
#define CS_ERROR_NO_MEMORY      0x0002
#define CS_ERROR_NOT_FOUND      0x0003
#define CS_ERROR_ALREADY_EXISTS 0x0004
#define CS_ERROR_ACCESS_DENIED  0x0005
#define CS_ERROR_TIMEOUT        0x0006
#define CS_ERROR_HARDWARE       0x0007

/* Memory Types */
#define CS_MEM_CONVENTIONAL     0x00
#define CS_MEM_UMB              0x01
#define CS_MEM_XMS              0x02

/* Log Levels */
#define CS_LOG_DEBUG            0x00
#define CS_LOG_INFO             0x01
#define CS_LOG_WARNING          0x02
#define CS_LOG_ERROR            0x03
#define CS_LOG_CRITICAL         0x04

/* Hardware Access Types */
#define CS_HW_IO_BYTE           0x00
#define CS_HW_IO_WORD           0x01
#define CS_HW_IO_DWORD          0x02
#define CS_HW_MEM_BYTE          0x10
#define CS_HW_MEM_WORD          0x11
#define CS_HW_MEM_DWORD         0x12
```

## Memory Management API

### cs_malloc()
Allocate memory block with specified type preference.

```c
void __far * __pascal cs_malloc(unsigned int size, unsigned char mem_type);
```

**Parameters:**
- `size`: Memory block size in bytes (1-65535)
- `mem_type`: Memory type preference (CS_MEM_*)

**Returns:**
- Far pointer to allocated memory block, or NULL on failure

**Example:**
```c
/* Allocate 1KB buffer in conventional memory */
void __far *buffer = cs_malloc(1024, CS_MEM_CONVENTIONAL);
if (buffer == NULL) {
    cs_log(CS_LOG_ERROR, "Failed to allocate buffer");
    return CS_ERROR_NO_MEMORY;
}
```

### cs_free()
Release previously allocated memory block.

```c
void __pascal cs_free(void __far *ptr);
```

**Parameters:**
- `ptr`: Pointer returned by cs_malloc()

**Example:**
```c
cs_free(buffer);
buffer = NULL;  /* Prevent double-free */
```

### cs_memory_info()
Query available memory in each type.

```c
unsigned int __pascal cs_memory_info(unsigned char mem_type);
```

**Parameters:**
- `mem_type`: Memory type to query (CS_MEM_*)

**Returns:**
- Available memory in KB, or 0 if type unavailable

## Hardware Access API

### cs_inb() / cs_inw() / cs_inl()
Read from I/O ports with proper access control.

```c
unsigned char  __pascal cs_inb(unsigned int port);
unsigned short __pascal cs_inw(unsigned int port);
unsigned long  __pascal cs_inl(unsigned int port);
```

**Parameters:**
- `port`: I/O port address (0x0000-0xFFFF)

**Returns:**
- Value read from port

**Example:**
```c
/* Read NIC status register */
unsigned short status = cs_inw(nic_base + 0x0E);
if (status & 0x8000) {
    cs_log(CS_LOG_INFO, "NIC interrupt pending");
}
```

### cs_outb() / cs_outw() / cs_outl()
Write to I/O ports with proper access control.

```c
void __pascal cs_outb(unsigned int port, unsigned char value);
void __pascal cs_outw(unsigned int port, unsigned short value);
void __pascal cs_outl(unsigned int port, unsigned long value);
```

**Parameters:**
- `port`: I/O port address
- `value`: Value to write to port

**Example:**
```c
/* Reset network interface */
cs_outw(nic_base + 0x0E, 0x8000);
cs_delay_ms(10);  /* Wait for reset completion */
```

### cs_peek_mem() / cs_poke_mem()
Safe memory access for hardware buffers.

```c
unsigned int __pascal cs_peek_mem(unsigned long address, unsigned char access_type);
void __pascal cs_poke_mem(unsigned long address, unsigned int value, unsigned char access_type);
```

**Parameters:**
- `address`: Physical memory address
- `value`: Value to write (poke only)
- `access_type`: Access width (CS_HW_MEM_*)

**Example:**
```c
/* Read from mapped NIC memory */
unsigned short data = cs_peek_mem(0xD0000, CS_HW_MEM_WORD);
```

## Logging & Diagnostics API

### cs_log()
Standardized logging with level filtering.

```c
void __pascal cs_log(unsigned char level, char __far *format, ...);
```

**Parameters:**
- `level`: Log level (CS_LOG_*)
- `format`: Printf-style format string
- `...`: Variable arguments

**Example:**
```c
cs_log(CS_LOG_INFO, "Module %s initialized, base=0x%04X", module_name, nic_base);
cs_log(CS_LOG_ERROR, "Hardware error: code=0x%02X", error_code);
```

### cs_debug_dump()
Hex dump of memory region for debugging.

```c
void __pascal cs_debug_dump(void __far *data, unsigned int size, char __far *description);
```

**Parameters:**
- `data`: Pointer to data to dump
- `size`: Number of bytes to dump
- `description`: Descriptive label for dump

**Example:**
```c
/* Dump EEPROM contents */
cs_debug_dump(eeprom_data, 64, "EEPROM Contents");
```

### cs_assert()
Debug assertion with automatic logging.

```c
void __pascal cs_assert(int condition, char __far *message);
```

**Parameters:**
- `condition`: Boolean condition to test
- `message`: Message to log if assertion fails

**Example:**
```c
cs_assert(buffer != NULL, "Buffer allocation failed");
cs_assert(port >= 0x100, "Invalid port address");
```

## Configuration API

### cs_config_get_string()
Retrieve string configuration parameter.

```c
char __far * __pascal cs_config_get_string(char __far *param_name);
```

**Parameters:**
- `param_name`: Parameter name (e.g., "MODULE_PATH", "DEBUG_LEVEL")

**Returns:**
- Pointer to parameter value string, or NULL if not found

**Example:**
```c
char __far *debug_level = cs_config_get_string("DEBUG_LEVEL");
if (debug_level && strcmp(debug_level, "VERBOSE") == 0) {
    enable_verbose_logging = 1;
}
```

### cs_config_get_int()
Retrieve integer configuration parameter.

```c
int __pascal cs_config_get_int(char __far *param_name, int default_value);
```

**Parameters:**
- `param_name`: Parameter name
- `default_value`: Value to return if parameter not found

**Returns:**
- Parameter value as integer

**Example:**
```c
/* Get interrupt number with default of 0x60 */
int interrupt = cs_config_get_int("INTERRUPT", 0x60);
```

### cs_config_set_string()
Set string configuration parameter (temporary).

```c
int __pascal cs_config_set_string(char __far *param_name, char __far *value);
```

**Parameters:**
- `param_name`: Parameter name
- `value`: New parameter value

**Returns:**
- CS_SUCCESS or error code

## Service Registration API

### cs_service_register()
Register service for inter-module communication.

```c
int __pascal cs_service_register(char __far *service_name, void __far *service_proc);
```

**Parameters:**
- `service_name`: Unique service name (max 31 chars)
- `service_proc`: Far pointer to service procedure

**Returns:**
- CS_SUCCESS or error code

**Example:**
```c
/* Register statistics collection service */
int __pascal statistics_service(int function, void __far *params) {
    /* Service implementation */
    return CS_SUCCESS;
}

cs_service_register("HWSTATS", statistics_service);
```

### cs_service_find()
Find registered service by name.

```c
void __far * __pascal cs_service_find(char __far *service_name);
```

**Parameters:**
- `service_name`: Service name to locate

**Returns:**
- Pointer to service procedure, or NULL if not found

**Example:**
```c
/* Use diagnostics service if available */
void (__far *diag_service)(int, void __far *) = cs_service_find("DIAGUTIL");
if (diag_service != NULL) {
    diag_service(DIAG_CABLE_TEST, &cable_params);
}
```

## Timing & Synchronization API

### cs_delay_ms()
Accurate millisecond delay using available timing sources.

```c
void __pascal cs_delay_ms(unsigned int milliseconds);
```

**Parameters:**
- `milliseconds`: Delay duration (1-65535)

**Example:**
```c
/* Reset sequence with proper timing */
cs_outw(nic_base + RESET_REG, 0x8000);
cs_delay_ms(100);  /* Wait 100ms for reset */
```

### cs_get_tick_count()
Get system tick counter for timing measurements.

```c
unsigned long __pascal cs_get_tick_count(void);
```

**Returns:**
- Current tick count (18.2 Hz resolution)

**Example:**
```c
/* Measure operation duration */
unsigned long start_ticks = cs_get_tick_count();
perform_operation();
unsigned long duration = cs_get_tick_count() - start_ticks;
cs_log(CS_LOG_DEBUG, "Operation took %lu ticks", duration);
```

## Module Lifecycle API

### Module Entry Point
All modules must implement the standard entry point.

```c
int __far __pascal module_init(void __far *core_services, void __far *module_params);
```

**Parameters:**
- `core_services`: Pointer to Core Services API table
- `module_params`: Module-specific initialization parameters

**Returns:**
- CS_SUCCESS or error code

**Example:**
```c
int __far __pascal module_init(void __far *core_services, void __far *module_params) {
    /* Initialize Core Services API access */
    if (cs_api_init(core_services) != CS_SUCCESS) {
        return CS_ERROR_INVALID_PARAM;
    }
    
    cs_log(CS_LOG_INFO, "WOL module initializing");
    
    /* Module-specific initialization */
    if (init_wake_on_lan() != CS_SUCCESS) {
        return CS_ERROR_HARDWARE;
    }
    
    /* Register services */
    cs_service_register("WOL", wol_service_handler);
    
    cs_log(CS_LOG_INFO, "WOL module ready");
    return CS_SUCCESS;
}
```

### Module Cleanup
Optional cleanup function called during unloading.

```c
void __far __pascal module_cleanup(void);
```

**Example:**
```c
void __far __pascal module_cleanup(void) {
    cs_log(CS_LOG_INFO, "WOL module shutting down");
    
    /* Disable wake-on-LAN features */
    disable_wake_on_lan();
    
    /* Free allocated resources */
    if (wol_buffer != NULL) {
        cs_free(wol_buffer);
        wol_buffer = NULL;
    }
}
```

## Error Handling Best Practices

### Error Code Conventions
```c
/* Module-specific error codes start at 0x1000 */
#define WOL_ERROR_BASE          0x1000
#define WOL_ERROR_NO_MAGIC      (WOL_ERROR_BASE + 1)
#define WOL_ERROR_POWER_STATE   (WOL_ERROR_BASE + 2)
```

### Resource Cleanup
```c
int __pascal init_module_resources(void) {
    /* Allocate resources with error handling */
    buffer1 = cs_malloc(1024, CS_MEM_CONVENTIONAL);
    if (buffer1 == NULL) goto cleanup_and_fail;
    
    buffer2 = cs_malloc(2048, CS_MEM_UMB);
    if (buffer2 == NULL) goto cleanup_and_fail;
    
    if (cs_service_register("MYSERVICE", service_handler) != CS_SUCCESS)
        goto cleanup_and_fail;
    
    return CS_SUCCESS;

cleanup_and_fail:
    /* Clean up partial initialization */
    if (buffer1) { cs_free(buffer1); buffer1 = NULL; }
    if (buffer2) { cs_free(buffer2); buffer2 = NULL; }
    return CS_ERROR_NO_MEMORY;
}
```

## Building Modules

### Compilation
```makefile
# Watcom C example makefile for module
MODULE.MOD: module.c core_services.h
    wcc -bt=dos -ms -fp3 -0 -ox module.c
    wlink system dos file module.obj name MODULE.MOD
```

### Module Format
Modules are standard DOS executables with specific entry points:
- Must be compiled for small memory model
- Entry point must be named `module_init`
- Optional cleanup function named `module_cleanup`
- Maximum size: 64KB per module

### Testing
```c
/* Test harness integration */
#ifdef TESTING
int __pascal run_module_tests(void) {
    cs_log(CS_LOG_INFO, "Running module self-tests");
    
    /* Test 1: Basic functionality */
    if (test_basic_operation() != CS_SUCCESS) {
        cs_log(CS_LOG_ERROR, "Basic operation test failed");
        return CS_ERROR_HARDWARE;
    }
    
    /* Test 2: Error handling */
    if (test_error_conditions() != CS_SUCCESS) {
        cs_log(CS_LOG_ERROR, "Error handling test failed");
        return CS_ERROR_INVALID_PARAM;
    }
    
    cs_log(CS_LOG_INFO, "All module tests passed");
    return CS_SUCCESS;
}
#endif
```

This API reference provides the foundation for developing third-party modules that integrate seamlessly with the 3Com packet driver architecture. For additional examples and advanced topics, see the [Module Development Guide](module-development.md) and [Architecture Overview](../architecture/overview.md).
/**
 * @file abi_validation.c
 * @brief ABI Structure Validation Implementation
 * 
 * Runtime validation of structure sizes and field offsets to ensure
 * ABI compatibility across different compiler versions and settings.
 */

#include "../include/abi_packing.h"
#include "../include/common.h"
#include "../include/logging.h"
#include <stddef.h>
#include <stdio.h>

/* Test structures for ABI validation */
ABI_STRUCT(abi_test_struct) {
    uint8_t byte_field;         /* Offset 0 */
    uint16_t word_field;        /* Offset 1 (packed) */
    uint32_t dword_field;       /* Offset 3 (packed) */
    uint8_t final_byte;         /* Offset 7 (packed) */
} ABI_STRUCT_END;

/* Expected sizes for validation */
#define EXPECTED_ABI_TEST_SIZE  8

/**
 * @brief Runtime structure size validation
 */
int abi_validate_struct_size(const char *struct_name, 
                             size_t actual_size, 
                             size_t expected_size) {
    if (!struct_name) {
        return ERROR_INVALID_PARAMETER;
    }
    
    if (actual_size != expected_size) {
        LOG_ERROR("ABI Validation: %s size mismatch - actual %u, expected %u", 
                  struct_name, (unsigned)actual_size, (unsigned)expected_size);
        return ERROR_ABI_SIZE;
    }
    
    LOG_DEBUG("ABI Validation: %s size validated - %u bytes", 
              struct_name, (unsigned)actual_size);
    
    return SUCCESS;
}

/**
 * @brief Runtime field offset validation
 */
int abi_validate_field_offset(const char *struct_name,
                              const char *field_name,
                              size_t actual_offset,
                              size_t expected_offset) {
    if (!struct_name || !field_name) {
        return ERROR_INVALID_PARAMETER;
    }
    
    if (actual_offset != expected_offset) {
        LOG_ERROR("ABI Validation: %s.%s offset mismatch - actual %u, expected %u", 
                  struct_name, field_name, 
                  (unsigned)actual_offset, (unsigned)expected_offset);
        return ERROR_ABI_ALIGNMENT;
    }
    
    LOG_DEBUG("ABI Validation: %s.%s offset validated - %u", 
              struct_name, field_name, (unsigned)actual_offset);
    
    return SUCCESS;
}

/**
 * @brief Initialize ABI validation system
 */
int abi_init_validation(void) {
    int result;
    
    LOG_INFO("ABI Validation: Initializing ABI compatibility checks");
    
    /* Test basic structure packing */
    result = abi_test_packing();
    if (result != SUCCESS) {
        LOG_ERROR("ABI Validation: Structure packing test failed: %d", result);
        return result;
    }
    
    /* Validate critical ABI structures */
    result = abi_validate_struct_size("packet_header", 
                                     sizeof(packet_header), 8);
    if (result != SUCCESS) {
        return result;
    }
    
    result = abi_validate_field_offset("packet_header", "type", 
                                      offsetof(packet_header, type), 2);
    if (result != SUCCESS) {
        return result;
    }
    
    result = abi_validate_field_offset("packet_header", "checksum", 
                                      offsetof(packet_header, checksum), 6);
    if (result != SUCCESS) {
        return result;
    }
    
    /* Validate module interface structure */
    result = abi_validate_struct_size("module_interface", 
                                     sizeof(module_interface), 20);
    if (result != SUCCESS) {
        return result;
    }
    
    result = abi_validate_field_offset("module_interface", "version", 
                                      offsetof(module_interface, version), 0);
    if (result != SUCCESS) {
        return result;
    }
    
    result = abi_validate_field_offset("module_interface", "flags", 
                                      offsetof(module_interface, flags), 8);
    if (result != SUCCESS) {
        return result;
    }
    
    LOG_INFO("ABI Validation: All ABI compatibility checks passed");
    
    return SUCCESS;
}

/**
 * @brief Print structure layout information
 */
void abi_debug_print_layout(const char *struct_name, size_t struct_size) {
    if (!struct_name) {
        return;
    }
    
    LOG_DEBUG("ABI Debug: %s layout - size: %u bytes", 
              struct_name, (unsigned)struct_size);
    
    /* Print compiler information */
#ifdef __WATCOMC__
    LOG_DEBUG("ABI Debug: Compiled with Watcom C/C++");
#elif defined(__GNUC__)
    LOG_DEBUG("ABI Debug: Compiled with GCC %d.%d.%d", 
              __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
    LOG_DEBUG("ABI Debug: Compiled with MSVC %d", _MSC_VER);
#else
    LOG_DEBUG("ABI Debug: Unknown compiler");
#endif

    /* Print structure packing status */
#if HAS_DISCARDABLE_SEGMENTS
    LOG_DEBUG("ABI Debug: Structure packing enabled");
#else
    LOG_DEBUG("ABI Debug: Structure packing disabled");
#endif
}

/**
 * @brief Check compiler-specific structure packing
 */
int abi_test_packing(void) {
    size_t test_size;
    int result = SUCCESS;
    
    LOG_DEBUG("ABI Test: Testing structure packing");
    
    /* Test packed structure size */
    test_size = sizeof(abi_test_struct);
    
    LOG_DEBUG("ABI Test: Test structure size: %u bytes (expected %u)", 
              (unsigned)test_size, EXPECTED_ABI_TEST_SIZE);
    
    if (test_size != EXPECTED_ABI_TEST_SIZE) {
        LOG_ERROR("ABI Test: Structure packing failed - size %u != expected %u", 
                  (unsigned)test_size, EXPECTED_ABI_TEST_SIZE);
        result = ERROR_ABI_SIZE;
    }
    
    /* Test field offsets */
    if (offsetof(abi_test_struct, byte_field) != 0) {
        LOG_ERROR("ABI Test: byte_field offset %u != 0", 
                  (unsigned)offsetof(abi_test_struct, byte_field));
        result = ERROR_ABI_ALIGNMENT;
    }
    
    if (offsetof(abi_test_struct, word_field) != 1) {
        LOG_ERROR("ABI Test: word_field offset %u != 1", 
                  (unsigned)offsetof(abi_test_struct, word_field));
        result = ERROR_ABI_ALIGNMENT;
    }
    
    if (offsetof(abi_test_struct, dword_field) != 3) {
        LOG_ERROR("ABI Test: dword_field offset %u != 3", 
                  (unsigned)offsetof(abi_test_struct, dword_field));
        result = ERROR_ABI_ALIGNMENT;
    }
    
    if (offsetof(abi_test_struct, final_byte) != 7) {
        LOG_ERROR("ABI Test: final_byte offset %u != 7", 
                  (unsigned)offsetof(abi_test_struct, final_byte));
        result = ERROR_ABI_ALIGNMENT;
    }
    
    if (result == SUCCESS) {
        LOG_INFO("ABI Test: Structure packing validation passed");
        
        /* Print detailed layout info in debug mode */
        abi_debug_print_layout("abi_test_struct", test_size);
        LOG_DEBUG("ABI Test: Field offsets - byte:0, word:1, dword:3, final:7");
    } else {
        LOG_ERROR("ABI Test: Structure packing validation failed");
        
        /* Print actual layout for debugging */
        LOG_ERROR("ABI Test: Actual offsets - byte:%u, word:%u, dword:%u, final:%u",
                  (unsigned)offsetof(abi_test_struct, byte_field),
                  (unsigned)offsetof(abi_test_struct, word_field),
                  (unsigned)offsetof(abi_test_struct, dword_field),
                  (unsigned)offsetof(abi_test_struct, final_byte));
    }
    
    return result;
}

/* Diagnostic functions for specific structures */

/**
 * @brief Validate packet header structure
 */
int abi_validate_packet_header(void) {
    int result;
    
    LOG_DEBUG("ABI Validation: Validating packet_header structure");
    
    result = abi_validate_struct_size("packet_header", sizeof(packet_header), 8);
    if (result != SUCCESS) return result;
    
    result = abi_validate_field_offset("packet_header", "length", 
                                      offsetof(packet_header, length), 0);
    if (result != SUCCESS) return result;
    
    result = abi_validate_field_offset("packet_header", "type", 
                                      offsetof(packet_header, type), 2);
    if (result != SUCCESS) return result;
    
    result = abi_validate_field_offset("packet_header", "flags", 
                                      offsetof(packet_header, flags), 4);
    if (result != SUCCESS) return result;
    
    result = abi_validate_field_offset("packet_header", "checksum", 
                                      offsetof(packet_header, checksum), 6);
    if (result != SUCCESS) return result;
    
    LOG_DEBUG("ABI Validation: packet_header structure validated");
    return SUCCESS;
}

/**
 * @brief Validate module interface structure
 */
int abi_validate_module_interface(void) {
    int result;
    
    LOG_DEBUG("ABI Validation: Validating module_interface structure");
    
    result = abi_validate_struct_size("module_interface", sizeof(module_interface), 20);
    if (result != SUCCESS) return result;
    
    result = abi_validate_field_offset("module_interface", "version", 
                                      offsetof(module_interface, version), 0);
    if (result != SUCCESS) return result;
    
    result = abi_validate_field_offset("module_interface", "size", 
                                      offsetof(module_interface, size), 4);
    if (result != SUCCESS) return result;
    
    result = abi_validate_field_offset("module_interface", "flags", 
                                      offsetof(module_interface, flags), 8);
    if (result != SUCCESS) return result;
    
    LOG_DEBUG("ABI Validation: module_interface structure validated");
    return SUCCESS;
}
/* ABI Validation Tool - 3Com Packet Driver Modular Architecture
 * Version: 1.0
 * Date: 2025-08-22
 * 
 * Validates the module ABI implementation against the specification.
 * Tests header size, field alignment, and functionality.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../../include/module_abi.h"

/* Test results structure */
typedef struct {
    int tests_run;
    int tests_passed;
    int tests_failed;
} test_results_t;

static test_results_t results = {0, 0, 0};

/* Test macros */
#define TEST_ASSERT(condition, message) \
    do { \
        results.tests_run++; \
        if (condition) { \
            results.tests_passed++; \
            printf("PASS: %s\n", message); \
        } else { \
            results.tests_failed++; \
            printf("FAIL: %s\n", message); \
        } \
    } while(0)

#define TEST_EQUAL(actual, expected, message) \
    TEST_ASSERT((actual) == (expected), message)

/* Header size and alignment tests */
static void test_header_structure(void) {
    printf("=== Testing Module Header Structure ===\n");
    
    /* Test exact 64-byte size */
    TEST_EQUAL(sizeof(module_header_t), 64, "Module header is exactly 64 bytes");
    
    /* Test field offsets */
    TEST_EQUAL(offsetof(module_header_t, signature), 0x00, "Signature at offset 0x00");
    TEST_EQUAL(offsetof(module_header_t, abi_version), 0x04, "ABI version at offset 0x04");
    TEST_EQUAL(offsetof(module_header_t, module_type), 0x05, "Module type at offset 0x05");
    TEST_EQUAL(offsetof(module_header_t, flags), 0x06, "Flags at offset 0x06");
    
    TEST_EQUAL(offsetof(module_header_t, total_size_para), 0x08, "Total size at offset 0x08");
    TEST_EQUAL(offsetof(module_header_t, resident_size_para), 0x0A, "Resident size at offset 0x0A");
    TEST_EQUAL(offsetof(module_header_t, cold_size_para), 0x0C, "Cold size at offset 0x0C");
    TEST_EQUAL(offsetof(module_header_t, alignment_para), 0x0E, "Alignment at offset 0x0E");
    
    TEST_EQUAL(offsetof(module_header_t, init_offset), 0x10, "Init offset at offset 0x10");
    TEST_EQUAL(offsetof(module_header_t, api_offset), 0x12, "API offset at offset 0x12");
    TEST_EQUAL(offsetof(module_header_t, isr_offset), 0x14, "ISR offset at offset 0x14");
    TEST_EQUAL(offsetof(module_header_t, unload_offset), 0x16, "Unload offset at offset 0x16");
    
    TEST_EQUAL(offsetof(module_header_t, export_table_offset), 0x18, "Export table at offset 0x18");
    TEST_EQUAL(offsetof(module_header_t, export_count), 0x1A, "Export count at offset 0x1A");
    TEST_EQUAL(offsetof(module_header_t, reloc_table_offset), 0x1C, "Reloc table at offset 0x1C");
    TEST_EQUAL(offsetof(module_header_t, reloc_count), 0x1E, "Reloc count at offset 0x1E");
    
    TEST_EQUAL(offsetof(module_header_t, bss_size_para), 0x20, "BSS size at offset 0x20");
    TEST_EQUAL(offsetof(module_header_t, required_cpu), 0x22, "Required CPU at offset 0x22");
    TEST_EQUAL(offsetof(module_header_t, required_features), 0x24, "Required features at offset 0x24");
    TEST_EQUAL(offsetof(module_header_t, module_id), 0x26, "Module ID at offset 0x26");
    
    TEST_EQUAL(offsetof(module_header_t, module_name), 0x28, "Module name at offset 0x28");
    TEST_EQUAL(offsetof(module_header_t, name_padding), 0x33, "Name padding at offset 0x33");
    
    TEST_EQUAL(offsetof(module_header_t, header_checksum), 0x34, "Header checksum at offset 0x34");
    TEST_EQUAL(offsetof(module_header_t, image_checksum), 0x36, "Image checksum at offset 0x36");
    TEST_EQUAL(offsetof(module_header_t, vendor_id), 0x38, "Vendor ID at offset 0x38");
    TEST_EQUAL(offsetof(module_header_t, build_timestamp), 0x3C, "Build timestamp at offset 0x3C");
    
    printf("\n");
}

/* Export and relocation structure tests */
static void test_subsidiary_structures(void) {
    printf("=== Testing Subsidiary Structures ===\n");
    
    /* Export entry structure */
    TEST_EQUAL(sizeof(export_entry_t), 12, "Export entry is exactly 12 bytes");
    TEST_EQUAL(offsetof(export_entry_t, symbol_name), 0, "Symbol name at offset 0");
    TEST_EQUAL(offsetof(export_entry_t, symbol_offset), 8, "Symbol offset at offset 8");
    TEST_EQUAL(offsetof(export_entry_t, symbol_flags), 10, "Symbol flags at offset 10");
    
    /* Relocation entry structure */
    TEST_EQUAL(sizeof(reloc_entry_t), 4, "Relocation entry is exactly 4 bytes");
    TEST_EQUAL(offsetof(reloc_entry_t, reloc_type), 0, "Reloc type at offset 0");
    TEST_EQUAL(offsetof(reloc_entry_t, reserved), 1, "Reserved at offset 1");
    TEST_EQUAL(offsetof(reloc_entry_t, reloc_offset), 2, "Reloc offset at offset 2");
    
    printf("\n");
}

/* Header validation function tests */
static void test_header_validation(void) {
    printf("=== Testing Header Validation ===\n");
    
    /* Create valid header */
    module_header_t valid_header = {
        .signature = "MD64",
        .abi_version = MODULE_ABI_VERSION,
        .module_type = MODULE_TYPE_NIC,
        .flags = 0,
        .total_size_para = 10,
        .resident_size_para = 8,
        .cold_size_para = 2,
        .alignment_para = 1,
        .init_offset = 64,
        .api_offset = 128,
        .isr_offset = 0,
        .unload_offset = 192,
        .export_table_offset = 0,
        .export_count = 0,
        .reloc_table_offset = 0,
        .reloc_count = 0,
        .bss_size_para = 0,
        .required_cpu = CPU_TYPE_80286,
        .required_features = FEATURE_NONE,
        .module_id = 0x1234,
        .module_name = "TEST",
        .name_padding = 0,
        .header_checksum = 0,
        .image_checksum = 0,
        .vendor_id = 0,
        .build_timestamp = 0,
        .reserved = {0, 0}
    };
    
    TEST_ASSERT(validate_module_header(&valid_header), "Valid header passes validation");
    
    /* Test invalid signature */
    module_header_t invalid_sig = valid_header;
    strcpy(invalid_sig.signature, "XXXX");
    TEST_ASSERT(!validate_module_header(&invalid_sig), "Invalid signature fails validation");
    
    /* Test invalid ABI version */
    module_header_t invalid_abi = valid_header;
    invalid_abi.abi_version = 99;
    TEST_ASSERT(!validate_module_header(&invalid_abi), "Invalid ABI version fails validation");
    
    /* Test invalid size relationship */
    module_header_t invalid_size = valid_header;
    invalid_size.resident_size_para = 20;  /* Larger than total */
    TEST_ASSERT(!validate_module_header(&invalid_size), "Invalid size relationship fails validation");
    
    /* Test entry point out of bounds */
    module_header_t invalid_entry = valid_header;
    invalid_entry.init_offset = 200;  /* Beyond module end */
    TEST_ASSERT(!validate_module_header(&invalid_entry), "Out-of-bounds entry point fails validation");
    
    printf("\n");
}

/* Checksum calculation tests */
static void test_checksum_calculation(void) {
    printf("=== Testing Checksum Calculation ===\n");
    
    module_header_t header = {0};
    strcpy(header.signature, "MD64");
    header.abi_version = 1;
    header.module_type = MODULE_TYPE_NIC;
    
    /* Calculate checksum */
    uint16_t checksum = calculate_header_checksum(&header);
    TEST_ASSERT(checksum != 0, "Checksum calculation produces non-zero result");
    
    /* Verify checksum verification */
    header.header_checksum = checksum;
    uint16_t verify_checksum = calculate_header_checksum(&header);
    TEST_ASSERT(verify_checksum != checksum, "Checksum verification excludes checksum field");
    
    printf("\n");
}

/* Symbol resolution performance test */
static void test_symbol_resolution_performance(void) {
    printf("=== Testing Symbol Resolution Performance ===\n");
    
    /* Create test symbols */
    resolved_symbol_t test_symbols[10];
    char symbol_names[10][9] = {
        "aaa", "bbb", "ccc", "ddd", "eee",
        "fff", "ggg", "hhh", "iii", "jjj"
    };
    
    /* Register test symbols */
    for (int i = 0; i < 10; i++) {
        strcpy(test_symbols[i].symbol_name, symbol_names[i]);
        test_symbols[i].symbol_address = MK_FP(0x1000, i * 16);
        test_symbols[i].symbol_flags = SYMBOL_FLAG_FUNCTION;
        test_symbols[i].module_id = 0x1234;
        
        int result = register_symbol(&test_symbols[i]);
        TEST_ASSERT(result == MODULE_SUCCESS, "Symbol registration succeeds");
    }
    
    /* Test symbol resolution */
    void far *addr = resolve_symbol("eee");
    TEST_ASSERT(addr != NULL, "Symbol resolution finds existing symbol");
    TEST_ASSERT(FP_OFF(addr) == 4 * 16, "Symbol resolution returns correct address");
    
    /* Test non-existent symbol */
    addr = resolve_symbol("zzz");
    TEST_ASSERT(addr == NULL, "Symbol resolution returns NULL for non-existent symbol");
    
    printf("\n");
}

/* Module constants validation */
static void test_module_constants(void) {
    printf("=== Testing Module Constants ===\n");
    
    /* Test module types */
    TEST_EQUAL(MODULE_TYPE_NIC, 0x01, "NIC module type is 0x01");
    TEST_EQUAL(MODULE_TYPE_SERVICE, 0x02, "Service module type is 0x02");
    TEST_EQUAL(MODULE_TYPE_FEATURE, 0x03, "Feature module type is 0x03");
    TEST_EQUAL(MODULE_TYPE_DIAGNOSTIC, 0x04, "Diagnostic module type is 0x04");
    
    /* Test CPU types */
    TEST_EQUAL(CPU_TYPE_80286, 0x0286, "80286 CPU type is 0x0286");
    TEST_EQUAL(CPU_TYPE_80386, 0x0386, "80386 CPU type is 0x0386");
    TEST_EQUAL(CPU_TYPE_80486, 0x0486, "80486 CPU type is 0x0486");
    TEST_EQUAL(CPU_TYPE_PENTIUM, 0x0586, "Pentium CPU type is 0x0586");
    
    /* Test relocation types */
    TEST_EQUAL(RELOC_TYPE_SEG_OFS, 0x01, "Segment:offset relocation is 0x01");
    TEST_EQUAL(RELOC_TYPE_SEGMENT, 0x02, "Segment relocation is 0x02");
    TEST_EQUAL(RELOC_TYPE_OFFSET, 0x03, "Offset relocation is 0x03");
    
    /* Test signature and version */
    TEST_ASSERT(strcmp(MODULE_SIGNATURE, "MD64") == 0, "Module signature is 'MD64'");
    TEST_EQUAL(MODULE_ABI_VERSION, 1, "ABI version is 1");
    
    printf("\n");
}

/* Print test summary */
static void print_test_summary(void) {
    printf("=== Test Summary ===\n");
    printf("Tests run: %d\n", results.tests_run);
    printf("Tests passed: %d\n", results.tests_passed);
    printf("Tests failed: %d\n", results.tests_failed);
    
    if (results.tests_failed == 0) {
        printf("SUCCESS: All tests passed!\n");
    } else {
        printf("FAILURE: %d tests failed!\n", results.tests_failed);
    }
    printf("\n");
}

int main(void) {
    printf("3Com Packet Driver Module ABI Validation Tool v1.0\n");
    printf("=== Validating ABI v0.9 Implementation ===\n\n");
    
    /* Run all test suites */
    test_header_structure();
    test_subsidiary_structures();
    test_header_validation();
    test_checksum_calculation();
    test_symbol_resolution_performance();
    test_module_constants();
    
    /* Print summary */
    print_test_summary();
    
    return (results.tests_failed == 0) ? 0 : 1;
}
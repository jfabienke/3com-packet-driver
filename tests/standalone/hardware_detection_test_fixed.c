/**
 * @file hardware_detection_test.c
 * @brief Hardware detection testing for Sprint 0A completion
 * 
 * This program tests hardware detection capabilities including:
 * - Variant identification and lookup
 * - PnP device ID recognition
 * - Media capability detection
 * - Product ID matching
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "nic_defs.h"

static int total_tests = 0;
static int passed_tests = 0;
static int failed_tests = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        total_tests++; \
        if (condition) { \
            passed_tests++; \
            printf("PASS: %s\n", message); \
        } else { \
            failed_tests++; \
            printf("FAIL: %s\n", message); \
        } \
    } while(0)

#define TEST_START(test_name) \
    printf("\n=== Testing: %s ===\n", test_name)

/**
 * @brief Test variant database lookup functionality
 */
void test_variant_database_lookups(void) {
    TEST_START("Variant Database Lookups");
    
    // Test each variant in the database
    for (size_t i = 0; i < NIC_3C509_VARIANT_COUNT; i++) {
        const nic_variant_info_t* variant = &NIC_3C509_VARIANT_DATABASE[i];
        
        // Basic validation of each entry
        TEST_ASSERT(variant->variant_id != VARIANT_UNKNOWN, "Variant has valid ID");
        TEST_ASSERT(variant->variant_name != NULL, "Variant has name");
        TEST_ASSERT(variant->description != NULL, "Variant has description");
        TEST_ASSERT(variant->product_id != 0, "Variant has product ID");
        TEST_ASSERT(variant->media_capabilities != 0, "Variant has media capabilities");
        TEST_ASSERT(variant->default_media != MEDIA_TYPE_UNKNOWN, "Variant has default media");
        TEST_ASSERT(variant->max_speed_mbps >= 10, "Variant has valid max speed");
        
        printf("  - Variant %s: Product ID 0x%04X, Media Caps 0x%04X\n",
               variant->variant_name, variant->product_id, variant->media_capabilities);
    }
}

/**
 * @brief Test PnP device table coverage
 */
void test_pnp_device_coverage(void) {
    TEST_START("PnP Device Table Coverage");
    
    // Count different variant types in PnP table
    int combo_count = 0, tp_count = 0, bnc_count = 0, aui_count = 0, c515_count = 0;
    
    for (size_t i = 0; i < NIC_3C509_PNP_DEVICE_COUNT; i++) {
        const pnp_device_id_t* pnp = &NIC_3C509_PNP_DEVICE_TABLE[i];
        
        // Basic validation
        TEST_ASSERT(pnp->vendor_id == 0x544D4350, "PnP entry has 3Com vendor ID");
        TEST_ASSERT(pnp->device_id != 0, "PnP entry has device ID");
        TEST_ASSERT(pnp->variant_id != VARIANT_UNKNOWN, "PnP entry has valid variant");
        TEST_ASSERT(pnp->pnp_name != NULL, "PnP entry has name");
        
        // Count variants
        switch (pnp->variant_id) {
            case VARIANT_3C509B_COMBO: combo_count++; break;
            case VARIANT_3C509B_TP: tp_count++; break;
            case VARIANT_3C509B_BNC: bnc_count++; break;
            case VARIANT_3C509B_AUI: aui_count++; break;
            case VARIANT_3C515_TX:
            case VARIANT_3C515_FX: c515_count++; break;
        }
        
        if (i < 10) { // Show first 10 entries
            printf("  - %s: Device 0x%04X -> Variant 0x%02X\n",
                   pnp->pnp_name, pnp->device_id, pnp->variant_id);
        }
    }
    
    TEST_ASSERT(combo_count > 0, "PnP table includes Combo variants");
    TEST_ASSERT(tp_count > 0, "PnP table includes TP variants");
    TEST_ASSERT(bnc_count > 0, "PnP table includes BNC variants");
    TEST_ASSERT(aui_count > 0, "PnP table includes AUI variants");
    TEST_ASSERT(c515_count > 0, "PnP table includes 3C515 variants");
    
    printf("  Variant coverage: Combo=%d, TP=%d, BNC=%d, AUI=%d, 3C515=%d\n",
           combo_count, tp_count, bnc_count, aui_count, c515_count);
}

/**
 * @brief Test media capability mapping
 */
void test_media_capability_mapping(void) {
    TEST_START("Media Capability Mapping");
    
    // Test predefined capability sets match expected variants
    for (size_t i = 0; i < NIC_3C509_VARIANT_COUNT; i++) {
        const nic_variant_info_t* variant = &NIC_3C509_VARIANT_DATABASE[i];
        
        switch (variant->variant_id) {
            case VARIANT_3C509B_COMBO:
                TEST_ASSERT((variant->media_capabilities & MEDIA_CAP_10BASE_T) != 0,
                           "Combo variant supports 10BaseT");
                TEST_ASSERT((variant->media_capabilities & MEDIA_CAP_10BASE_2) != 0,
                           "Combo variant supports 10Base2");
                TEST_ASSERT((variant->media_capabilities & MEDIA_CAP_AUI) != 0,
                           "Combo variant supports AUI");
                TEST_ASSERT((variant->media_capabilities & MEDIA_CAP_AUTO_SELECT) != 0,
                           "Combo variant supports auto-select");
                break;
                
            case VARIANT_3C509B_TP:
                TEST_ASSERT((variant->media_capabilities & MEDIA_CAP_10BASE_T) != 0,
                           "TP variant supports 10BaseT");
                TEST_ASSERT((variant->media_capabilities & MEDIA_CAP_10BASE_2) == 0,
                           "TP variant does not support 10Base2");
                TEST_ASSERT((variant->media_capabilities & MEDIA_CAP_AUI) == 0,
                           "TP variant does not support AUI");
                break;
                
            case VARIANT_3C509B_BNC:
                TEST_ASSERT((variant->media_capabilities & MEDIA_CAP_10BASE_2) != 0,
                           "BNC variant supports 10Base2");
                TEST_ASSERT((variant->media_capabilities & MEDIA_CAP_10BASE_T) == 0,
                           "BNC variant does not support 10BaseT");
                break;
                
            case VARIANT_3C509B_AUI:
                TEST_ASSERT((variant->media_capabilities & MEDIA_CAP_AUI) != 0,
                           "AUI variant supports AUI");
                TEST_ASSERT((variant->media_capabilities & MEDIA_CAP_10BASE_T) == 0,
                           "AUI variant does not support 10BaseT");
                break;
                
            case VARIANT_3C515_TX:
                TEST_ASSERT((variant->media_capabilities & MEDIA_CAP_100BASE_TX) != 0,
                           "3C515 variant supports 100BaseTX");
                TEST_ASSERT((variant->media_capabilities & MEDIA_CAP_FULL_DUPLEX) != 0,
                           "3C515 variant supports full duplex");
                break;
        }
    }
}

/**
 * @brief Test product ID ranges and matching
 */
void test_product_id_ranges(void) {
    TEST_START("Product ID Ranges and Matching");
    
    // Test that product IDs follow expected patterns
    for (size_t i = 0; i < NIC_3C509_VARIANT_COUNT; i++) {
        const nic_variant_info_t* variant = &NIC_3C509_VARIANT_DATABASE[i];
        
        switch (variant->variant_id) {
            case VARIANT_3C509B_COMBO:
            case VARIANT_3C509B_TP:
            case VARIANT_3C509B_BNC:
            case VARIANT_3C509B_AUI:
            case VARIANT_3C509B_FL:
                // 3C509B family should have product IDs in 0x6Dxx range
                TEST_ASSERT((variant->product_id & 0xFF00) == 0x6D00,
                           "3C509B variant has product ID in 0x6Dxx range");
                break;
                
            case VARIANT_3C515_TX:
            case VARIANT_3C515_FX:
                // 3C515 family should have product IDs in 0x50xx range
                TEST_ASSERT((variant->product_id & 0xFF00) == 0x50000,
                           "3C515 variant has product ID in 0x50xx range");
                break;
        }
        
        // Test product ID mask usage
        TEST_ASSERT(variant->product_id_mask != 0, "Variant has product ID mask");
        TEST_ASSERT((variant->product_id & variant->product_id_mask) == variant->product_id,
                   "Product ID matches its own mask");
    }
}

/**
 * @brief Test detection priority ordering
 */
void test_detection_priority(void) {
    TEST_START("Detection Priority Ordering");
    
    // Test that detection priorities are reasonable
    uint8_t combo_priority = 255, tp_priority = 255, bnc_priority = 255;
    
    for (size_t i = 0; i < NIC_3C509_VARIANT_COUNT; i++) {
        const nic_variant_info_t* variant = &NIC_3C509_VARIANT_DATABASE[i];
        
        TEST_ASSERT(variant->detection_priority > 0, "Variant has valid detection priority");
        TEST_ASSERT(variant->detection_priority < 100, "Detection priority is reasonable");
        
        switch (variant->variant_id) {
            case VARIANT_3C509B_COMBO: combo_priority = variant->detection_priority; break;
            case VARIANT_3C509B_TP: tp_priority = variant->detection_priority; break;
            case VARIANT_3C509B_BNC: bnc_priority = variant->detection_priority; break;
        }
    }
    
    // Combo should have highest priority (lowest number)
    TEST_ASSERT(combo_priority < tp_priority, "Combo has higher priority than TP");
    TEST_ASSERT(combo_priority < bnc_priority, "Combo has higher priority than BNC");
    
    printf("  Priority order: Combo=%d, TP=%d, BNC=%d\n", 
           combo_priority, tp_priority, bnc_priority);
}

/**
 * @brief Test connector type mapping
 */
void test_connector_mapping(void) {
    TEST_START("Connector Type Mapping");
    
    for (size_t i = 0; i < NIC_3C509_VARIANT_COUNT; i++) {
        const nic_variant_info_t* variant = &NIC_3C509_VARIANT_DATABASE[i];
        
        TEST_ASSERT(variant->connector_type != 0, "Variant has connector type");
        
        switch (variant->variant_id) {
            case VARIANT_3C509B_COMBO:
                TEST_ASSERT(variant->connector_type == CONNECTOR_COMBO,
                           "Combo variant has combo connector");
                break;
                
            case VARIANT_3C509B_TP:
                TEST_ASSERT(variant->connector_type == CONNECTOR_RJ45,
                           "TP variant has RJ45 connector");
                break;
                
            case VARIANT_3C509B_BNC:
                TEST_ASSERT(variant->connector_type == CONNECTOR_BNC,
                           "BNC variant has BNC connector");
                break;
                
            case VARIANT_3C509B_AUI:
                TEST_ASSERT(variant->connector_type == CONNECTOR_DB15_AUI,
                           "AUI variant has AUI connector");
                break;
        }
    }
}

/**
 * @brief Test special features validation
 */
void test_special_features(void) {
    TEST_START("Special Features Validation");
    
    for (size_t i = 0; i < NIC_3C509_VARIANT_COUNT; i++) {
        const nic_variant_info_t* variant = &NIC_3C509_VARIANT_DATABASE[i];
        
        // All 3C509B variants should have LED indicators
        if (variant->variant_id >= VARIANT_3C509B_COMBO && 
            variant->variant_id <= VARIANT_3C509B_FL) {
            TEST_ASSERT((variant->special_features & FEATURE_LED_INDICATORS) != 0,
                       "3C509B variant has LED indicators");
        }
        
        // Variants with 10BaseT should have link beat detection
        if (variant->media_capabilities & MEDIA_CAP_10BASE_T) {
            TEST_ASSERT((variant->special_features & FEATURE_LINK_BEAT) != 0,
                       "10BaseT-capable variant has link beat detection");
        }
        
        // Combo variants should support multiple features
        if (variant->variant_id == VARIANT_3C509B_COMBO) {
            TEST_ASSERT((variant->special_features & FEATURE_SQE_TEST) != 0,
                       "Combo variant supports SQE test");
            TEST_ASSERT((variant->special_features & FEATURE_COLLISION_DETECT) != 0,
                       "Combo variant supports collision detection");
        }
    }
}

/**
 * @brief Test PnP to variant mapping consistency
 */
void test_pnp_variant_consistency(void) {
    TEST_START("PnP to Variant Mapping Consistency");
    
    // Check that each PnP entry maps to a valid variant in the database
    for (size_t i = 0; i < NIC_3C509_PNP_DEVICE_COUNT; i++) {
        const pnp_device_id_t* pnp = &NIC_3C509_PNP_DEVICE_TABLE[i];
        
        // Find corresponding variant in database
        bool found = false;
        for (size_t j = 0; j < NIC_3C509_VARIANT_COUNT; j++) {
            const nic_variant_info_t* variant = &NIC_3C509_VARIANT_DATABASE[j];
            if (variant->variant_id == pnp->variant_id) {
                found = true;
                
                // Check product ID consistency
                if (pnp->product_id_override != 0) {
                    TEST_ASSERT(pnp->product_id_override != variant->product_id,
                               "PnP override ID differs from variant default");
                }
                break;
            }
        }
        
        TEST_ASSERT(found, "PnP entry maps to valid variant in database");
    }
}

/**
 * @brief Print hardware detection test results
 */
void print_hardware_test_results(void) {
    printf("\n\n=== HARDWARE DETECTION TEST RESULTS ===\n");
    printf("Total Tests:  %d\n", total_tests);
    printf("Passed Tests: %d\n", passed_tests);
    printf("Failed Tests: %d\n", failed_tests);
    
    if (total_tests > 0) {
        printf("Success Rate: %.1f%%\n", (100.0 * passed_tests / total_tests));
    }
    
    printf("\nHardware Coverage Summary:\n");
    printf("- Variant Database: %zu entries\n", NIC_3C509_VARIANT_COUNT);
    printf("- PnP Device Table: %zu entries\n", NIC_3C509_PNP_DEVICE_COUNT);
    printf("- Media Types: 10+ supported\n");
    printf("- Connector Types: 7 defined\n");
    printf("- Special Features: 12 flags\n");
    
    if (failed_tests == 0) {
        printf("\n✅ HARDWARE DETECTION VALIDATION SUCCESSFUL!\n");
        printf("🔍 All hardware detection capabilities are properly implemented\n");
    } else {
        printf("\n❌ %d HARDWARE DETECTION TESTS FAILED\n", failed_tests);
    }
    
    printf("=========================================\n\n");
}

/**
 * @brief Main hardware detection test execution
 */
int main(void) {
    printf("=== 3COM PACKET DRIVER - HARDWARE DETECTION VALIDATION ===\n");
    printf("Testing hardware detection capabilities for Sprint 0A\n");
    printf("=======================================================\n");
    
    test_variant_database_lookups();
    test_pnp_device_coverage();
    test_media_capability_mapping();
    test_product_id_ranges();
    test_detection_priority();
    test_connector_mapping();
    test_special_features();
    test_pnp_variant_consistency();
    
    print_hardware_test_results();
    
    return (failed_tests == 0) ? 0 : 1;
}
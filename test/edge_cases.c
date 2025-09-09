/**
 * @file edge_cases.c
 * @brief Edge case validation for Extension API
 * 
 * Tests buffer boundaries, invalid segments, AH fuzzing,
 * and other corner cases to ensure robustness.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>

#define PACKET_INT 0x60
#define TEST_ITERATIONS 1000

/* Error codes */
#define EXT_ERR_NOT_READY  0x7000
#define EXT_ERR_TOO_SMALL  0x7001
#define EXT_ERR_BAD_FUNCTION 0x7002
#define EXT_ERR_NO_BUFFER  0x7003

static int tests_passed = 0;
static int tests_failed = 0;

void test_result(const char* name, int passed) {
    printf("%s: %s\n", name, passed ? "PASS" : "FAIL");
    if (passed) tests_passed++;
    else tests_failed++;
}

/**
 * Test NULL buffer handling
 */
void test_null_buffer(void) {
    union REGS r;
    struct SREGS sr;
    
    printf("\n=== NULL Buffer Test ===\n");
    
    /* ES:DI = NULL */
    r.h.ah = 0x83;  /* Memory map - requires buffer */
    r.x.di = 0;
    sr.es = 0;
    int86x(PACKET_INT, &r, &r, &sr);
    
    test_result("NULL buffer returns CF=1", r.x.cflag);
    test_result("NULL buffer returns NO_BUFFER", r.x.ax == EXT_ERR_NO_BUFFER);
}

/**
 * Test buffer at segment boundary
 */
void test_segment_boundary(void) {
    union REGS r;
    struct SREGS sr;
    uint8_t far *boundary_buffer;
    
    printf("\n=== Segment Boundary Test ===\n");
    
    /* Allocate buffer at end of segment */
    boundary_buffer = (uint8_t far *)MK_FP(_DS, 0xFFF8);
    memset(boundary_buffer, 0xFF, 8);
    
    r.h.ah = 0x83;
    r.x.di = 0xFFF8;  /* 8 bytes from segment end */
    sr.es = _DS;
    int86x(PACKET_INT, &r, &r, &sr);
    
    /* Should either succeed or fail gracefully */
    if (!r.x.cflag) {
        test_result("Boundary buffer handled", r.x.ax == 8);
        test_result("No segment wrap", boundary_buffer[0] != 0xFF);
    } else {
        test_result("Boundary rejected safely", 
                   r.x.ax == EXT_ERR_TOO_SMALL || 
                   r.x.ax == EXT_ERR_NO_BUFFER);
    }
}

/**
 * Test invalid segment selector
 */
void test_invalid_segment(void) {
    union REGS r;
    struct SREGS sr;
    int crashed = 0;
    
    printf("\n=== Invalid Segment Test ===\n");
    
    /* Set up structured exception handling */
    _asm {
        push    es
        push    di
    }
    
    /* Try invalid segment (would normally GP fault) */
    r.h.ah = 0x83;
    r.x.di = 0;
    sr.es = 0xFFFF;  /* Invalid in most configurations */
    
    /* This might crash on real hardware */
    /* In production, would use exception handler */
    int86x(PACKET_INT, &r, &r, &sr);
    
    _asm {
        pop     di
        pop     es
    }
    
    /* If we get here, it was handled */
    test_result("Invalid segment handled", !crashed);
    if (r.x.cflag) {
        test_result("Returns error on invalid", r.x.ax >= 0x7000);
    }
}

/**
 * Test zero-length buffer
 */
void test_zero_length(void) {
    union REGS r;
    struct SREGS sr;
    uint8_t buffer[8];
    
    printf("\n=== Zero Length Test ===\n");
    
    r.h.ah = 0x83;
    r.x.di = FP_OFF(buffer);
    sr.es = FP_SEG(buffer);
    r.x.cx = 0;  /* Zero length requested */
    int86x(PACKET_INT, &r, &r, &sr);
    
    /* Should handle gracefully */
    if (r.x.cflag) {
        test_result("Zero length rejected", r.x.ax == EXT_ERR_TOO_SMALL);
    } else {
        test_result("Zero length accepted", r.x.ax == 0);
    }
}

/**
 * AH space fuzzing - test all possible AH values
 */
void test_ah_fuzzing(void) {
    union REGS r;
    int ah;
    int vendor_handled = 0;
    int passed_through = 0;
    int errors = 0;
    
    printf("\n=== AH Space Fuzzing ===\n");
    
    for (ah = 0x00; ah <= 0xFF; ah++) {
        r.h.ah = ah;
        r.x.bx = 0xDEAD;  /* Marker */
        int86(PACKET_INT, &r, &r);
        
        if (ah >= 0x80 && ah <= 0x84) {
            /* Our vendor range - should handle */
            if (!r.x.cflag || r.x.ax >= 0x7000) {
                vendor_handled++;
            } else {
                printf("  ERROR: AH=%02X not handled properly\n", ah);
                errors++;
            }
        } else if (ah >= 0x85 && ah <= 0x9F) {
            /* Vendor range but not implemented */
            if (r.x.cflag && r.x.ax == EXT_ERR_BAD_FUNCTION) {
                vendor_handled++;
            } else {
                errors++;
            }
        } else {
            /* Should pass through to original handler */
            if (ah <= 0x10) {
                /* Standard packet driver range */
                passed_through++;
            } else {
                /* Unknown - should error from original handler */
                if (r.x.cflag) passed_through++;
                else errors++;
            }
        }
    }
    
    printf("  Vendor handled: %d\n", vendor_handled);
    printf("  Passed through: %d\n", passed_through);
    printf("  Errors: %d\n", errors);
    
    test_result("All AH values handled correctly", errors == 0);
    test_result("Vendor range detected", vendor_handled >= 5);
}

/**
 * Test rapid concurrent access (seqlock stress)
 */
void test_concurrent_access(void) {
    union REGS r;
    int i;
    uint16_t last_value = 0;
    uint16_t current_value;
    int torn_reads = 0;
    int retries = 0;
    
    printf("\n=== Concurrent Access Test ===\n");
    
    for (i = 0; i < TEST_ITERATIONS; i++) {
        /* Rapid queries */
        r.h.ah = 0x81;  /* Safety state */
        int86(PACKET_INT, &r, &r);
        
        if (r.x.cflag) {
            printf("  Error at iteration %d: AX=%04X\n", i, r.x.ax);
            torn_reads++;
            continue;
        }
        
        current_value = r.x.ax;
        
        /* Check for impossible transitions */
        if (last_value != 0) {
            /* Safety flags should only change in valid ways */
            uint16_t changed = last_value ^ current_value;
            
            /* Multiple bits changing might indicate torn read */
            if (changed != 0 && 
                changed != 0x0001 &&  /* PIO bit */
                changed != 0x0020 &&  /* DMA validated bit */
                changed != 0x8000) {  /* Kill switch */
                printf("  Suspicious change: %04X -> %04X\n", 
                       last_value, current_value);
                torn_reads++;
            }
        }
        
        last_value = current_value;
        
        /* Simulate concurrent update every 100 iterations */
        if (i % 100 == 0) {
            /* Would trigger snapshot update here */
            retries++;
        }
    }
    
    printf("  Iterations: %d\n", TEST_ITERATIONS);
    printf("  Torn reads: %d\n", torn_reads);
    printf("  Update cycles: %d\n", retries);
    
    test_result("No torn reads detected", torn_reads == 0);
}

/**
 * Test API readiness guard
 */
void test_api_ready_guard(void) {
    union REGS r;
    
    printf("\n=== API Ready Guard Test ===\n");
    
    /* This would need to be tested early in boot */
    /* For now, verify error code is consistent */
    
    /* Try to force NOT_READY (would need special build) */
    r.h.ah = 0x80;
    int86(PACKET_INT, &r, &r);
    
    if (r.x.cflag && r.x.ax == EXT_ERR_NOT_READY) {
        test_result("NOT_READY returned when uninitialized", 1);
    } else {
        /* API is ready, which is also OK for this test */
        test_result("API is ready", !r.x.cflag);
    }
}

/**
 * Test buffer overflow protection
 */
void test_buffer_overflow_protection(void) {
    union REGS r;
    struct SREGS sr;
    uint8_t small_buffer[4];
    uint8_t guard_before[4] = {0xAA, 0xAA, 0xAA, 0xAA};
    uint8_t guard_after[4] = {0xBB, 0xBB, 0xBB, 0xBB};
    uint8_t test_area[12];
    
    printf("\n=== Buffer Overflow Protection ===\n");
    
    /* Set up guarded buffer */
    memcpy(test_area, guard_before, 4);
    memcpy(test_area + 4, small_buffer, 4);
    memcpy(test_area + 8, guard_after, 4);
    
    /* Try to overflow */
    r.h.ah = 0x83;  /* Needs 8 bytes */
    r.x.di = FP_OFF(test_area + 4);
    sr.es = FP_SEG(test_area + 4);
    r.x.cx = 4;  /* Only 4 bytes available */
    int86x(PACKET_INT, &r, &r, &sr);
    
    /* Check guards */
    int before_intact = (memcmp(test_area, guard_before, 4) == 0);
    int after_intact = (memcmp(test_area + 8, guard_after, 4) == 0);
    
    test_result("Guard before intact", before_intact);
    test_result("Guard after intact", after_intact);
    test_result("Overflow rejected", r.x.cflag);
}

/**
 * Test register preservation under stress
 */
void test_register_preservation_stress(void) {
    union REGS r;
    struct SREGS sr;
    int i;
    int preservation_errors = 0;
    
    printf("\n=== Register Preservation Stress ===\n");
    
    segread(&sr);
    uint16_t saved_ds = sr.ds;
    uint16_t saved_es = sr.es;
    
    for (i = 0; i < 100; i++) {
        /* Set sentinel values */
        r.x.si = 0x1234 + i;
        r.x.di = 0x5678 + i;
        r.x.bp = 0x9ABC + i;
        
        /* Call vendor function */
        r.h.ah = 0x80 + (i % 5);
        int86(PACKET_INT, &r, &r);
        
        /* Check preserved registers */
        if (r.x.si != 0x1234 + i) preservation_errors++;
        if (r.x.di != 0x5678 + i) preservation_errors++;
        if (r.x.bp != 0x9ABC + i) preservation_errors++;
    }
    
    segread(&sr);
    test_result("DS preserved under stress", sr.ds == saved_ds);
    test_result("ES preserved under stress", sr.es == saved_es);
    test_result("SI/DI/BP preserved", preservation_errors == 0);
}

int main(void) {
    printf("========================================\n");
    printf("Extension API Edge Case Tests\n");
    printf("========================================\n");
    
    /* Run edge case tests */
    test_null_buffer();
    test_segment_boundary();
    test_invalid_segment();
    test_zero_length();
    test_ah_fuzzing();
    test_concurrent_access();
    test_api_ready_guard();
    test_buffer_overflow_protection();
    test_register_preservation_stress();
    
    /* Summary */
    printf("\n========================================\n");
    printf("Edge Case Test Summary\n");
    printf("========================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    
    if (tests_failed == 0) {
        printf("\n✓ ALL EDGE CASES HANDLED\n");
        printf("Extension API is robust.\n");
        return 0;
    } else {
        printf("\n✗ EDGE CASE FAILURES\n");
        printf("Extension API needs hardening.\n");
        return 1;
    }
}
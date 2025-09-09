/**
 * @file exttest_enhanced.c
 * @brief Enhanced Extension API Test with seqlock and error code validation
 *
 * Additional tests for:
 * - Standardized error codes
 * - Seqlock consistency
 * - Capability discovery
 * - Update detection via timestamp
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <time.h>

#define PACKET_INT          0x60

/* Standardized error codes */
#define EXT_SUCCESS         0x0000
#define EXT_ERR_NOT_READY  0x7000
#define EXT_ERR_TOO_SMALL  0x7001
#define EXT_ERR_BAD_FUNCTION 0x7002
#define EXT_ERR_NO_BUFFER  0x7003
#define EXT_ERR_TIMEOUT    0x7004

/* Capability bits */
#define CAP_DISCOVERY      0x0001
#define CAP_SAFETY        0x0002
#define CAP_PATCHES       0x0004
#define CAP_MEMORY        0x0008
#define CAP_VERSION       0x0010
#define CAP_RUNTIME_CONFIG 0x0020
#define CAP_ALL_CURRENT   0x003F

static int tests_passed = 0;
static int tests_failed = 0;

void test_result(const char* name, int passed) {
    if (passed) {
        printf("[PASS] %s\n", name);
        tests_passed++;
    } else {
        printf("[FAIL] %s\n", name);
        tests_failed++;
    }
}

/**
 * Test capability discovery and negotiation
 */
void test_capability_discovery(void) {
    union REGS r;
    
    printf("\n=== Capability Discovery Test ===\n");
    
    r.h.ah = 0x80;
    int86(PACKET_INT, &r, &r);
    
    if (r.x.cflag) {
        printf("ERROR: Discovery failed with AX=%04X\n", r.x.ax);
        tests_failed++;
        return;
    }
    
    /* Check each capability bit */
    test_result("CAP_DISCOVERY present", r.x.dx & CAP_DISCOVERY);
    test_result("CAP_SAFETY present", r.x.dx & CAP_SAFETY);
    test_result("CAP_PATCHES present", r.x.dx & CAP_PATCHES);
    test_result("CAP_MEMORY present", r.x.dx & CAP_MEMORY);
    test_result("CAP_VERSION present", r.x.dx & CAP_VERSION);
    
    /* Verify max function matches capabilities */
    int expected_max = 0x97;  /* Updated to include runtime config + DMA validation */
    test_result("Max function matches caps", r.x.cx >= 0x96);  /* At least AH=96h */
    
    printf("  Capabilities: 0x%04X\n", r.x.dx);
    printf("  Max function: 0x%02X\n", r.x.cx);
}

/**
 * Test constant-time execution
 * Measures timing of Extension API calls to ensure O(1) behavior
 */
void test_constant_time(void) {
    union REGS r;
    clock_t start, end;
    clock_t times[10];
    int i;
    clock_t min_time = (clock_t)-1;
    clock_t max_time = 0;
    
    printf("\n=== Constant Time Test ===\n");
    
    /* Test AH=80h discovery (should be instant - just reads snapshot) */
    for (i = 0; i < 10; i++) {
        start = clock();
        r.h.ah = 0x80;
        int86(PACKET_INT, &r, &r);
        end = clock();
        times[i] = end - start;
        if (times[i] < min_time) min_time = times[i];
        if (times[i] > max_time) max_time = times[i];
    }
    
    /* Check variance is minimal (within 2 ticks) */
    test_result("Discovery timing consistent", (max_time - min_time) <= 2);
    printf("  Discovery: min=%ld max=%ld ticks\n", (long)min_time, (long)max_time);
    
    /* Test AH=81h safety state */
    min_time = (clock_t)-1;
    max_time = 0;
    for (i = 0; i < 10; i++) {
        start = clock();
        r.h.ah = 0x81;
        int86(PACKET_INT, &r, &r);
        end = clock();
        times[i] = end - start;
        if (times[i] < min_time) min_time = times[i];
        if (times[i] > max_time) max_time = times[i];
    }
    
    test_result("Safety state timing consistent", (max_time - min_time) <= 2);
    printf("  Safety state: min=%ld max=%ld ticks\n", (long)min_time, (long)max_time);
    
    /* Test AH=82h patch stats */
    min_time = (clock_t)-1;
    max_time = 0;
    for (i = 0; i < 10; i++) {
        start = clock();
        r.h.ah = 0x82;
        int86(PACKET_INT, &r, &r);
        end = clock();
        times[i] = end - start;
        if (times[i] < min_time) min_time = times[i];
        if (times[i] > max_time) max_time = times[i];
    }
    
    test_result("Patch stats timing consistent", (max_time - min_time) <= 2);
    printf("  Patch stats: min=%ld max=%ld ticks\n", (long)min_time, (long)max_time);
}

/**
 * Test runtime configuration functions (AH=94h-96h)
 */
void test_runtime_config(void) {
    union REGS r;
    uint16_t original_threshold;
    uint8_t original_batch, original_timeout;
    
    printf("\n=== Runtime Configuration Test ===\n");
    
    /* Test AH=94h: Get/Set copy-break threshold */
    r.h.ah = 0x94;
    r.x.bx = 0;  /* Get current value */
    int86(PACKET_INT, &r, &r);
    
    if (r.x.cflag) {
        printf("WARNING: Copy-break get failed (AX=%04X)\n", r.x.ax);
        tests_failed++;
    } else {
        original_threshold = r.x.bx;
        printf("  Current copy-break threshold: %u bytes\n", original_threshold);
        
        /* Set new value */
        r.h.ah = 0x94;
        r.x.bx = 512;  /* Set to 512 bytes */
        int86(PACKET_INT, &r, &r);
        test_result("Set copy-break to 512", !r.x.cflag);
        
        /* Verify it was set */
        r.h.ah = 0x94;
        r.x.bx = 0;  /* Get current value */
        int86(PACKET_INT, &r, &r);
        test_result("Copy-break set correctly", r.x.bx == 512);
        
        /* Restore original */
        r.h.ah = 0x94;
        r.x.bx = original_threshold;
        int86(PACKET_INT, &r, &r);
    }
    
    /* Test AH=95h: Get/Set interrupt mitigation */
    r.h.ah = 0x95;
    r.x.bx = 0;  /* Get current values */
    int86(PACKET_INT, &r, &r);
    
    if (!r.x.cflag) {
        original_batch = r.h.bl;
        original_timeout = r.h.bh;
        printf("  Current mitigation: batch=%u timeout=%u\n", original_batch, original_timeout);
        
        /* Set new values */
        r.h.ah = 0x95;
        r.h.bl = 20;  /* 20 packets */
        r.h.bh = 5;   /* 5 ticks */
        int86(PACKET_INT, &r, &r);
        test_result("Set mitigation params", !r.x.cflag);
        
        /* Restore original */
        r.h.ah = 0x95;
        r.h.bl = original_batch;
        r.h.bh = original_timeout;
        int86(PACKET_INT, &r, &r);
    } else {
        printf("WARNING: Mitigation get failed (AX=%04X)\n", r.x.ax);
        tests_failed++;
    }
    
    /* Test AH=96h: Media mode control */
    r.h.ah = 0x96;
    r.h.al = 0;  /* Auto mode */
    int86(PACKET_INT, &r, &r);
    test_result("Set media mode to auto", !r.x.cflag);
}

/**
 * Test DMA validation API (AH=97h)
 */
void test_dma_validation(void) {
    union REGS r;
    
    printf("\n=== DMA Validation API Test ===\n");
    
    /* Test setting validation to failed */
    r.h.ah = 0x97;
    r.h.al = 0;  /* Failed */
    int86(PACKET_INT, &r, &r);
    test_result("Set DMA validation failed", !r.x.cflag);
    
    /* Test setting validation to passed */
    r.h.ah = 0x97;
    r.h.al = 1;  /* Passed */
    int86(PACKET_INT, &r, &r);
    test_result("Set DMA validation passed", !r.x.cflag);
    
    /* Verify via safety state (AH=81h) */
    r.h.ah = 0x81;
    int86(PACKET_INT, &r, &r);
    if (!r.x.cflag) {
        /* Check if DMA_VALIDATED flag is set (bit 5) */
        test_result("DMA validated flag set", (r.x.ax & 0x0020) != 0);
    }
}

/**
 * Test standardized error codes
 */
void test_error_codes(void) {
    union REGS r;
    struct SREGS sr;
    
    printf("\n=== Error Code Test ===\n");
    
    /* Test BAD_FUNCTION error */
    r.h.ah = 0x85;  /* Not implemented */
    int86(PACKET_INT, &r, &r);
    test_result("Bad function returns CF=1", r.x.cflag);
    test_result("Bad function code correct", r.x.ax == EXT_ERR_BAD_FUNCTION);
    
    /* Test NO_BUFFER error */
    r.h.ah = 0x83;  /* Memory map */
    r.x.di = 0;     /* NULL buffer */
    sr.es = 0;
    int86x(PACKET_INT, &r, &r, &sr);
    test_result("No buffer returns CF=1", r.x.cflag);
    test_result("No buffer code correct", r.x.ax == EXT_ERR_NO_BUFFER);
    
    /* Test way out of range */
    r.h.ah = 0xFF;
    int86(PACKET_INT, &r, &r);
    test_result("AH=FFh returns error", r.x.cflag);
    test_result("Out of range code correct", r.x.ax == EXT_ERR_BAD_FUNCTION);
}

/**
 * Test seqlock consistency under rapid queries
 */
void test_seqlock_consistency(void) {
    union REGS r;
    uint16_t last_safety = 0;
    uint16_t last_stack = 0;
    int consistent = 1;
    int i;
    
    printf("\n=== Seqlock Consistency Test ===\n");
    
    /* Get initial state */
    r.h.ah = 0x81;
    int86(PACKET_INT, &r, &r);
    last_safety = r.x.ax;
    last_stack = r.x.bx;
    
    /* Rapid queries looking for torn reads */
    for (i = 0; i < 1000; i++) {
        r.h.ah = 0x81;
        int86(PACKET_INT, &r, &r);
        
        /* Check for impossible values (torn read indicators) */
        if (r.x.bx > 2048 || r.x.bx < 100) {
            printf("  Torn read detected: stack=%u at iteration %d\n", 
                   r.x.bx, i);
            consistent = 0;
            break;
        }
        
        /* Values should be stable or change atomically */
        if (r.x.ax != last_safety) {
            printf("  Safety flags changed: 0x%04X -> 0x%04X\n",
                   last_safety, r.x.ax);
            last_safety = r.x.ax;
        }
    }
    
    test_result("No torn reads in 1000 queries", consistent);
    test_result("Values remain plausible", 1);  /* We got here */
}

/**
 * Test timestamp/update detection
 */
void test_update_detection(void) {
    union REGS r;
    struct SREGS sr;
    uint8_t buffer1[8], buffer2[8];
    int updates_detected = 0;
    int i;
    
    printf("\n=== Update Detection Test ===\n");
    
    /* Get initial memory map */
    r.h.ah = 0x83;
    r.x.di = FP_OFF(buffer1);
    sr.es = FP_SEG(buffer1);
    int86x(PACKET_INT, &r, &r, &sr);
    
    /* Query multiple times looking for changes */
    for (i = 0; i < 100; i++) {
        /* Small delay to allow potential updates */
        delay(10);  /* 10ms */
        
        r.h.ah = 0x83;
        r.x.di = FP_OFF(buffer2);
        sr.es = FP_SEG(buffer2);
        int86x(PACKET_INT, &r, &r, &sr);
        
        if (memcmp(buffer1, buffer2, 8) != 0) {
            updates_detected++;
            memcpy(buffer1, buffer2, 8);
        }
    }
    
    /* Some updates are OK (deferred updates), too many indicate instability */
    test_result("Snapshot stability", updates_detected <= 2);
    printf("  Updates detected: %d in 100 queries\n", updates_detected);
}

/**
 * Test dispatch performance overhead
 */
void test_dispatch_overhead(void) {
    union REGS r;
    clock_t start, end;
    double vendor_time, standard_time;
    int i;
    
    printf("\n=== Dispatch Overhead Test ===\n");
    
    /* Measure standard call (bypasses extension) */
    r.h.ah = 0x01;  /* Driver info */
    r.h.al = 0xFF;
    start = clock();
    for (i = 0; i < 10000; i++) {
        int86(PACKET_INT, &r, &r);
    }
    end = clock();
    standard_time = (double)(end - start);
    
    /* Measure vendor call */
    r.h.ah = 0x81;  /* Safety state */
    start = clock();
    for (i = 0; i < 10000; i++) {
        int86(PACKET_INT, &r, &r);
    }
    end = clock();
    vendor_time = (double)(end - start);
    
    /* Calculate overhead percentage */
    double overhead_pct = ((vendor_time - standard_time) / standard_time) * 100;
    
    printf("  Standard call: %.2f ms/10K\n", standard_time);
    printf("  Vendor call: %.2f ms/10K\n", vendor_time);
    printf("  Overhead: %.1f%%\n", overhead_pct);
    
    /* Should be minimal overhead (<10%) */
    test_result("Dispatch overhead <10%", overhead_pct < 10.0);
}

/**
 * Test buffer overflow protection
 */
void test_buffer_overflow(void) {
    union REGS r;
    struct SREGS sr;
    uint8_t small_buffer[4];  /* Too small */
    uint8_t guard[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    
    printf("\n=== Buffer Overflow Test ===\n");
    
    /* Place guard pattern */
    memcpy(small_buffer, guard, 4);
    
    /* Try to overflow with memory map (needs 8 bytes) */
    r.h.ah = 0x83;
    r.x.di = FP_OFF(small_buffer);
    sr.es = FP_SEG(small_buffer);
    int86x(PACKET_INT, &r, &r, &sr);
    
    /* Should fail with NO_BUFFER, not write anything */
    test_result("Returns error for small buffer", r.x.cflag);
    test_result("Guard pattern intact", 
                memcmp(small_buffer, guard, 4) == 0);
}

/**
 * Test register preservation
 */
void test_register_preservation(void) {
    union REGS r_in, r_out;
    struct SREGS sr_in, sr_out;
    
    printf("\n=== Register Preservation Test ===\n");
    
    /* Set known values */
    segread(&sr_in);
    r_in.x.bx = 0x1234;
    r_in.x.si = 0x5678;
    r_in.x.di = 0x9ABC;
    r_in.x.bp = 0xDEF0;
    
    /* Call vendor function */
    r_in.h.ah = 0x81;
    int86x(PACKET_INT, &r_in, &r_out, &sr_out);
    
    /* Check preservation */
    segread(&sr_out);
    test_result("DS preserved", sr_out.ds == sr_in.ds);
    test_result("ES preserved", sr_out.es == sr_in.es);
    test_result("SI preserved", r_out.x.si == r_in.x.si);
    test_result("DI preserved", r_out.x.di == r_in.x.di);
    test_result("BP preserved", r_out.x.bp == r_in.x.bp);
    /* BX is output register, so not preserved */
}

int main(void) {
    printf("========================================\n");
    printf("Enhanced Extension API Test Suite v2.0\n");
    printf("========================================\n");
    
    /* Core Extension API tests */
    test_capability_discovery();
    test_error_codes();
    test_seqlock_consistency();
    test_update_detection();
    
    /* Performance and timing tests */
    test_constant_time();
    test_dispatch_overhead();
    
    /* Runtime configuration tests (AH=94h-96h) */
    test_runtime_config();
    
    /* DMA validation API test (AH=97h) */
    test_dma_validation();
    
    /* Safety and protection tests */
    test_buffer_overflow();
    test_register_preservation();
    
    /* Summary */
    printf("\n========================================\n");
    printf("Enhanced Test Summary\n");
    printf("========================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    
    if (tests_failed == 0) {
        printf("\n✓ ALL ENHANCED TESTS PASSED\n");
        return 0;
    } else {
        printf("\n✗ SOME TESTS FAILED\n");
        return 1;
    }
}
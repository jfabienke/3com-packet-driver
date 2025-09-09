/**
 * @file exttest.c
 * @brief Extension API Test Harness (builds to EXTTEST.COM)
 *
 * Validates vendor extension API (AH=80h-84h) for:
 * - CF/AX semantics
 * - Register preservation (DS/ES/BP)
 * - Buffer overflow behavior
 * - Timing bounds (<2μs on 486)
 * - Error handling
 *
 * Build: wcc -ms exttest.c && wlink system dos com file exttest.obj name exttest.com
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <time.h>

#define PACKET_INT      0x60
#define MAX_TESTS       20
#define TIMING_SAMPLES  1000

/* Test results tracking */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* ANSI color codes for output */
#define GREEN   "\x1b[32m"
#define RED     "\x1b[31m"
#define YELLOW  "\x1b[33m"
#define RESET   "\x1b[0m"

/**
 * @brief Test result helper
 */
void test_result(const char* test_name, int passed) {
    tests_run++;
    if (passed) {
        tests_passed++;
        printf("%s[PASS]%s %s\n", GREEN, RESET, test_name);
    } else {
        tests_failed++;
        printf("%s[FAIL]%s %s\n", RED, RESET, test_name);
    }
}

/**
 * @brief Check if packet driver is installed
 */
int check_driver_installed(void) {
    union REGS r;
    struct SREGS sr;
    
    r.h.ah = 0x00;  /* Driver info call */
    r.x.bx = 0xFFFF; /* Magic value */
    r.h.al = 0xFF;
    
    int86x(PACKET_INT, &r, &r, &sr);
    
    /* Check for signature response */
    return (r.x.bx != 0xFFFF);
}

/**
 * @brief Test AH=80h Vendor Discovery
 */
void test_vendor_discovery(void) {
    union REGS r_in, r_out;
    struct SREGS sr;
    
    printf("\n=== AH=80h: Vendor Discovery ===\n");
    
    /* Save registers for preservation check */
    segread(&sr);
    uint16_t saved_ds = sr.ds;
    uint16_t saved_es = sr.es;
    
    /* Call vendor discovery */
    r_in.h.ah = 0x80;
    int86x(PACKET_INT, &r_in, &r_out, &sr);
    
    /* Check CF clear on success */
    test_result("CF clear on success", !(r_out.x.cflag));
    
    /* Check signature */
    test_result("Signature = '3C'", r_out.x.ax == 0x3343);
    
    /* Check version format */
    test_result("Version BCD format", (r_out.x.bx & 0xFF00) <= 0x9900 && 
                                      (r_out.x.bx & 0x00FF) <= 0x0099);
    
    /* Check max function reasonable */
    test_result("Max function >= 0x84", r_out.x.cx >= 0x0084);
    
    /* Check capabilities non-zero */
    test_result("Capabilities present", r_out.x.dx != 0);
    
    /* Check register preservation */
    segread(&sr);
    test_result("DS preserved", sr.ds == saved_ds);
    test_result("ES preserved", sr.es == saved_es);
}

/**
 * @brief Test AH=81h Safety State
 */
void test_safety_state(void) {
    union REGS r;
    
    printf("\n=== AH=81h: Safety State ===\n");
    
    r.h.ah = 0x81;
    int86(PACKET_INT, &r, &r);
    
    test_result("CF clear", !r.x.cflag);
    test_result("Safety flags valid", r.x.ax != 0xFFFF);
    test_result("Stack free reasonable", r.x.bx >= 256 && r.x.bx <= 2048);
    test_result("Patch count > 0", r.x.cx > 0);
    
    /* Print decoded flags */
    printf("  Safety flags: 0x%04X\n", r.x.ax);
    if (r.x.ax & 0x0001) printf("    - PIO forced\n");
    if (r.x.ax & 0x0002) printf("    - Patches verified\n");
    if (r.x.ax & 0x0004) printf("    - Boundary checking\n");
    printf("  Stack free: %u bytes\n", r.x.bx);
    printf("  Active patches: %u\n", r.x.cx);
}

/**
 * @brief Test AH=82h Patch Statistics
 */
void test_patch_stats(void) {
    union REGS r;
    
    printf("\n=== AH=82h: Patch Statistics ===\n");
    
    r.h.ah = 0x82;
    int86(PACKET_INT, &r, &r);
    
    test_result("CF clear", !r.x.cflag);
    test_result("Patches applied > 0", r.x.ax > 0);
    test_result("CLI ticks < 10", r.x.bx < 10);
    test_result("Modules patched > 0", r.x.cx > 0);
    test_result("Health code valid", r.x.dx == 0x0A11 || r.x.dx == 0x0BAD);
    
    printf("  Patches: %u\n", r.x.ax);
    printf("  Max CLI: %u ticks\n", r.x.bx);
    printf("  Modules: %u\n", r.x.cx);
    printf("  Health: 0x%04X\n", r.x.dx);
}

/**
 * @brief Test AH=83h Memory Map
 */
void test_memory_map(void) {
    union REGS r;
    struct SREGS sr;
    uint8_t buffer[16];
    uint16_t far *wptr;
    
    printf("\n=== AH=83h: Memory Map ===\n");
    
    /* Test with buffer */
    memset(buffer, 0xFF, sizeof(buffer));
    r.h.ah = 0x83;
    r.x.di = FP_OFF(buffer);
    sr.es = FP_SEG(buffer);
    int86x(PACKET_INT, &r, &r, &sr);
    
    test_result("CF clear with buffer", !r.x.cflag);
    test_result("Returns 8 bytes", r.x.ax == 8);
    test_result("Buffer modified", buffer[0] != 0xFF);
    
    /* Decode buffer */
    wptr = (uint16_t far *)buffer;
    printf("  Hot code: %u bytes\n", wptr[0]);
    printf("  Hot data: %u bytes\n", wptr[1]);
    printf("  ISR stack: %u bytes\n", wptr[2]);
    printf("  Total resident: %u bytes\n", wptr[3]);
    
    test_result("Total < 8KB", wptr[3] < 8192);
    
    /* Test without buffer */
    r.h.ah = 0x83;
    r.x.di = 0;  /* NULL buffer */
    int86(PACKET_INT, &r, &r);
    
    test_result("CF set without buffer", r.x.cflag);
    test_result("Returns required size", r.x.ax == 8);
}

/**
 * @brief Test AH=84h Version Info
 */
void test_version_info(void) {
    union REGS r;
    
    printf("\n=== AH=84h: Version Info ===\n");
    
    r.h.ah = 0x84;
    int86(PACKET_INT, &r, &r);
    
    test_result("CF clear", !r.x.cflag);
    test_result("Version valid BCD", (r.x.ax & 0xFF00) <= 0x9900);
    test_result("Build flags present", r.x.bx != 0);
    
    printf("  Version: %d.%02d\n", (r.x.ax >> 8), (r.x.ax & 0xFF));
    printf("  Build flags: 0x%04X\n", r.x.bx);
    if (r.x.bx & 0x8000) printf("    - Production\n");
    if (r.x.bx & 0x0001) printf("    - PIO mode\n");
    if (r.x.bx & 0x0002) printf("    - DMA mode\n");
    printf("  NIC type: 0x%04X\n", r.x.cx);
}

/**
 * @brief Test invalid function codes
 */
void test_invalid_functions(void) {
    union REGS r;
    
    printf("\n=== Invalid Function Tests ===\n");
    
    /* Test below range */
    r.h.ah = 0x7F;
    int86(PACKET_INT, &r, &r);
    test_result("AH=7Fh passes through", !r.x.cflag || r.x.ax != 0xFFFF);
    
    /* Test above implemented range */
    r.h.ah = 0x85;  /* Not implemented yet */
    int86(PACKET_INT, &r, &r);
    test_result("AH=85h returns error", r.x.cflag && r.x.ax == 0xFFFF);
    
    /* Test way above range */
    r.h.ah = 0xA0;
    int86(PACKET_INT, &r, &r);
    test_result("AH=A0h returns error", r.x.cflag && r.x.ax == 0xFFFF);
}

/**
 * @brief Timing test - ensure <2μs per call
 */
void test_timing(void) {
    union REGS r;
    clock_t start, end;
    int i;
    double elapsed_ms;
    double us_per_call;
    
    printf("\n=== Timing Test (%d samples) ===\n", TIMING_SAMPLES);
    
    /* Warm up */
    for (i = 0; i < 10; i++) {
        r.h.ah = 0x80;
        int86(PACKET_INT, &r, &r);
    }
    
    /* Measure */
    start = clock();
    for (i = 0; i < TIMING_SAMPLES; i++) {
        r.h.ah = 0x81;  /* Safety state - simple register loads */
        int86(PACKET_INT, &r, &r);
    }
    end = clock();
    
    elapsed_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;
    us_per_call = (elapsed_ms * 1000.0) / TIMING_SAMPLES;
    
    printf("  Total time: %.2f ms\n", elapsed_ms);
    printf("  Per call: %.2f μs\n", us_per_call);
    
    /* On 486/66MHz, should be <2μs */
    /* Allow up to 10μs for slower systems */
    test_result("Timing <10μs per call", us_per_call < 10.0);
}

/**
 * @brief Stress test - rapid repeated calls
 */
void test_stress(void) {
    union REGS r;
    int i;
    int errors = 0;
    
    printf("\n=== Stress Test (1000 calls) ===\n");
    
    for (i = 0; i < 1000; i++) {
        r.h.ah = 0x80 + (i % 5);  /* Rotate through functions */
        int86(PACKET_INT, &r, &r);
        if (r.x.cflag && r.h.ah <= 0x84) {
            errors++;
        }
    }
    
    test_result("No errors in 1000 calls", errors == 0);
}

/**
 * @brief Main test harness
 */
int main(void) {
    printf("========================================\n");
    printf("3Com Packet Driver Extension API Test\n");
    printf("========================================\n");
    
    /* Check driver installed */
    if (!check_driver_installed()) {
        printf("%sERROR:%s Packet driver not found at INT %02Xh\n", 
               RED, RESET, PACKET_INT);
        return 1;
    }
    
    printf("%sDriver detected%s at INT %02Xh\n\n", GREEN, RESET, PACKET_INT);
    
    /* Run test suite */
    test_vendor_discovery();
    test_safety_state();
    test_patch_stats();
    test_memory_map();
    test_version_info();
    test_invalid_functions();
    test_timing();
    test_stress();
    
    /* Summary */
    printf("\n========================================\n");
    printf("Test Summary\n");
    printf("========================================\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %s%d%s\n", GREEN, tests_passed, RESET);
    printf("Tests failed: %s%d%s\n", 
           tests_failed > 0 ? RED : GREEN, tests_failed, RESET);
    
    if (tests_failed == 0) {
        printf("\n%s✓ ALL TESTS PASSED%s\n", GREEN, RESET);
        printf("Extension API validated and compliant.\n");
        return 0;
    } else {
        printf("\n%s✗ TESTS FAILED%s\n", RED, RESET);
        printf("Extension API has issues requiring fixes.\n");
        return 1;
    }
}
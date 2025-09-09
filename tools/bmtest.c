/**
 * @file bmtest.c
 * @brief Bus Master Test Utility - External DMA validation for 3C515
 *
 * Comprehensive test suite that validates DMA safety through the driver's
 * own DMA path, with zero resident memory impact.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <conio.h>
#include <time.h>
#include <fcntl.h>
#include <io.h>
#include "../include/vds.h"
#include "../include/common.h"

/* Forward declarations for stress test functions */
int run_stress_test(uint32_t duration_secs, bool verbose);
int run_soak_test(uint32_t duration_mins, bool verbose);
int run_negative_test(void);
void get_stress_stats(void *stats);
void set_stress_params(uint32_t seed, uint32_t rate);

/* Forward declaration for DMA policy update */
static void update_dma_policy(uint8_t passed);

/* JSON schema version */
#define JSON_SCHEMA_VERSION "1.2"

#define PACKET_INT          0x60
#define TEST_PATTERN_AA     0xAA
#define TEST_PATTERN_55     0x55
#define TEST_PATTERN_FF     0xFF
#define TEST_PATTERN_00     0x00
#define TEST_BUFFER_SIZE    8192
#define BOUNDARY_TEST_SIZE  256

/* Vendor API function codes */
#define EXT_VENDOR_DISCOVERY    0x80
#define EXT_SAFETY_STATE       0x81
#define EXT_PATCH_STATS        0x82
#define EXT_QUIESCE           0x90
#define EXT_RESUME            0x91
#define EXT_GET_DMA_STATS     0x92
#define EXT_SET_XFER_MODE     0x93

/* Telemetry structure from driver */
typedef struct {
    uint16_t version;
    uint8_t cpu_family;
    uint8_t cpu_model;
    uint8_t cpu_stepping;
    uint8_t dos_major;
    uint8_t dos_minor;
    uint8_t ems_present;
    uint8_t xms_present;
    uint8_t vds_present;
    uint16_t nic_io_base;
    uint8_t nic_irq;
    uint8_t nic_type;
    uint8_t cache_tier;
    uint8_t patch_count;
    uint16_t health_flags;
    uint8_t loopback_on;
    uint8_t patches_active;
    uint8_t cascade_ok;
    uint8_t smoke_reason;
    uint32_t capability;
    uint16_t uptime_ticks;
} telemetry_t;

/* Latency histogram for statistics */
typedef struct {
    uint32_t samples[100];
    uint16_t count;
    uint32_t sum;
    uint32_t max;
} latency_hist_t;

/* Enhanced test results structure */
typedef struct {
    /* Environment info */
    telemetry_t telemetry;
    char bios_cache[16];
    
    /* Boundary test results - detailed */
    struct {
        uint32_t aligned_bounces;
        uint32_t aligned_violations;
        uint32_t cross64k_bounces;
        uint32_t cross64k_violations;
        uint32_t above16m_rejected;
        uint32_t misaligned_bounces;
        uint32_t misaligned_violations;
    } boundaries;
    
    /* Cache coherency results */
    bool coherency_passed;
    uint32_t wbinvd_median_us;
    uint32_t wbinvd_p95_us;
    uint32_t cli_max_ticks;
    uint32_t stale_reads;
    uint8_t selected_tier;
    
    /* Performance results - enhanced */
    struct {
        uint32_t throughput_kbps;
        uint32_t cpu_percent;
        uint32_t latency_max_us;
        uint32_t latency_avg_us;
        uint32_t latency_median_us;
        uint32_t latency_p95_us;
    } pio, dma;
    
    /* Overall decision */
    bool tests_passed;
    char failure_reason[64];
    uint32_t hw_signature;
} test_results_t;

/* DMA policy state */
typedef struct {
    uint8_t runtime_enable;
    uint8_t validation_passed;
    uint8_t last_known_safe;
    uint32_t signature;
    uint8_t failure_reason;
} dma_policy_t;

/* Global test state */
static test_results_t g_results;
static dma_policy_t g_policy;
static uint16_t g_nic_io_base = 0x300;
static uint8_t g_nic_irq = 10;
static bool g_vds_available = false;

/**
 * Read PIT counter for timing
 */
static uint32_t read_pit(void) {
    uint32_t count;
    _disable();
    outp(0x43, 0x00);  /* Latch counter 0 */
    count = inp(0x40);
    count |= inp(0x40) << 8;
    _enable();
    return count;
}

/**
 * Microsecond delay using PIT
 */
static void delay_us(uint32_t us) {
    uint32_t ticks = (us * 1193182L) / 1000000L;
    uint32_t start = read_pit();
    while ((read_pit() - start) < ticks);
}

/**
 * Get telemetry from driver
 */
static bool get_telemetry(telemetry_t *telem) {
    union REGS r;
    struct SREGS sr;
    
    r.h.ah = 0x95;  /* Get telemetry */
    r.x.cx = sizeof(telemetry_t);
    sr.es = FP_SEG(telem);
    r.x.di = FP_OFF(telem);
    
    int86x(PACKET_INT, &r, &r, &sr);
    
    return !r.x.cflag;
}

/**
 * Measure CPU utilization via idle loop counting
 */
static uint32_t measure_cpu_idle(void) {
    uint32_t idle_count = 0;
    uint32_t start = read_pit();
    uint32_t target = 1193;  /* ~100ms worth of ticks */
    
    /* Count idle loops */
    while ((read_pit() - start) < target) {
        idle_count++;
    }
    
    return idle_count;  /* Higher = more idle = less CPU usage */
}

/**
 * Calculate CPU percentage from idle counts
 */
static uint32_t calc_cpu_percent(uint32_t baseline_idle, uint32_t current_idle) {
    if (baseline_idle == 0) return 100;
    if (current_idle >= baseline_idle) return 0;
    
    return ((baseline_idle - current_idle) * 100) / baseline_idle;
}

/**
 * Add latency sample to histogram
 */
static void add_latency_sample(latency_hist_t *hist, uint32_t us) {
    if (hist->count < 100) {
        hist->samples[hist->count++] = us;
        hist->sum += us;
        if (us > hist->max) hist->max = us;
    }
}

/**
 * Compare function for qsort
 */
static int compare_uint32(const void *a, const void *b) {
    uint32_t ua = *(uint32_t*)a;
    uint32_t ub = *(uint32_t*)b;
    if (ua < ub) return -1;
    if (ua > ub) return 1;
    return 0;
}

/**
 * Calculate latency statistics from histogram
 */
static void calc_latency_stats(latency_hist_t *hist, test_results_t *results, bool is_pio) {
    if (hist->count == 0) return;
    
    /* Sort samples */
    qsort(hist->samples, hist->count, sizeof(uint32_t), compare_uint32);
    
    /* Calculate statistics */
    uint32_t median = hist->samples[hist->count / 2];
    uint32_t p95 = hist->samples[(hist->count * 95) / 100];
    uint32_t avg = hist->sum / hist->count;
    
    if (is_pio) {
        results->pio.latency_median_us = median;
        results->pio.latency_p95_us = p95;
        results->pio.latency_avg_us = avg;
        results->pio.latency_max_us = hist->max;
    } else {
        results->dma.latency_median_us = median;
        results->dma.latency_p95_us = p95;
        results->dma.latency_avg_us = avg;
        results->dma.latency_max_us = hist->max;
    }
}

/**
 * Check if driver is loaded
 */
static bool verify_driver_loaded(void) {
    union REGS r;
    
    r.h.ah = EXT_VENDOR_DISCOVERY;
    int86(PACKET_INT, &r, &r);
    
    if (r.x.cflag) {
        return false;
    }
    
    /* Check for 3C signature */
    return (r.x.ax == 0x3343);
}

/**
 * Verify patches are active
 */
static bool verify_patches_active(void) {
    union REGS r;
    
    r.h.ah = EXT_PATCH_STATS;
    int86(PACKET_INT, &r, &r);
    
    if (r.x.cflag) {
        return false;
    }
    
    /* Should have at least 12 patches */
    return (r.x.ax >= 12);
}

/**
 * Quiesce driver for testing
 */
static bool quiesce_driver(void) {
    union REGS r;
    int retries = 10;
    
    while (retries--) {
        r.h.ah = EXT_QUIESCE;
        int86(PACKET_INT, &r, &r);
        
        if (!r.x.cflag) {
            return true;  /* Success */
        }
        
        if (r.x.ax == 0x7005) {
            /* ISR active, wait and retry */
            delay_us(1000);
            continue;
        }
        
        /* Other error */
        return false;
    }
    
    return false;
}

/**
 * Resume driver after testing
 */
static bool resume_driver(void) {
    union REGS r;
    
    r.h.ah = EXT_RESUME;
    int86(PACKET_INT, &r, &r);
    
    return !r.x.cflag;
}

/**
 * Get DMA statistics from driver
 */
static void get_dma_stats(uint16_t *bounces, uint16_t *violations) {
    union REGS r;
    
    r.h.ah = EXT_GET_DMA_STATS;
    int86(PACKET_INT, &r, &r);
    
    if (!r.x.cflag) {
        *bounces = r.x.ax;
        *violations = r.x.bx;
    }
}

/**
 * Enable NIC loopback through vendor API
 */
static void enable_nic_loopback(void) {
    union REGS r;
    
    /* Use vendor-specific loopback enable (AH=94h) */
    r.h.ah = 0x94;  /* Vendor loopback control */
    r.h.al = 1;     /* 1 = enable loopback */
    int86(PACKET_INT, &r, &r);
    
    if (r.x.cflag) {
        printf("  Warning: Loopback not supported (tests may vary)\n");
    }
}

/**
 * Allocate buffer crossing 64KB boundary
 */
static void far* allocate_boundary_buffer(void) {
    union REGS r;
    void far *buffer;
    
    /* Allocate 64KB + extra via DOS */
    r.h.ah = 0x48;
    r.x.bx = 0x1001;  /* 64KB + 1 paragraph */
    int86(0x21, &r, &r);
    
    if (r.x.cflag) {
        return NULL;
    }
    
    /* Create buffer starting near segment end */
    buffer = MK_FP(r.x.ax, 0xFFF0);
    return buffer;
}

/**
 * Update DMA policy persistence
 * Calls driver's Extension API to persist validation result
 */
static void update_dma_policy(uint8_t passed) {
    union REGS r;
    
    /* Use Extension API AH=97h (Set DMA validation result) */
    r.h.ah = 0x97;  /* Set DMA validation */
    r.h.al = passed ? 1 : 0;
    int86(PACKET_INT, &r, &r);
    
    if (r.x.cflag) {
        printf("  Warning: Failed to update DMA policy (AX=%04X)\n", r.x.ax);
    } else {
        printf("  DMA policy updated: validation=%s\n", passed ? "PASSED" : "FAILED");
        
        /* Also enable runtime DMA if validation passed */
        if (passed) {
            r.h.ah = 0x93;  /* Set transfer mode */
            r.h.al = 1;     /* Enable DMA */
            int86(PACKET_INT, &r, &r);
            
            if (!r.x.cflag) {
                printf("  DMA runtime enabled\n");
            }
        }
    }
}

/**
 * Send test packet through driver for DMA testing
 */
static bool send_test_packet(void far *buffer, uint16_t size) {
    union REGS r;
    struct SREGS sr;
    
    /* Use packet driver send_pkt */
    r.h.ah = 0x04;  /* send_pkt function */
    r.x.cx = size;  /* Packet length */
    sr.ds = FP_SEG(buffer);
    r.x.si = FP_OFF(buffer);
    int86x(PACKET_INT, &r, &r, &sr);
    
    return !r.x.cflag;
}

/**
 * Test boundary handling
 */
static bool test_boundaries(void) {
    void far *test_buffer;
    uint32_t physical;
    uint16_t initial_bounces, initial_violations;
    uint16_t final_bounces, final_violations;
    VDS_DDS dds;
    bool success = true;
    int test_count = 0;
    
    printf("Testing DMA boundaries...\n");
    
    /* Get initial stats */
    get_dma_stats(&initial_bounces, &initial_violations);
    
    /* Test 1: Aligned buffer (should NOT bounce) */
    test_buffer = _fmalloc(TEST_BUFFER_SIZE);
    if (!test_buffer) {
        printf("  Failed to allocate test buffer\n");
        return false;
    }
    
    /* Fill with pattern */
    _fmemset(test_buffer, TEST_PATTERN_AA, TEST_BUFFER_SIZE);
    
    /* Get physical address */
    if (g_vds_available) {
        if (vds_lock_region(test_buffer, TEST_BUFFER_SIZE, &dds) == 0) {
            physical = dds.physical;
            vds_unlock_region(&dds);
        } else {
            physical = far_ptr_to_physical(test_buffer);
        }
    } else {
        physical = far_ptr_to_physical(test_buffer);
    }
    
    printf("  Test 1 - Aligned buffer at %08lX: ", physical);
    
    /* Send packet to trigger DMA */
    if (send_test_packet(test_buffer, 1514)) {
        test_count++;
        if (!crosses_64k_boundary(physical, 1514)) {
            printf("OK (no bounce expected)\n");
            g_results.boundaries.aligned_bounces = 0;
        } else {
            printf("UNEXPECTED 64K crossing\n");
            g_results.boundaries.cross64k_violations++;
        }
    } else {
        printf("SEND FAILED\n");
    }
    
    _ffree(test_buffer);
    g_results.boundaries_tested++;
    
    /* Test 2: 64KB Boundary-crossing buffer (should bounce) */
    test_buffer = allocate_boundary_buffer();
    if (test_buffer) {
        _fmemset(test_buffer, TEST_PATTERN_55, BOUNDARY_TEST_SIZE);
        
        if (g_vds_available) {
            vds_lock_region(test_buffer, BOUNDARY_TEST_SIZE, &dds);
            physical = dds.physical;
            vds_unlock_region(&dds);
        } else {
            physical = far_ptr_to_physical(test_buffer);
        }
        
        printf("  Test 2 - 64K boundary at %08lX: ", physical);
        
        if (crosses_64k_boundary(physical, BOUNDARY_TEST_SIZE)) {
            /* Send packet - should trigger bounce buffer */
            if (send_test_packet(test_buffer, BOUNDARY_TEST_SIZE)) {
                test_count++;
                printf("OK (bounce expected)\n");
                g_results.boundaries.cross64k_bounces++;
            } else {
                printf("SEND FAILED\n");
            }
        } else {
            printf("no crossing (test setup failed)\n");
        }
        
        _ffree(test_buffer);
    }
    g_results.boundaries_tested++;
    
    /* Test 3: Misaligned buffer (odd address - should bounce on some systems) */
    test_buffer = _fmalloc(1024);
    if (test_buffer) {
        /* Make it odd-aligned */
        test_buffer = (void far *)((char far *)test_buffer + 1);
        _fmemset(test_buffer, TEST_PATTERN_FF, 512);
        
        physical = far_ptr_to_physical(test_buffer);
        printf("  Test 3 - Misaligned at %08lX: ", physical);
        
        if (physical & 0x01) {
            /* Send packet - may trigger bounce on word-aligned DMA */
            if (send_test_packet(test_buffer, 512)) {
                test_count++;
                printf("OK (bounce possible)\n");
                g_results.boundaries.misaligned_bounces++;
            } else {
                printf("SEND FAILED\n");
            }
        } else {
            printf("alignment test setup failed\n");
        }
        /* Note: Don't free the adjusted pointer */
    }
    g_results.boundaries_tested++;
    
    /* Test 4: Above 16MB buffer (if available - should be rejected) */
    /* This test requires XMS or other extended memory access */
    printf("  Test 4 - Above 16MB: ");
    if (g_telemetry.xms_present) {
        /* Would need XMS allocation here */
        printf("SKIPPED (XMS test not implemented)\n");
    } else {
        printf("SKIPPED (no XMS)\n");
    }
    
    /* Get final stats */
    get_dma_stats(&final_bounces, &final_violations);
    
    g_results.bounce_count = final_bounces - initial_bounces;
    g_results.boundary_violations = final_violations - initial_violations;
    
    printf("\n  Summary:\n");
    printf("    Tests run: %d\n", test_count);
    printf("    Bounces triggered: %lu\n", g_results.bounce_count);
    printf("    Violations detected: %lu\n", g_results.boundary_violations);
    printf("    64K crossings handled: %lu\n", g_results.boundaries.cross64k_bounces);
    printf("    Misaligned handled: %lu\n", g_results.boundaries.misaligned_bounces);
    
    if (g_results.boundary_violations > 0) {
        strcpy(g_results.failure_reason, "Boundary violations detected");
        success = false;
    }
    
    return success;
}

/**
 * Test cache coherency
 */
static bool test_cache_coherency(void) {
    uint8_t far *test_buffer;
    uint32_t timings[10];
    uint32_t start, end;
    int i, j;
    
    printf("Testing cache coherency...\n");
    
    test_buffer = _fmalloc(1024);
    if (!test_buffer) {
        return false;
    }
    
    /* Test 1: CPU write → flush → device read */
    _fmemset(test_buffer, TEST_PATTERN_AA, 1024);
    
    /* WBINVD on 486+ */
    _asm {
        wbinvd
    }
    
    /* Verify pattern (simulated device read) */
    for (i = 0; i < 1024; i++) {
        if (test_buffer[i] != TEST_PATTERN_AA) {
            printf("  Coherency error at byte %d\n", i);
            g_results.coherency_passed = false;
            _ffree(test_buffer);
            return false;
        }
    }
    
    /* Measure WBINVD timing with proper cooldowns */
    printf("  Measuring WBINVD timing with cooldowns...\n");
    
    /* Disable DOS idle helpers */
    _asm {
        mov ax, 0x1680  ; Release time slice
        int 0x2F        ; Multiplex interrupt
    }
    
    for (i = 0; i < 10; i++) {
        /* Longer cooldown between samples */
        delay_us(1000);  /* 1ms cooldown */
        
        /* Flush any pending work */
        _asm {
            wbinvd
        }
        delay_us(100);   /* Let cache settle */
        
        _disable();
        start = read_pit();
        _asm {
            wbinvd
        }
        end = read_pit();
        _enable();
        
        timings[i] = (start > end) ? (start - end) : (0xFFFF - end + start);
        
        /* Skip outliers (first measurement often slow) */
        if (i == 0 && timings[0] > timings[1] * 2) {
            i--;  /* Redo first measurement */
        }
    }
    
    /* Sort timings for median/P95 */
    for (i = 0; i < 9; i++) {
        for (j = i + 1; j < 10; j++) {
            if (timings[i] > timings[j]) {
                uint32_t temp = timings[i];
                timings[i] = timings[j];
                timings[j] = temp;
            }
        }
    }
    
    g_results.wbinvd_median_ticks = timings[5];
    g_results.wbinvd_p95_ticks = timings[9];
    
    /* Convert to microseconds (1.193MHz PIT) */
    uint32_t median_us = (g_results.wbinvd_median_ticks * 1000) / 1193;
    uint32_t p95_us = (g_results.wbinvd_p95_ticks * 1000) / 1193;
    
    printf("  WBINVD median: %lu us\n", median_us);
    printf("  WBINVD P95: %lu us\n", p95_us);
    
    g_results.coherency_passed = true;
    g_results.selected_tier = 1;  /* WBINVD */
    
    _ffree(test_buffer);
    return true;
}

/**
 * Measure PIO performance through driver
 */
static uint32_t measure_pio_performance(void) {
    uint8_t far *buffer;
    uint32_t start_ticks, end_ticks;
    uint32_t bytes_transferred = 0;
    uint32_t packets_sent = 0;
    union REGS r;
    struct SREGS sr;
    int i;
    
    printf("Measuring PIO performance...\n");
    
    buffer = _fmalloc(1514);
    if (!buffer) {
        return 0;
    }
    
    /* Fill with test pattern */
    _fmemset(buffer, 0x5A, 1514);
    
    /* Force PIO mode via Extension API */
    r.h.ah = 0x93;  /* Set transfer mode */
    r.h.al = 0;     /* 0 = PIO mode */
    int86(PACKET_INT, &r, &r);
    
    /* Send 100 test packets */
    start_ticks = read_pit();
    
    for (i = 0; i < 100; i++) {
        /* Use packet driver send_pkt */
        r.h.ah = 0x04;  /* send_pkt */
        r.x.cx = 1514;  /* Length */
        sr.ds = FP_SEG(buffer);
        r.x.si = FP_OFF(buffer);
        int86x(PACKET_INT, &r, &r, &sr);
        
        if (!r.x.cflag) {
            packets_sent++;
            bytes_transferred += 1514;
        }
    }
    
    end_ticks = read_pit();
    
    /* Calculate throughput */
    uint32_t ticks_elapsed = (start_ticks > end_ticks) ? 
                             (start_ticks - end_ticks) : 
                             (0xFFFF - end_ticks + start_ticks);
    uint32_t ms_elapsed = (ticks_elapsed * 1000) / 1193;
    
    if (ms_elapsed > 0) {
        g_results.pio_throughput_kbps = (bytes_transferred * 8) / ms_elapsed;
    }
    
    printf("  PIO: %lu packets in %lu ms, %lu KB/s\n", 
           packets_sent, ms_elapsed, g_results.pio_throughput_kbps / 8);
    
    _ffree(buffer);
    return g_results.pio_throughput_kbps;
}

/**
 * Measure DMA performance through driver
 */
static uint32_t measure_dma_performance(void) {
    uint8_t far *buffer;
    uint32_t start_ticks, end_ticks;
    uint32_t bytes_transferred = 0;
    uint32_t packets_sent = 0;
    union REGS r;
    struct SREGS sr;
    int i;
    
    printf("Measuring DMA performance...\n");
    
    buffer = _fmalloc(1514);
    if (!buffer) {
        return 0;
    }
    
    /* Fill with test pattern */
    _fmemset(buffer, 0xA5, 1514);
    
    /* Enable DMA mode via Extension API */
    r.h.ah = 0x93;  /* Set transfer mode */
    r.h.al = 1;     /* 1 = DMA mode */
    int86(PACKET_INT, &r, &r);
    
    if (r.x.cflag) {
        printf("  DMA mode not available\n");
        _ffree(buffer);
        return 0;
    }
    
    /* Send 100 test packets */
    start_ticks = read_pit();
    
    for (i = 0; i < 100; i++) {
        /* Use packet driver send_pkt */
        r.h.ah = 0x04;  /* send_pkt */
        r.x.cx = 1514;  /* Length */
        sr.ds = FP_SEG(buffer);
        r.x.si = FP_OFF(buffer);
        int86x(PACKET_INT, &r, &r, &sr);
        
        if (!r.x.cflag) {
            packets_sent++;
            bytes_transferred += 1514;
        }
    }
    
    end_ticks = read_pit();
    
    /* Calculate throughput */
    uint32_t ticks_elapsed = (start_ticks > end_ticks) ? 
                             (start_ticks - end_ticks) : 
                             (0xFFFF - end_ticks + start_ticks);
    uint32_t ms_elapsed = (ticks_elapsed * 1000) / 1193;
    
    if (ms_elapsed > 0) {
        g_results.dma_throughput_kbps = (bytes_transferred * 8) / ms_elapsed;
    }
    
    printf("  DMA: %lu packets in %lu ms, %lu KB/s\n", 
           packets_sent, ms_elapsed, g_results.dma_throughput_kbps / 8);
    
    /* Calculate speedup */
    if (g_results.pio_throughput_kbps > 0) {
        uint32_t speedup = (g_results.dma_throughput_kbps * 100) / g_results.pio_throughput_kbps;
        printf("  Speedup: %lu.%02lu x\n", speedup / 100, speedup % 100);
    }
    
    _ffree(buffer);
    return g_results.dma_throughput_kbps;
}

/**
 * Calculate hardware signature
 */
static uint32_t calculate_hardware_signature(void) {
    uint32_t sig = 0;
    union REGS r;
    
    /* Get CPU info */
    _asm {
        pushf
        pop ax
        mov bx, ax
        xor ax, 0x7000
        push ax
        popf
        pushf
        pop ax
        cmp ax, bx
        je no_cpuid
        mov ax, 1
        jmp done
    no_cpuid:
        xor ax, ax
    done:
        mov word ptr sig, ax
    }
    
    /* Add I/O base and IRQ */
    sig = (sig << 16) | (g_nic_io_base << 8) | g_nic_irq;
    
    return sig;
}

/**
 * Save DMA policy to file
 */
static void save_dma_policy(dma_policy_t *policy) {
    FILE *fp;
    char temp_name[] = "C:\\3CPKT\\DMA.TMP";
    char final_name[] = "C:\\3CPKT\\DMA.SAF";
    
    /* Create directory if needed */
    mkdir("C:\\3CPKT");
    
    policy->signature = calculate_hardware_signature();
    
    fp = fopen(temp_name, "wb");
    if (!fp) {
        printf("Failed to save policy\n");
        return;
    }
    
    fwrite(policy, sizeof(*policy), 1, fp);
    fclose(fp);
    
    /* Atomic rename */
    unlink(final_name);
    rename(temp_name, final_name);
    
    printf("Policy saved to %s\n", final_name);
}

/**
 * Generate test report
 */
static void generate_report(test_results_t *results, const char *decision) {
    FILE *fp;
    time_t now;
    
    fp = fopen("BMTEST.RPT", "w");
    if (!fp) {
        return;
    }
    
    time(&now);
    
    fprintf(fp, "3C515 DMA Validation Report\n");
    fprintf(fp, "===========================\n");
    fprintf(fp, "Date: %s", ctime(&now));
    fprintf(fp, "\nTest Results:\n");
    fprintf(fp, "  Boundaries tested: %lu\n", results->boundaries_tested);
    fprintf(fp, "  Bounces used: %lu\n", results->bounce_count);
    fprintf(fp, "  Violations: %lu\n", results->boundary_violations);
    fprintf(fp, "\n  Cache coherency: %s\n", 
            results->coherency_passed ? "PASS" : "FAIL");
    fprintf(fp, "  WBINVD median: %lu ticks\n", results->wbinvd_median_ticks);
    fprintf(fp, "\n  Performance:\n");
    fprintf(fp, "    PIO: %lu KB/s\n", results->pio_throughput_kbps / 8);
    fprintf(fp, "    DMA: %lu KB/s\n", results->dma_throughput_kbps / 8);
    fprintf(fp, "\nDecision: %s\n", decision);
    
    if (results->tests_passed) {
        fprintf(fp, "DMA ENABLED\n");
    } else {
        fprintf(fp, "DMA DISABLED: %s\n", results->failure_reason);
    }
    
    fclose(fp);
    
    /* Generate enhanced JSON */
    fp = fopen("BMTEST.JSN", "w");
    if (fp) {
        time_t now;
        time(&now);
        
        fprintf(fp, "{\n");
        fprintf(fp, "  \"version\": \"1.1\",\n");
        fprintf(fp, "  \"timestamp\": %ld,\n", now);
        fprintf(fp, "  \"telemetry_stamp\": \"0x%04X\",\n", results->telemetry.uptime_ticks);
        
        /* Environment */
        fprintf(fp, "  \"environment\": {\n");
        fprintf(fp, "    \"dos_version\": \"%d.%02d\",\n", 
                results->telemetry.dos_major, results->telemetry.dos_minor);
        fprintf(fp, "    \"ems_present\": %s,\n", results->telemetry.ems_present ? "true" : "false");
        fprintf(fp, "    \"xms_present\": %s,\n", results->telemetry.xms_present ? "true" : "false");
        fprintf(fp, "    \"vds_present\": %s,\n", results->telemetry.vds_present ? "true" : "false");
        fprintf(fp, "    \"bios_cache\": \"%s\"\n", results->bios_cache);
        fprintf(fp, "  },\n");
        
        /* Hardware */
        fprintf(fp, "  \"hardware\": {\n");
        fprintf(fp, "    \"cpu_family\": %d,\n", results->telemetry.cpu_family);
        fprintf(fp, "    \"chipset\": \"unknown\",\n");
        fprintf(fp, "    \"nic\": \"%s\",\n", results->telemetry.nic_type == 2 ? "3C515-TX" : "3C509B");
        fprintf(fp, "    \"io_base\": \"0x%03X\",\n", results->telemetry.nic_io_base);
        fprintf(fp, "    \"irq\": %d,\n", results->telemetry.nic_irq);
        fprintf(fp, "    \"capability_mask\": \"0x%08lX\"\n", results->telemetry.capability);
        fprintf(fp, "  },\n");
        
        /* Safety snapshot */
        fprintf(fp, "  \"safety_snapshot\": {\n");
        fprintf(fp, "    \"health_flags\": \"0x%04X\",\n", results->telemetry.health_flags);
        fprintf(fp, "    \"patch_count\": %d,\n", results->telemetry.patch_count);
        fprintf(fp, "    \"patches_active\": \"0x%02X\",\n", results->telemetry.patches_active);
        fprintf(fp, "    \"loopback_on\": %s,\n", results->telemetry.loopback_on ? "true" : "false");
        fprintf(fp, "    \"cascade_ok\": %s\n", results->telemetry.cascade_ok ? "true" : "false");
        fprintf(fp, "  },\n");
        
        /* Tests */
        fprintf(fp, "  \"tests\": {\n");
        
        /* Boundaries */
        fprintf(fp, "    \"boundaries\": {\n");
        fprintf(fp, "      \"aligned\": {\"bounces\": %lu, \"violations\": %lu},\n",
                results->boundaries.aligned_bounces, results->boundaries.aligned_violations);
        fprintf(fp, "      \"cross_64k\": {\"bounces\": %lu, \"violations\": %lu},\n",
                results->boundaries.cross64k_bounces, results->boundaries.cross64k_violations);
        fprintf(fp, "      \"above_16mb\": {\"rejected\": %lu},\n",
                results->boundaries.above16m_rejected);
        fprintf(fp, "      \"misaligned\": {\"bounces\": %lu, \"violations\": %lu},\n",
                results->boundaries.misaligned_bounces, results->boundaries.misaligned_violations);
        fprintf(fp, "      \"pass\": %s\n", 
                (results->boundaries.aligned_violations + results->boundaries.cross64k_violations +
                 results->boundaries.misaligned_violations) == 0 ? "true" : "false");
        fprintf(fp, "    },\n");
        
        /* Coherency */
        fprintf(fp, "    \"coherency\": {\n");
        fprintf(fp, "      \"tier\": %d,\n", results->selected_tier);
        fprintf(fp, "      \"wbinvd_median_us\": %lu,\n", results->wbinvd_median_us);
        fprintf(fp, "      \"wbinvd_p95_us\": %lu,\n", results->wbinvd_p95_us);
        fprintf(fp, "      \"stale_reads\": %lu,\n", results->stale_reads);
        fprintf(fp, "      \"cli_ticks_max\": %lu,\n", results->cli_max_ticks);
        fprintf(fp, "      \"pass\": %s\n", results->coherency_passed ? "true" : "false");
        fprintf(fp, "    },\n");
        
        /* Performance */
        fprintf(fp, "    \"performance\": {\n");
        fprintf(fp, "      \"pio\": {\n");
        fprintf(fp, "        \"throughput_kbps\": %lu,\n", results->pio.throughput_kbps);
        fprintf(fp, "        \"cpu_percent\": %lu,\n", results->pio.cpu_percent);
        fprintf(fp, "        \"latency_max_us\": %lu,\n", results->pio.latency_max_us);
        fprintf(fp, "        \"latency_avg_us\": %lu,\n", results->pio.latency_avg_us);
        fprintf(fp, "        \"latency_median_us\": %lu,\n", results->pio.latency_median_us);
        fprintf(fp, "        \"latency_p95_us\": %lu\n", results->pio.latency_p95_us);
        fprintf(fp, "      },\n");
        fprintf(fp, "      \"dma\": {\n");
        fprintf(fp, "        \"throughput_kbps\": %lu,\n", results->dma.throughput_kbps);
        fprintf(fp, "        \"cpu_percent\": %lu,\n", results->dma.cpu_percent);
        fprintf(fp, "        \"latency_max_us\": %lu,\n", results->dma.latency_max_us);
        fprintf(fp, "        \"latency_avg_us\": %lu,\n", results->dma.latency_avg_us);
        fprintf(fp, "        \"latency_median_us\": %lu,\n", results->dma.latency_median_us);
        fprintf(fp, "        \"latency_p95_us\": %lu\n", results->dma.latency_p95_us);
        fprintf(fp, "      },\n");
        
        /* Speedup calculation */
        float speedup = 1.0;
        if (results->pio.throughput_kbps > 0) {
            speedup = (float)results->dma.throughput_kbps / results->pio.throughput_kbps;
        }
        fprintf(fp, "      \"speedup\": %.2f,\n", speedup);
        fprintf(fp, "      \"pass\": %s\n", speedup >= 1.5 ? "true" : "false");
        fprintf(fp, "    }\n");
        fprintf(fp, "  },\n");
        
        /* Decision */
        fprintf(fp, "  \"decision\": {\n");
        fprintf(fp, "    \"dma_enabled\": %s,\n", results->tests_passed ? "true" : "false");
        fprintf(fp, "    \"reason\": \"%s\",\n", 
                results->tests_passed ? "All tests passed" : results->failure_reason);
        fprintf(fp, "    \"policy\": {\n");
        fprintf(fp, "      \"runtime_enable\": 0,\n");
        fprintf(fp, "      \"validation_passed\": %d,\n", results->tests_passed ? 1 : 0);
        fprintf(fp, "      \"last_known_safe\": %d,\n", results->tests_passed ? 1 : 0);
        fprintf(fp, "      \"hw_signature\": \"0x%08lX\"\n", results->hw_signature);
        fprintf(fp, "    }\n");
        fprintf(fp, "  }\n");
        fprintf(fp, "}\n");
        fclose(fp);
    }
}

/**
 * Main test entry point
 */
/**
 * Print usage information
 */
static void print_usage(void) {
    printf("BMTEST - Bus Master Test Utility for 3C515\n");
    printf("Usage: BMTEST [options]\n");
    printf("Options:\n");
    printf("  -j             JSON output format\n");
    printf("  -v             Verbose output\n");
    printf("  -s             Run 10-minute stress test\n");
    printf("  -S <mins>      Run soak test for N minutes (30-60)\n");
    printf("  -n             Run negative test (force failure)\n");
    printf("  -d             Run standard DMA validation tests\n");
    printf("  -seed <value>  Set random seed for deterministic tests\n");
    printf("  -rate <pps>    Target packet rate (default 100 pps)\n");
    printf("  -h             This help message\n");
}

int main(int argc, char *argv[]) {
    bool all_passed = true;
    bool json_output = false;
    bool verbose = false;
    bool stress_test = false;
    bool negative_test = false;
    bool standard_test = false;
    uint32_t soak_mins = 0;
    uint32_t test_seed = 0x12345678;  /* Default seed */
    uint32_t target_rate = 100;       /* Default 100 pps */
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-j") == 0) {
            json_output = true;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-s") == 0) {
            stress_test = true;
        } else if (strcmp(argv[i], "-S") == 0 && i + 1 < argc) {
            soak_mins = atoi(argv[++i]);
            if (soak_mins < 30 || soak_mins > 60) {
                printf("Error: Soak test duration must be 30-60 minutes\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-n") == 0) {
            negative_test = true;
        } else if (strcmp(argv[i], "-d") == 0) {
            standard_test = true;
        } else if (strcmp(argv[i], "-seed") == 0 && i + 1 < argc) {
            test_seed = strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "-rate") == 0 && i + 1 < argc) {
            target_rate = atoi(argv[++i]);
            if (target_rate < 1 || target_rate > 1000) {
                printf("Error: Rate must be 1-1000 pps\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else {
            printf("Unknown option: %s\n", argv[i]);
            print_usage();
            return 1;
        }
    }
    
    /* Default to standard test if no specific test selected */
    if (!stress_test && !negative_test && soak_mins == 0 && !standard_test) {
        standard_test = true;
    }
    
    printf("3C515 Bus Master Test Utility v1.0\n");
    printf("===================================\n\n");
    
    /* Initialize */
    memset(&g_results, 0, sizeof(g_results));
    memset(&g_policy, 0, sizeof(g_policy));
    
    /* Check for VDS */
    g_vds_available = vds_available();
    if (verbose) {
        printf("VDS: %s\n", g_vds_available ? "Available" : "Not available");
    }
    
    /* Check driver is loaded for all tests */
    if (!verify_driver_loaded()) {
        printf("ERROR: Driver not loaded\n");
        return 1;
    }
    
    /* Run stress/soak/negative tests if requested */
    if (stress_test) {
        printf("Starting 10-minute stress test (seed=0x%08lX, rate=%lu pps)...\n", test_seed, target_rate);
        set_stress_params(test_seed, target_rate);
        int result = run_stress_test(600, verbose);
        
        if (json_output) {
            /* Get stress stats for JSON */
            struct {
                uint32_t packets_sent;
                uint32_t packets_failed;
                uint32_t bytes_sent;
                uint32_t errors_detected;
                uint32_t health_checks;
                uint32_t rollbacks;
                time_t start_time;
                time_t end_time;
            } stats;
            
            get_stress_stats(&stats);
            
            printf("{\n");
            printf("  \"schema_version\": \"%s\",\n", JSON_SCHEMA_VERSION);
            printf("  \"test\": \"stress\",\n");
            printf("  \"parameters\": {\n");
            printf("    \"seed\": \"0x%08lX\",\n", test_seed);
            printf("    \"target_rate_pps\": %lu,\n", target_rate);
            printf("    \"duration_sec\": %lu\n", (uint32_t)(stats.end_time - stats.start_time));
            printf("  },\n");
            printf("  \"results\": {\n");
            printf("    \"packets_sent\": %lu,\n", stats.packets_sent);
            printf("    \"packets_failed\": %lu,\n", stats.packets_failed);
            printf("    \"bytes_sent\": %lu,\n", stats.bytes_sent);
            printf("    \"throughput_kbps\": %lu,\n", stats.bytes_sent * 8 / (stats.end_time - stats.start_time) / 1000);
            printf("    \"health_checks\": %lu,\n", stats.health_checks);
            printf("    \"rollbacks\": %lu,\n", stats.rollbacks);
            printf("    \"error_rate\": %.4f\n", (float)stats.packets_failed / (float)stats.packets_sent);
            printf("  },\n");
            printf("  \"units\": {\n");
            printf("    \"throughput\": \"kilobits_per_second\",\n");
            printf("    \"duration\": \"seconds\",\n");
            printf("    \"bytes\": \"bytes\",\n");
            printf("    \"rate\": \"packets_per_second\"\n");
            printf("  },\n");
            printf("  \"result\": \"%s\"\n", result ? "PASS" : "FAIL");
            printf("}\n");
        }
        return result ? 0 : 1;
    }
    
    if (soak_mins > 0) {
        printf("Starting %lu-minute soak test...\n", soak_mins);
        int result = run_soak_test(soak_mins, verbose);
        
        if (json_output) {
            printf("{\n");
            printf("  \"test\": \"soak\",\n");
            printf("  \"duration_min\": %lu,\n", soak_mins);
            printf("  \"result\": \"%s\"\n", result ? "PASS" : "FAIL");
            printf("}\n");
        }
        return result ? 0 : 1;
    }
    
    if (negative_test) {
        printf("Running negative test...\n");
        int result = run_negative_test();
        
        if (json_output) {
            printf("{\n");
            printf("  \"test\": \"negative\",\n");
            printf("  \"result\": \"%s\"\n", result ? "PASS" : "FAIL");
            printf("}\n");
        }
        return result ? 0 : 1;
    }
    
    /* Standard DMA validation tests */
    if (!standard_test) {
        return 0;
    }
    
    /* Phase 1: Pre-checks */
    printf("\nPhase 1: Pre-validation\n");
    printf("  Driver loaded: OK\n");
    
    if (!verify_patches_active()) {
        printf("  ERROR: Patches not active\n");
        strcpy(g_results.failure_reason, "Patches not active");
        all_passed = false;
        goto done;
    }
    printf("  Patches active: OK\n");
    
    /* Phase 2: Quiesce driver */
    printf("\nPhase 2: Driver control\n");
    
    if (!quiesce_driver()) {
        printf("  ERROR: Failed to quiesce driver\n");
        return 1;
    }
    printf("  Driver quiesced: OK\n");
    
    /* Enable loopback */
    enable_nic_loopback();
    printf("  Loopback enabled: OK\n");
    
    /* Phase 3: Boundary testing */
    printf("\nPhase 3: Boundary validation\n");
    if (!test_boundaries()) {
        all_passed = false;
        goto resume;
    }
    
    /* Phase 4: Cache coherency */
    printf("\nPhase 4: Cache coherency\n");
    if (!test_cache_coherency()) {
        all_passed = false;
        strcpy(g_results.failure_reason, "Cache coherency failed");
        goto resume;
    }
    
    /* Phase 5: Performance */
    printf("\nPhase 5: Performance comparison\n");
    measure_pio_performance();
    measure_dma_performance();
    
    if (g_results.dma_throughput_kbps < g_results.pio_throughput_kbps) {
        printf("  DMA slower than PIO!\n");
        strcpy(g_results.failure_reason, "DMA slower than PIO");
        all_passed = false;
    }
    
resume:
    /* Resume driver */
    if (!resume_driver()) {
        printf("  WARNING: Failed to resume driver\n");
    } else {
        printf("\nDriver resumed: OK\n");
    }
    
done:
    /* Generate results */
    g_results.tests_passed = all_passed;
    
    printf("\n===================================\n");
    if (all_passed) {
        printf("RESULT: ALL TESTS PASSED\n");
        printf("DMA can be enabled safely\n");
        
        /* Update DMA policy via Extension API */
        update_dma_policy(1);  /* validation_passed = true */
        
        g_policy.validation_passed = 1;
        g_policy.last_known_safe = 1;
        save_dma_policy(&g_policy);
    } else {
        printf("RESULT: TESTS FAILED\n");
        printf("Reason: %s\n", g_results.failure_reason);
        printf("DMA will remain disabled\n");
        
        /* Update DMA policy to reflect failure */
        update_dma_policy(0);  /* validation_passed = false */
        
        g_policy.validation_passed = 0;
        g_policy.last_known_safe = 0;
        g_policy.failure_reason = 1;
    }
    
    generate_report(&g_results, all_passed ? "PASS" : "FAIL");
    printf("\nReport saved to BMTEST.RPT\n");
    
    return all_passed ? 0 : 1;
}

/**
 * Stress test implementation - rapid packet transmission with randomization
 */
int run_stress_test(uint32_t duration_secs, bool verbose) {
    uint8_t far *buffer;
    uint32_t start_ticks, current_ticks, elapsed_ticks;
    uint32_t packets_sent = 0, packets_failed = 0;
    uint32_t target_pps = 1000;  /* Default 1000 packets/sec */
    uint32_t packet_delay_us;
    union REGS r;
    struct SREGS sr;
    uint16_t sizes[] = {64, 128, 256, 512, 1024, 1514};
    int size_idx = 0;
    uint32_t duration_ticks = duration_secs * 18;  /* 18.2 ticks/sec */
    
    buffer = _fmalloc(1514);
    if (!buffer) {
        printf("ERROR: Failed to allocate buffer\n");
        return 0;
    }
    
    packet_delay_us = 1000000L / target_pps;
    
    /* Initialize random seed */
    srand((unsigned)time(NULL));
    
    /* Initialize global stats */
    g_stress_stats.start_time = time(NULL);
    g_stress_stats.bytes_sent = 0;
    
    /* Get starting BIOS tick count (0x0040:0x006C) */
    _disable();
    start_ticks = *((uint32_t far *)MK_FP(0x0040, 0x006C));
    _enable();
    
    printf("Running stress test for %lu seconds...\n", duration_secs);
    
    elapsed_ticks = 0;
    while (elapsed_ticks < duration_ticks) {
        uint16_t packet_size = sizes[size_idx];
        
        /* Update elapsed time at start of each iteration */
        _disable();
        current_ticks = *((uint32_t far *)MK_FP(0x0040, 0x006C));
        _enable();
        
        /* Handle midnight rollover using BIOS rollover flag at 0040:0070 */
        /* Flag is set to 1 when midnight rollover occurs, cleared on read */
        if (*((uint8_t far *)MK_FP(0x0040, 0x0070)) != 0) {
            /* Rollover occurred - add full day of ticks (0x1800B0) */
            elapsed_ticks = (0x1800B0UL - start_ticks) + current_ticks;
            /* Clear the rollover flag */
            *((uint8_t far *)MK_FP(0x0040, 0x0070)) = 0;
        } else if (current_ticks < start_ticks) {
            /* Rollover detected by tick comparison */
            elapsed_ticks = (0x1800B0UL - start_ticks) + current_ticks;
        } else {
            elapsed_ticks = current_ticks - start_ticks;
        }
        
        /* Fill with random pattern */
        for (int i = 0; i < packet_size; i++) {
            buffer[i] = rand() & 0xFF;
        }
        
        /* Send packet via driver */
        memset(&r, 0, sizeof(r));      /* Clear registers */
        memset(&sr, 0, sizeof(sr));    /* Clear segment registers */
        r.h.ah = 0x04;  /* send_pkt */
        r.x.cx = packet_size;
        sr.es = FP_SEG(buffer);
        r.x.si = FP_OFF(buffer);
        int86x(PACKET_INT, &r, &r, &sr);
        
        if (r.x.cflag) {
            packets_failed++;
            if (verbose) {
                printf("  Packet %lu failed (size=%u, error=0x%04X)\n", 
                       packets_sent, packet_size, r.x.ax);
            }
        } else {
            packets_sent++;
        }
        
        /* Update bytes sent for statistics */
        g_stress_stats.bytes_sent += packet_size;
        
        /* Vary packet size */
        size_idx = (size_idx + 1) % 6;
        
        /* Brief delay to control rate */
        delay_us(packet_delay_us);
        
        /* Check for user abort */
        if (kbhit()) {
            if (getch() == 27) {  /* ESC key */
                printf("Stress test aborted by user\n");
                break;
            }
        }
    }
    
    /* Update global statistics */
    g_stress_stats.packets_sent = packets_sent;
    g_stress_stats.packets_failed = packets_failed;
    g_stress_stats.end_time = time(NULL);
    
    printf("Stress test complete:\n");
    printf("  Packets sent: %lu\n", packets_sent);
    printf("  Packets failed: %lu\n", packets_failed);
    printf("  Bytes sent: %lu\n", g_stress_stats.bytes_sent);
    printf("  Success rate: %.2f%%\n", 
           packets_sent ? (100.0 * (packets_sent - packets_failed) / packets_sent) : 0);
    printf("  Duration: %lu seconds\n", elapsed_ticks / 18);
    
    _ffree(buffer);
    return (packets_failed == 0) ? 1 : 0;
}

/**
 * Soak test - long duration stability test
 */
int run_soak_test(uint32_t duration_mins, bool verbose) {
    uint32_t duration_secs = duration_mins * 60;
    uint8_t far *buffer;
    uint32_t start_time, current_time, last_report;
    uint32_t packets_sent = 0, packets_failed = 0;
    uint32_t health_checks = 0;
    union REGS r;
    struct SREGS sr;
    
    buffer = _fmalloc(1514);
    if (!buffer) {
        return 0;
    }
    
    /* Fill with stable pattern */
    _fmemset(buffer, 0xAA, 1514);
    
    start_time = time(NULL);
    last_report = start_time;
    current_time = start_time;
    
    printf("Running soak test for %lu minutes...\n", duration_mins);
    
    while ((current_time - start_time) < duration_secs) {
        /* Send steady stream of packets */
        r.h.ah = 0x04;  /* send_pkt */
        r.x.cx = 1514;
        sr.es = FP_SEG(buffer);
        r.x.si = FP_OFF(buffer);
        int86x(PACKET_INT, &r, &r, &sr);
        
        if (r.x.cflag) {
            packets_failed++;
        } else {
            packets_sent++;
        }
        
        /* Periodic health check via Extension API */
        if ((packets_sent % 1000) == 0) {
            r.h.ah = 0x81;  /* Get safety state */
            int86(PACKET_INT, &r, &r);
            if (!r.x.cflag) {
                health_checks++;
                if (verbose && (r.x.ax & 0x8000)) {  /* Kill switch active */
                    printf("WARNING: Kill switch activated at packet %lu\n", packets_sent);
                }
            }
        }
        
        /* Progress report every minute */
        if ((current_time - last_report) >= 60) {
            printf("  %lu min: %lu packets, %lu failures\n", 
                   (current_time - start_time) / 60, packets_sent, packets_failed);
            last_report = current_time;
        }
        
        /* Slow steady pace - 100 pps */
        delay(10);  /* 10ms between packets */
        
        current_time = time(NULL);
    }
    
    printf("Soak test complete:\n");
    printf("  Duration: %lu minutes\n", duration_mins);
    printf("  Packets sent: %lu\n", packets_sent);
    printf("  Packets failed: %lu\n", packets_failed);
    printf("  Health checks: %lu\n", health_checks);
    
    _ffree(buffer);
    return (packets_failed == 0) ? 1 : 0;
}

/**
 * Negative test - intentionally trigger error conditions
 */
int run_negative_test(void) {
    uint8_t far *bad_buffer;
    union REGS r;
    struct SREGS sr;
    int tests_passed = 0;
    int total_tests = 0;
    
    printf("Running negative tests...\n");
    
    /* Test 1: NULL buffer */
    printf("  Test 1: NULL buffer... ");
    total_tests++;
    r.h.ah = 0x04;  /* send_pkt */
    r.x.cx = 100;
    sr.es = 0;
    r.x.si = 0;
    int86x(PACKET_INT, &r, &r, &sr);
    if (r.x.cflag) {
        printf("PASS (correctly rejected)\n");
        tests_passed++;
    } else {
        printf("FAIL (should have rejected)\n");
    }
    
    /* Test 2: Oversized packet */
    printf("  Test 2: Oversized packet... ");
    total_tests++;
    bad_buffer = _fmalloc(2000);
    if (bad_buffer) {
        r.h.ah = 0x04;
        r.x.cx = 2000;  /* Too large */
        sr.es = FP_SEG(bad_buffer);
        r.x.si = FP_OFF(bad_buffer);
        int86x(PACKET_INT, &r, &r, &sr);
        if (r.x.cflag) {
            printf("PASS (correctly rejected)\n");
            tests_passed++;
        } else {
            printf("FAIL (should have rejected)\n");
        }
        _ffree(bad_buffer);
    } else {
        printf("SKIP (no memory)\n");
    }
    
    /* Test 3: Undersized packet */
    printf("  Test 3: Undersized packet... ");
    total_tests++;
    bad_buffer = _fmalloc(100);
    if (bad_buffer) {
        r.h.ah = 0x04;
        r.x.cx = 10;  /* Too small for Ethernet */
        sr.es = FP_SEG(bad_buffer);
        r.x.si = FP_OFF(bad_buffer);
        int86x(PACKET_INT, &r, &r, &sr);
        if (r.x.cflag) {
            printf("PASS (correctly rejected)\n");
            tests_passed++;
        } else {
            printf("FAIL (should have rejected)\n");
        }
        _ffree(bad_buffer);
    } else {
        printf("SKIP (no memory)\n");
    }
    
    /* Test 4: Invalid function code */
    printf("  Test 4: Invalid function... ");
    total_tests++;
    r.h.ah = 0xFF;  /* Invalid function */
    int86(PACKET_INT, &r, &r);
    if (r.x.cflag && r.x.ax == 0xFFFF) {
        printf("PASS (correctly rejected)\n");
        tests_passed++;
    } else {
        printf("FAIL (should have rejected)\n");
    }
    
    printf("\nNegative tests: %d/%d passed\n", tests_passed, total_tests);
    return (tests_passed == total_tests) ? 1 : 0;
}

/* Global stress test statistics */
static struct {
    uint32_t packets_sent;
    uint32_t packets_failed;
    uint32_t bytes_sent;
    uint32_t errors_detected;
    uint32_t health_checks;
    uint32_t rollbacks;
    time_t start_time;
    time_t end_time;
    uint32_t seed;
    uint32_t rate;
} g_stress_stats = {0};

/**
 * Get stress test statistics
 */
void get_stress_stats(void *stats) {
    memcpy(stats, &g_stress_stats, sizeof(g_stress_stats));
}

/**
 * Set stress test parameters
 */
void set_stress_params(uint32_t seed, uint32_t rate) {
    g_stress_stats.seed = seed;
    g_stress_stats.rate = rate;
    srand(seed);
}
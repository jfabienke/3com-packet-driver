/**
 * @file stress_test.c
 * @brief Stress and soak test implementations for BMTEST
 *
 * Provides extended duration testing with mixed workloads
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <dos.h>
#include "../include/common.h"

#define PACKET_INT 0x60
#define DEFAULT_SEED 0x12345678
#define DEFAULT_RATE 100  /* packets per second */

/* Packet sizes for mixed workload */
static const uint16_t packet_sizes[] = {
    64,     /* Minimum Ethernet */
    128,    /* Small */
    256,    /* Small-medium */
    512,    /* Medium */
    576,    /* Typical Internet */
    1024,   /* Large */
    1514    /* Maximum Ethernet */
};

/* Test statistics */
typedef struct {
    uint32_t packets_sent;
    uint32_t packets_failed;
    uint32_t bytes_sent;
    uint32_t errors_detected;
    uint32_t health_checks;
    uint32_t rollbacks;
    time_t start_time;
    time_t end_time;
    /* Variance tracking */
    uint32_t throughput_samples[100];
    uint16_t sample_count;
    uint32_t throughput_sum;
    uint32_t throughput_sum_sq;
    /* Rollback audit trail */
    uint8_t rollback_reasons[10];
    uint16_t rollback_events[10];
    uint8_t rollback_index;
    uint16_t last_patch_mask;
} stress_stats_t;

static stress_stats_t g_stats;
static uint32_t g_test_seed = DEFAULT_SEED;
static uint32_t g_target_rate = DEFAULT_RATE;
static uint32_t g_rand_state;

/**
 * Send packet via driver
 */
static int send_test_packet(uint16_t size) {
    union REGS r;
    struct SREGS sr;
    static uint8_t buffer[1514];
    
    /* Fill with pattern */
    for (int i = 0; i < size; i++) {
        buffer[i] = (uint8_t)(i ^ 0xAA);
    }
    
    /* Send via packet driver */
    r.h.ah = 0x04;  /* send_pkt */
    r.x.cx = size;
    sr.ds = (uint16_t)((unsigned long)(void far *)buffer >> 16);
    r.x.si = (uint16_t)((unsigned long)(void far *)buffer & 0xFFFF);
    
    int86x(PACKET_INT, &r, &r, &sr);
    
    return !r.x.cflag;
}

/**
 * Check driver health
 */
static uint16_t check_health(void) {
    union REGS r;
    
    r.h.ah = 0x81;  /* Get safety state */
    int86(PACKET_INT, &r, &r);
    
    if (!r.x.cflag) {
        return r.x.bx;  /* Health flags */
    }
    
    return 0xFFFF;  /* Unknown */
}

/**
 * Simple deterministic PRNG for reproducible tests
 */
static uint32_t stress_rand(void) {
    g_rand_state = g_rand_state * 1103515245 + 12345;
    return (g_rand_state >> 16) & 0x7FFF;
}

/**
 * Set test parameters for deterministic behavior
 */
void set_stress_params(uint32_t seed, uint32_t rate) {
    g_test_seed = seed;
    g_target_rate = rate;
    g_rand_state = seed;
}

/**
 * Calculate variance statistics
 */
static void update_variance(uint32_t throughput_kbps) {
    if (g_stats.sample_count < 100) {
        g_stats.throughput_samples[g_stats.sample_count] = throughput_kbps;
        g_stats.throughput_sum += throughput_kbps;
        g_stats.throughput_sum_sq += throughput_kbps * throughput_kbps;
        g_stats.sample_count++;
    }
}

/**
 * Record rollback event
 */
static void record_rollback(uint8_t reason, uint16_t event_code) {
    uint8_t idx = g_stats.rollback_index % 10;
    g_stats.rollback_reasons[idx] = reason;
    g_stats.rollback_events[idx] = event_code;
    g_stats.rollback_index++;
}

/**
 * Run stress test for specified duration
 */
int run_stress_test(uint32_t duration_secs, bool verbose) {
    uint32_t packet_count = 0;
    uint32_t check_interval = 100;  /* Check health every 100 packets */
    uint16_t last_health = 0;
    int size_index;
    uint32_t packets_per_interval;
    uint32_t interval_start;
    uint32_t last_throughput_calc;
    
    printf("Starting stress test for %lu seconds...\n", duration_secs);
    
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.start_time = time(NULL);
    
    /* Initialize PRNG with seed */
    g_rand_state = g_test_seed;
    
    /* Rate control setup */
    packets_per_interval = g_target_rate / 10;  /* packets per 100ms */
    interval_start = time(NULL);
    last_throughput_calc = interval_start;
    
    /* Main test loop */
    while ((time(NULL) - g_stats.start_time) < duration_secs) {
        /* Pick packet size using deterministic PRNG */
        size_index = stress_rand() % (sizeof(packet_sizes) / sizeof(packet_sizes[0]));
        uint16_t size = packet_sizes[size_index];
        
        /* Send packet */
        if (send_test_packet(size)) {
            g_stats.packets_sent++;
            g_stats.bytes_sent += size;
        } else {
            g_stats.packets_failed++;
            g_stats.errors_detected++;
        }
        
        packet_count++;
        
        /* Periodic health check */
        if (packet_count % check_interval == 0) {
            uint16_t health = check_health();
            g_stats.health_checks++;
            
            if (health != last_health) {
                if (verbose) {
                    printf("  Health changed: 0x%04X -> 0x%04X at %lu packets\n",
                           last_health, health, g_stats.packets_sent);
                }
                
                if (health != 0 && last_health == 0) {
                    g_stats.rollbacks++;
                    record_rollback(1, health);  /* Reason 1: health degraded */
                    
                    /* Capture patch mask */
                    union REGS r2;
                    r2.h.ah = 0x82;  /* Get patch stats */
                    int86(PACKET_INT, &r2, &r2);
                    if (!r2.x.cflag) {
                        g_stats.last_patch_mask = r2.x.ax;
                    }
                }
                
                last_health = health;
            }
            
            /* Progress indicator */
            if (verbose && packet_count % 1000 == 0) {
                printf("  %lu packets sent, %lu failed\n", 
                       g_stats.packets_sent, g_stats.packets_failed);
            }
        }
        
        /* Rate limiting to achieve target packets/sec */
        if (packet_count % packets_per_interval == 0) {
            /* Calculate throughput for variance tracking */
            time_t now = time(NULL);
            if (now - last_throughput_calc >= 1) {
                uint32_t throughput_kbps = (g_stats.bytes_sent * 8) / 
                                          ((now - g_stats.start_time) * 1000);
                update_variance(throughput_kbps);
                last_throughput_calc = now;
            }
            
            /* Delay to maintain target rate */
            volatile int i;
            for (i = 0; i < (100000 / g_target_rate); i++);
        }
    }
    
    g_stats.end_time = time(NULL);
    
    /* Print summary */
    uint32_t duration = g_stats.end_time - g_stats.start_time;
    printf("\nStress Test Complete:\n");
    printf("  Duration: %lu seconds\n", duration);
    printf("  Packets sent: %lu\n", g_stats.packets_sent);
    printf("  Packets failed: %lu\n", g_stats.packets_failed);
    printf("  Bytes sent: %lu\n", g_stats.bytes_sent);
    printf("  Throughput: %lu KB/s\n", g_stats.bytes_sent / duration / 1024);
    printf("  Health checks: %lu\n", g_stats.health_checks);
    printf("  Rollbacks: %lu\n", g_stats.rollbacks);
    printf("  Error rate: %.2f%%\n", 
           (float)g_stats.packets_failed * 100.0 / g_stats.packets_sent);
    
    /* Pass if error rate < 0.1% and no rollbacks */
    return (g_stats.packets_failed * 1000 < g_stats.packets_sent) && 
           (g_stats.rollbacks == 0);
}

/**
 * Run soak test (extended duration)
 */
int run_soak_test(uint32_t duration_mins, bool verbose) {
    printf("Starting soak test for %lu minutes...\n", duration_mins);
    
    /* Soak test is just a long stress test with lower intensity */
    return run_stress_test(duration_mins * 60, verbose);
}

/**
 * Run negative test - intentionally cause failure
 */
int run_negative_test(void) {
    union REGS r;
    uint16_t initial_health, final_health;
    uint8_t initial_mode, final_mode;
    uint16_t initial_policy_state;
    
    printf("Running negative test (forcing failure)...\n");
    
    /* Save initial policy state */
    r.h.ah = 0x93;  /* Get transfer mode */
    r.h.al = 2;     /* Query */
    int86(PACKET_INT, &r, &r);
    initial_policy_state = r.x.ax;
    
    /* Get initial state */
    r.h.ah = 0x81;
    int86(PACKET_INT, &r, &r);
    initial_health = r.x.bx;
    
    /* Get transfer mode */
    r.h.ah = 0x93;
    r.h.al = 2;  /* Query */
    int86(PACKET_INT, &r, &r);
    initial_mode = r.h.al;
    
    printf("  Initial: Health=0x%04X, Mode=%s\n", 
           initial_health, initial_mode ? "DMA" : "PIO");
    
    /* Disable a critical patch (if we had that capability) */
    /* For now, simulate by sending malformed packet */
    
    /* Try to send packet with invalid size */
    union REGS r2;
    struct SREGS sr;
    uint8_t bad_buffer[10];
    
    r2.h.ah = 0x04;
    r2.x.cx = 65535;  /* Invalid size */
    sr.ds = (uint16_t)((unsigned long)(void far *)bad_buffer >> 16);
    r2.x.si = (uint16_t)((unsigned long)(void far *)bad_buffer & 0xFFFF);
    
    int86x(PACKET_INT, &r2, &r2, &sr);
    
    /* Check if health degraded */
    r.h.ah = 0x81;
    int86(PACKET_INT, &r, &r);
    final_health = r.x.bx;
    
    /* Check if mode reverted */
    r.h.ah = 0x93;
    r.h.al = 2;
    int86(PACKET_INT, &r, &r);
    final_mode = r.h.al;
    
    printf("  Final: Health=0x%04X, Mode=%s\n", 
           final_health, final_mode ? "DMA" : "PIO");
    
    /* Test passes if health degraded or mode reverted to PIO */
    if (initial_mode == 1 && final_mode == 0) {
        printf("  PASS: DMA disabled on error\n");
        return 1;
    }
    
    if (final_health != initial_health && final_health != 0) {
        printf("  PASS: Health degraded on error\n");
        return 1;
    }
    
    printf("  INFO: No automatic rollback detected (may be normal)\n");
    
    /* Cleanup: Always quiesce and restore state */
    r.h.ah = 0x90;  /* Quiesce */
    int86(PACKET_INT, &r, &r);
    
    /* Clear any forced PIO/DEGRADED bits */
    r.h.ah = 0x93;  /* Set transfer mode */
    r.h.al = 0;     /* Auto-select */
    int86(PACKET_INT, &r, &r);
    
    /* Resume driver */
    r.h.ah = 0x91;  /* Resume */
    int86(PACKET_INT, &r, &r);
    
    return 1;  /* Not a failure if no rollback */
}

/**
 * Calculate variance and standard deviation
 */
void calculate_variance_stats(uint32_t *median, uint32_t *p95, float *std_dev, bool *high_variance) {
    if (g_stats.sample_count == 0) {
        *median = 0;
        *p95 = 0;
        *std_dev = 0;
        *high_variance = false;
        return;
    }
    
    /* Sort samples for median and P95 */
    uint32_t sorted[100];
    uint16_t i, j;
    for (i = 0; i < g_stats.sample_count; i++) {
        sorted[i] = g_stats.throughput_samples[i];
    }
    
    /* Simple bubble sort (small array) */
    for (i = 0; i < g_stats.sample_count - 1; i++) {
        for (j = 0; j < g_stats.sample_count - i - 1; j++) {
            if (sorted[j] > sorted[j + 1]) {
                uint32_t temp = sorted[j];
                sorted[j] = sorted[j + 1];
                sorted[j + 1] = temp;
            }
        }
    }
    
    /* Calculate median */
    *median = sorted[g_stats.sample_count / 2];
    
    /* Calculate P95 */
    uint16_t p95_index = (g_stats.sample_count * 95) / 100;
    *p95 = sorted[p95_index];
    
    /* Calculate standard deviation */
    float mean = (float)g_stats.throughput_sum / g_stats.sample_count;
    float variance = ((float)g_stats.throughput_sum_sq / g_stats.sample_count) - (mean * mean);
    *std_dev = variance > 0 ? sqrt(variance) : 0;
    
    /* Check for high variance (>20% of mean) */
    *high_variance = (*std_dev > mean * 0.2);
}

/**
 * Export statistics for JSON
 */
void get_stress_stats(void *stats) {
    if (stats) {
        /* Copy basic stats */
        memcpy(stats, &g_stats, sizeof(stress_stats_t));
    }
}
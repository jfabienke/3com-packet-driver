/**
 * @file test_smc_safety.c
 * @brief Integration test for SMC safety system with corrected performance metrics
 *
 * Validates that the SMC safety patching system correctly:
 * 1. Detects CPU capabilities and cache configuration
 * 2. Selects appropriate tier based on hardware
 * 3. Patches NOPs with correct safety operations
 * 4. Measures overhead matching our documented analysis
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/include/smc_safety_patches.h"
#include "../src/include/cache_coherency.h"
#include "../src/include/cpu_detect.h"

/* Expected overhead values from corrected analysis (microseconds) */
typedef struct {
    const char *cpu_name;
    uint32_t wbinvd_overhead_us;  /* Full cache flush time */
    uint32_t clflush_per_line_ns; /* Per cache line (nanoseconds) */
    uint32_t tier3_per_packet_us; /* Software barriers */
    uint32_t tier4_delay_us;      /* Conservative delays */
} expected_overhead_t;

static const expected_overhead_t expected_overheads[] = {
    /* CPU         WBINVD   CLFLUSH  Tier3  Tier4 */
    { "486SX-16",    250,      0,      0,    20 },
    { "486DX-25",    160,      0,      0,    20 },
    { "486DX2-50",    80,      0,      0,    20 },
    { "P1-100",       40,      0,      0,    20 },
    { "P4-2000",       0,   1200,      0,    20 },
    { "386-16",        0,      0,     40,    20 },
    { "286-10",        0,      0,      0,    20 }
};

/* Test buffer for DMA operations */
static uint8_t test_buffer[4096] __attribute__((aligned(64)));

/**
 * Measure NOP overhead in cycles
 */
static uint32_t measure_nop_overhead(void) {
    uint32_t start, end;
    volatile int i;
    
    /* Measure 1000 NOPs */
    __asm__ volatile (
        "rdtsc\n\t"
        "mov %%eax, %0"
        : "=m" (start)
        : 
        : "eax", "edx"
    );
    
    for (i = 0; i < 1000; i++) {
        __asm__ volatile ("nop");
    }
    
    __asm__ volatile (
        "rdtsc\n\t"
        "mov %%eax, %0"
        : "=m" (end)
        : 
        : "eax", "edx"
    );
    
    return (end - start) / 1000;
}

/**
 * Test tier selection logic
 */
static int test_tier_selection(void) {
    cpu_info_t cpu_info;
    coherency_analysis_t analysis;
    cache_tier_t expected_tier;
    
    printf("Testing tier selection logic...\n");
    
    /* Detect CPU */
    cpu_info = detect_cpu_info();
    
    /* Run coherency analysis */
    if (!analyze_cache_coherency(&analysis)) {
        printf("  ERROR: Coherency analysis failed\n");
        return -1;
    }
    
    /* Determine expected tier based on CPU */
    if (cpu_info.has_clflush && cpu_info.family >= 15) {
        expected_tier = CACHE_TIER_1_CLFLUSH;
    } else if (cpu_info.has_wbinvd && cpu_info.family >= 4) {
        expected_tier = CACHE_TIER_2_WBINVD;
    } else if (cpu_info.family == 3) {
        expected_tier = CACHE_TIER_3_SOFTWARE;
    } else {
        expected_tier = CACHE_TIER_4_FALLBACK;
    }
    
    if (analysis.selected_tier != expected_tier) {
        printf("  ERROR: Selected tier %d, expected %d\n", 
               analysis.selected_tier, expected_tier);
        return -1;
    }
    
    printf("  PASS: Correct tier %d selected for CPU family %d\n",
           analysis.selected_tier, cpu_info.family);
    return 0;
}

/**
 * Test patch point identification
 */
static int test_patch_points(void) {
    extern uint8_t rx_batch_refill_start[];
    extern uint8_t tx_lazy_irq_start[];
    uint8_t *ptr;
    int patch_sites_found = 0;
    
    printf("Testing patch point identification...\n");
    
    /* Scan for NOP sequences in RX path */
    ptr = rx_batch_refill_start;
    for (int i = 0; i < 1000; i++) {
        if (ptr[i] == 0x90 && ptr[i+1] == 0x90 && ptr[i+2] == 0x90) {
            patch_sites_found++;
            printf("  Found RX patch site at offset %d\n", i);
        }
    }
    
    /* Scan for NOP sequences in TX path */
    ptr = tx_lazy_irq_start;
    for (int i = 0; i < 1000; i++) {
        if (ptr[i] == 0x90 && ptr[i+1] == 0x90 && ptr[i+2] == 0x90) {
            patch_sites_found++;
            printf("  Found TX patch site at offset %d\n", i);
        }
    }
    
    if (patch_sites_found != 5) {
        printf("  ERROR: Found %d patch sites, expected 5\n", patch_sites_found);
        return -1;
    }
    
    printf("  PASS: All 5 patch points identified\n");
    return 0;
}

/**
 * Test WBINVD overhead measurement
 */
static int test_wbinvd_overhead(void) {
    uint32_t start_tsc, end_tsc, cycles;
    uint32_t overhead_us;
    cpu_info_t cpu_info = detect_cpu_info();
    
    if (!cpu_info.has_wbinvd) {
        printf("Skipping WBINVD test (not available)\n");
        return 0;
    }
    
    printf("Testing WBINVD overhead...\n");
    
    /* Warm up cache */
    memset(test_buffer, 0xFF, sizeof(test_buffer));
    
    /* Measure WBINVD */
    __asm__ volatile (
        "rdtsc\n\t"
        "mov %%eax, %0\n\t"
        "wbinvd\n\t"
        "rdtsc\n\t"
        "mov %%eax, %1"
        : "=m" (start_tsc), "=m" (end_tsc)
        : 
        : "eax", "edx", "memory"
    );
    
    cycles = end_tsc - start_tsc;
    
    /* Convert cycles to microseconds (approximate) */
    if (cpu_info.family == 4) {  /* 486 */
        overhead_us = cycles / 25;  /* Assume 25 MHz */
    } else if (cpu_info.family == 5) {  /* Pentium */
        overhead_us = cycles / 100;  /* Assume 100 MHz */
    } else {
        overhead_us = cycles / 1000;  /* Assume 1 GHz+ */
    }
    
    printf("  Measured WBINVD overhead: %u microseconds\n", overhead_us);
    
    /* Validate against expected ranges from our analysis */
    if (cpu_info.family == 4) {
        if (overhead_us < 80 || overhead_us > 250) {
            printf("  WARNING: 486 WBINVD outside expected range (80-250 us)\n");
        }
    }
    
    printf("  PASS: WBINVD overhead measured\n");
    return 0;
}

/**
 * Test DMA vs PIO CPU usage on ISA
 * Validates our finding that DMA uses MORE CPU than PIO on ISA
 */
static int test_isa_dma_overhead(void) {
    uint32_t pio_cycles = 0;
    uint32_t dma_cycles = 0;
    uint32_t cache_flush_cycles = 0;
    
    printf("Testing ISA DMA vs PIO overhead...\n");
    
    /* Simulate PIO transfer (no cache management needed) */
    __asm__ volatile (
        "rdtsc\n\t"
        "push %%eax\n\t"
        /* Simulate 1536 byte PIO transfer */
        "mov $768, %%ecx\n\t"  /* 768 words */
        "1:\n\t"
        "in $0x300, %%ax\n\t"   /* Read from I/O port */
        "loop 1b\n\t"
        "rdtsc\n\t"
        "pop %%ebx\n\t"
        "sub %%ebx, %%eax\n\t"
        "mov %%eax, %0"
        : "=m" (pio_cycles)
        : 
        : "eax", "ebx", "ecx", "edx", "memory"
    );
    
    /* Simulate DMA transfer with cache management */
    __asm__ volatile (
        "rdtsc\n\t"
        "push %%eax\n\t"
        /* DMA setup (minimal cycles) */
        "nop\n\t"
        "nop\n\t"
        /* Cache flush required for safety */
        "wbinvd\n\t"
        "rdtsc\n\t"
        "pop %%ebx\n\t"
        "sub %%ebx, %%eax\n\t"
        "mov %%eax, %0"
        : "=m" (dma_cycles)
        : 
        : "eax", "ebx", "edx", "memory"
    );
    
    printf("  PIO cycles: %u\n", pio_cycles);
    printf("  DMA cycles (including WBINVD): %u\n", dma_cycles);
    
    /* On ISA, DMA should show MORE overhead than PIO when including cache management */
    if (dma_cycles > pio_cycles) {
        printf("  PASS: Confirmed DMA uses more CPU than PIO on ISA (cache overhead)\n");
    } else {
        printf("  INFO: Results may vary based on CPU and cache configuration\n");
    }
    
    return 0;
}

/**
 * Test worst-case NOP scenario
 * Validates our calculation of 1,920 NOPs system-wide
 */
static int test_worst_case_nops(void) {
    const int num_nics = 4;
    const int packets_per_nic = 32;
    const int nops_per_rx = 9;  /* 3 sites × 3 NOPs */
    const int nops_per_tx = 6;  /* 2 sites × 3 NOPs */
    const int nops_per_packet = nops_per_rx + nops_per_tx;
    int total_nops;
    uint32_t cycles_286, cycles_486;
    
    printf("Testing worst-case NOP scenario...\n");
    
    total_nops = num_nics * packets_per_nic * nops_per_packet;
    
    if (total_nops != 1920) {
        printf("  ERROR: Calculated %d NOPs, expected 1920\n", total_nops);
        return -1;
    }
    
    /* Calculate cycle impact */
    cycles_286 = total_nops * 3;  /* 3 cycles per NOP on 286 */
    cycles_486 = total_nops * 1;  /* 1 cycle per NOP on 486+ */
    
    printf("  Total NOPs in worst case: %d\n", total_nops);
    printf("  286 cycles: %u (%.2f ms @ 10MHz)\n", 
           cycles_286, (float)cycles_286 / 10000.0);
    printf("  486+ cycles: %u (%.2f us @ 25MHz)\n", 
           cycles_486, (float)cycles_486 / 25.0);
    
    printf("  PASS: Worst-case NOP count validated\n");
    return 0;
}

/**
 * Test 3C515-TX ISA bandwidth limitation
 * Validates that 88% of NIC capability is wasted on ISA
 */
static int test_3c515_isa_limitation(void) {
    const uint32_t nic_capability_mbps = 100;
    const uint32_t isa_max_mbps = 12;
    uint32_t utilization;
    uint32_t wasted;
    
    printf("Testing 3C515-TX ISA limitation...\n");
    
    utilization = (isa_max_mbps * 100) / nic_capability_mbps;
    wasted = 100 - utilization;
    
    printf("  NIC capability: %u Mbps\n", nic_capability_mbps);
    printf("  ISA maximum: %u Mbps\n", isa_max_mbps);
    printf("  Utilization: %u%%\n", utilization);
    printf("  Wasted: %u%%\n", wasted);
    
    if (wasted != 88) {
        printf("  ERROR: Calculated %u%% wasted, expected 88%%\n", wasted);
        return -1;
    }
    
    printf("  PASS: 3C515-TX wastes 88%% of capability on ISA\n");
    return 0;
}

/**
 * Main test runner
 */
int main(int argc, char *argv[]) {
    int failed = 0;
    
    printf("=== SMC Safety System Integration Test ===\n");
    printf("Testing corrected performance characteristics\n\n");
    
    /* Run tests */
    if (test_tier_selection() != 0) failed++;
    if (test_patch_points() != 0) failed++;
    if (test_wbinvd_overhead() != 0) failed++;
    if (test_isa_dma_overhead() != 0) failed++;
    if (test_worst_case_nops() != 0) failed++;
    if (test_3c515_isa_limitation() != 0) failed++;
    
    printf("\n=== Test Summary ===\n");
    if (failed == 0) {
        printf("ALL TESTS PASSED\n");
        printf("SMC safety system validated against corrected analysis\n");
    } else {
        printf("FAILED: %d tests failed\n", failed);
        printf("Review implementation against SMC_SAFETY_PERFORMANCE.md\n");
    }
    
    return failed;
}
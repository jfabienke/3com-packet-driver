# Bus Mastering Capability Testing Framework

## Overview

This document describes the implementation of an automated bus mastering capability testing system for 80286 and later systems. The framework performs comprehensive testing to determine bus mastering compatibility and provides a confidence score for safe operation.

## Testing Framework Architecture

### Test Phases

```
┌─────────────────────────────────────────────────────────────────┐
│                 Bus Mastering Test Framework                    │
├─────────────────┬─────────────────┬─────────────────────────────┤
│   Phase 1:      │   Phase 2:      │   Phase 3:                 │
│   Basic Tests   │   Stress Tests  │   Stability Tests          │
│                 │                 │                            │
│ • DMA Detection │ • Pattern Tests │ • Long Duration            │
│ • Memory Access │ • Burst Timing  │ • Thermal Stress           │
│ • Timing Tests  │ • Error Recovery│ • Error Rate Analysis      │
└─────────────────┴─────────────────┴─────────────────────────────┘
```

### Implementation Strategy

The testing framework is integrated into the driver initialization process:

```c
// Driver initialization sequence
int driver_init(void) {
    // Standard initialization
    if (detect_hardware() != SUCCESS) return FAILURE;
    if (detect_cpu() < CPU_286) return CPU_NOT_SUPPORTED;
    
    // Bus mastering capability testing
    if (config.busmaster_enabled) {
        busmaster_confidence_t confidence = test_busmaster_capability();
        
        switch (confidence.level) {
            case BM_CONFIDENCE_HIGH:
                enable_busmaster_full();
                break;
            case BM_CONFIDENCE_MEDIUM:
                enable_busmaster_conservative();
                break;
            case BM_CONFIDENCE_LOW:
                log_warning("Bus mastering unreliable - using programmed I/O");
                disable_busmaster();
                break;
            case BM_CONFIDENCE_FAILED:
                log_error("Bus mastering incompatible - disabled");
                disable_busmaster();
                break;
        }
    }
    
    return SUCCESS;
}
```

## Phase 1: Basic Capability Tests

### DMA Controller Detection

```c
typedef struct {
    uint8_t dma_channels_available;
    uint8_t dma_controller_type;    // 8237, 82374, etc.
    bool supports_16bit_transfers;
    bool supports_burst_mode;
    uint16_t max_transfer_size;
} dma_capability_t;

int test_dma_controller(dma_capability_t *caps) {
    int score = 0;
    
    // Test 1: Basic DMA channel availability
    for (int channel = 0; channel < 8; channel++) {
        if (test_dma_channel(channel)) {
            caps->dma_channels_available |= (1 << channel);
            score += 10;
        }
    }
    
    // Test 2: 16-bit transfer capability
    if (test_16bit_dma_transfer()) {
        caps->supports_16bit_transfers = true;
        score += 20;
    }
    
    // Test 3: Burst mode support
    if (test_dma_burst_mode()) {
        caps->supports_burst_mode = true;
        score += 25;
    }
    
    // Test 4: Maximum transfer size
    caps->max_transfer_size = determine_max_transfer_size();
    if (caps->max_transfer_size >= 1518) score += 15;  // Full Ethernet frame
    
    return score;  // Maximum: 70 points
}
```

### Memory Coherency Testing

```c
int test_memory_coherency(void) {
    int score = 0;
    uint8_t test_patterns[] = {0x00, 0xFF, 0xAA, 0x55, 0xCC, 0x33};
    
    // Allocate test buffers in different memory regions
    void *conventional_buf = malloc_conventional(TEST_BUFFER_SIZE);
    void *xms_buf = malloc_xms(TEST_BUFFER_SIZE);
    void *umb_buf = malloc_umb(TEST_BUFFER_SIZE);
    
    for (int pattern_idx = 0; pattern_idx < 6; pattern_idx++) {
        uint8_t pattern = test_patterns[pattern_idx];
        
        // Test conventional memory coherency
        if (test_memory_coherency_pattern(conventional_buf, pattern)) {
            score += 5;
        }
        
        // Test XMS memory coherency (if available)
        if (xms_buf && test_memory_coherency_pattern(xms_buf, pattern)) {
            score += 8;
        }
        
        // Test UMB coherency (if available)
        if (umb_buf && test_memory_coherency_pattern(umb_buf, pattern)) {
            score += 7;
        }
    }
    
    free_test_buffers(conventional_buf, xms_buf, umb_buf);
    return score;  // Maximum: 120 points
}

bool test_memory_coherency_pattern(void *buffer, uint8_t pattern) {
    // Fill buffer with pattern using CPU
    memset(buffer, pattern, TEST_BUFFER_SIZE);
    
    // Set up DMA to modify the buffer
    setup_dma_transfer(buffer, TEST_BUFFER_SIZE, DMA_MODE_WRITE);
    
    // Start DMA transfer with inverted pattern
    uint8_t dma_pattern = ~pattern;
    start_dma_pattern_fill(dma_pattern);
    
    // Wait for completion
    if (!wait_dma_completion(100)) return false;  // 100ms timeout
    
    // Verify memory contents
    for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
        if (((uint8_t*)buffer)[i] != dma_pattern) {
            return false;  // Coherency failure
        }
    }
    
    return true;
}
```

### Timing Accuracy Testing

```c
typedef struct {
    uint32_t min_latency_us;
    uint32_t max_latency_us;
    uint32_t avg_latency_us;
    uint16_t timing_variance;
    bool timing_stable;
} timing_test_results_t;

int test_dma_timing(timing_test_results_t *results) {
    int score = 0;
    uint32_t latencies[100];
    
    // Perform 100 timing measurements
    for (int i = 0; i < 100; i++) {
        uint32_t start_time = get_high_resolution_timer();
        
        // Perform a small DMA transfer
        setup_dma_transfer(test_buffer, 64, DMA_MODE_READ);
        start_dma_transfer();
        wait_dma_completion(50);  // 50ms timeout
        
        uint32_t end_time = get_high_resolution_timer();
        latencies[i] = end_time - start_time;
    }
    
    // Calculate statistics
    calculate_timing_statistics(latencies, 100, results);
    
    // Score based on timing characteristics
    if (results->max_latency_us < 1000) score += 20;      // < 1ms max latency
    if (results->avg_latency_us < 500) score += 15;       // < 500µs average
    if (results->timing_variance < 100) score += 15;      // Low variance
    if (results->timing_stable) score += 10;              // Stable timing
    
    return score;  // Maximum: 60 points
}
```

## Phase 2: Stress Testing

### Pattern-Based Data Integrity

```c
int test_data_integrity_patterns(void) {
    int score = 0;
    
    struct test_pattern {
        char *name;
        void (*generator)(uint8_t *buffer, size_t size);
        int points;
    } patterns[] = {
        {"Walking 1s", generate_walking_ones, 15},
        {"Walking 0s", generate_walking_zeros, 15},
        {"Checkerboard", generate_checkerboard, 10},
        {"Random", generate_random_pattern, 20},
        {"Address-in-Address", generate_address_pattern, 25}
    };
    
    for (int p = 0; p < 5; p++) {
        uint8_t *test_buffer = allocate_dma_buffer(4096);
        uint8_t *verify_buffer = malloc(4096);
        
        // Generate test pattern
        patterns[p].generator(test_buffer, 4096);
        memcpy(verify_buffer, test_buffer, 4096);
        
        // Perform DMA transfer to NIC and back
        bool success = true;
        for (int iteration = 0; iteration < 10; iteration++) {
            if (!dma_transfer_test(test_buffer, 4096)) {
                success = false;
                break;
            }
            
            // Verify data integrity
            if (memcmp(test_buffer, verify_buffer, 4096) != 0) {
                success = false;
                break;
            }
        }
        
        if (success) {
            score += patterns[p].points;
            log_debug("Pattern test '%s' passed", patterns[p].name);
        } else {
            log_warning("Pattern test '%s' failed", patterns[p].name);
        }
        
        free(test_buffer);
        free(verify_buffer);
    }
    
    return score;  // Maximum: 85 points
}
```

### Burst Transfer Testing

```c
int test_burst_transfers(void) {
    int score = 0;
    size_t burst_sizes[] = {64, 128, 256, 512, 1024, 1518};  // Including max Ethernet frame
    
    for (int i = 0; i < 6; i++) {
        size_t burst_size = burst_sizes[i];
        bool burst_success = true;
        
        for (int test = 0; test < 20; test++) {
            uint8_t *buffer = allocate_dma_buffer(burst_size);
            fill_test_pattern(buffer, burst_size, test);
            
            // Perform burst DMA transfer
            uint32_t start_time = get_timer();
            setup_dma_burst_transfer(buffer, burst_size);
            
            if (!wait_dma_completion(burst_size / 10 + 10)) {  // Adaptive timeout
                burst_success = false;
                break;
            }
            
            uint32_t transfer_time = get_timer() - start_time;
            
            // Verify data integrity
            if (!verify_test_pattern(buffer, burst_size, test)) {
                burst_success = false;
                break;
            }
            
            // Check transfer rate
            uint32_t transfer_rate = (burst_size * 1000) / transfer_time;  // KB/s
            if (transfer_rate < (burst_size / 10)) {  // Minimum acceptable rate
                burst_success = false;
                break;
            }
            
            free(buffer);
        }
        
        if (burst_success) {
            score += (10 + i * 2);  // Increasing points for larger bursts
            log_debug("Burst test %zu bytes passed", burst_size);
        } else {
            log_warning("Burst test %zu bytes failed", burst_size);
            break;  // Stop testing larger bursts if smaller ones fail
        }
    }
    
    return score;  // Maximum: 82 points
}
```

### Error Recovery Testing

```c
int test_error_recovery(void) {
    int score = 0;
    
    // Test 1: DMA timeout recovery
    if (test_dma_timeout_recovery()) {
        score += 25;
    }
    
    // Test 2: Bus arbitration conflict recovery
    if (test_bus_arbitration_recovery()) {
        score += 20;
    }
    
    // Test 3: Memory parity error recovery
    if (test_memory_error_recovery()) {
        score += 15;
    }
    
    // Test 4: Transfer abort and restart
    if (test_transfer_abort_recovery()) {
        score += 15;
    }
    
    // Test 5: ISA bus timeout recovery
    if (test_isa_timeout_recovery()) {
        score += 10;
    }
    
    return score;  // Maximum: 85 points
}

bool test_dma_timeout_recovery(void) {
    // Simulate DMA timeout by setting very short timeout
    set_dma_timeout(1);  // 1ms - guaranteed to timeout
    
    uint8_t *buffer = allocate_dma_buffer(1518);
    setup_dma_transfer(buffer, 1518, DMA_MODE_READ);
    
    bool timeout_occurred = !wait_dma_completion(2);
    if (!timeout_occurred) {
        free(buffer);
        return false;  // Should have timed out
    }
    
    // Test recovery
    abort_dma_transfer();
    reset_dma_controller();
    
    // Restore normal timeout and retry
    set_dma_timeout(100);  // 100ms
    setup_dma_transfer(buffer, 64, DMA_MODE_READ);  // Smaller transfer
    
    bool recovery_success = wait_dma_completion(50);
    free(buffer);
    
    return recovery_success;
}
```

## Phase 3: Stability Testing

### Long Duration Testing

```c
typedef struct {
    uint32_t total_transfers;
    uint32_t successful_transfers;
    uint32_t timeout_errors;
    uint32_t data_errors;
    uint32_t dma_errors;
    uint32_t test_duration_ms;
    float success_rate;
} stability_test_results_t;

int test_long_duration_stability(stability_test_results_t *results) {
    int score = 0;
    uint32_t start_time = get_system_time();
    uint32_t test_duration = 30000;  // 30 seconds
    
    memset(results, 0, sizeof(stability_test_results_t));
    
    while ((get_system_time() - start_time) < test_duration) {
        // Vary transfer sizes randomly
        size_t transfer_size = 64 + (rand() % 1454);  // 64 to 1518 bytes
        uint8_t *buffer = allocate_dma_buffer(transfer_size);
        
        fill_random_pattern(buffer, transfer_size);
        uint8_t *original = malloc(transfer_size);
        memcpy(original, buffer, transfer_size);
        
        results->total_transfers++;
        
        // Perform DMA transfer
        setup_dma_transfer(buffer, transfer_size, DMA_MODE_READWRITE);
        
        if (wait_dma_completion(100)) {
            // Verify data integrity
            if (memcmp(buffer, original, transfer_size) == 0) {
                results->successful_transfers++;
            } else {
                results->data_errors++;
            }
        } else {
            results->timeout_errors++;
        }
        
        free(buffer);
        free(original);
        
        // Small delay to prevent overwhelming the system
        delay_ms(10);
    }
    
    results->test_duration_ms = get_system_time() - start_time;
    results->success_rate = (float)results->successful_transfers / results->total_transfers;
    
    // Scoring based on success rate
    if (results->success_rate >= 0.99) score += 50;      // 99%+ success
    else if (results->success_rate >= 0.95) score += 40; // 95%+ success
    else if (results->success_rate >= 0.90) score += 25; // 90%+ success
    else if (results->success_rate >= 0.80) score += 10; // 80%+ success
    
    return score;  // Maximum: 50 points
}
```

## Confidence Scoring System

### Score Calculation

```c
typedef enum {
    BM_CONFIDENCE_FAILED = 0,    // 0-199 points
    BM_CONFIDENCE_LOW = 1,       // 200-299 points
    BM_CONFIDENCE_MEDIUM = 2,    // 300-399 points
    BM_CONFIDENCE_HIGH = 3       // 400+ points
} busmaster_confidence_level_t;

typedef struct {
    busmaster_confidence_level_t level;
    uint16_t total_score;
    uint16_t max_possible_score;
    float confidence_percentage;
    
    // Individual test scores
    uint16_t dma_controller_score;
    uint16_t memory_coherency_score;
    uint16_t timing_score;
    uint16_t data_integrity_score;
    uint16_t burst_transfer_score;
    uint16_t error_recovery_score;
    uint16_t stability_score;
    
    // Failure reasons (if applicable)
    char failure_reason[256];
    bool fatal_error_detected;
} busmaster_confidence_t;

busmaster_confidence_t test_busmaster_capability(void) {
    busmaster_confidence_t confidence = {0};
    
    log_info("Starting bus mastering capability test...");
    
    // Phase 1: Basic Tests
    confidence.dma_controller_score = test_dma_controller(&dma_caps);
    confidence.memory_coherency_score = test_memory_coherency();
    confidence.timing_score = test_dma_timing(&timing_results);
    
    // Check for fatal errors in Phase 1
    if (confidence.dma_controller_score == 0) {
        strcpy(confidence.failure_reason, "DMA controller not functional");
        confidence.fatal_error_detected = true;
        confidence.level = BM_CONFIDENCE_FAILED;
        return confidence;
    }
    
    // Phase 2: Stress Tests
    confidence.data_integrity_score = test_data_integrity_patterns();
    confidence.burst_transfer_score = test_burst_transfers();
    confidence.error_recovery_score = test_error_recovery();
    
    // Check for critical failures in Phase 2
    if (confidence.data_integrity_score < 20) {
        strcpy(confidence.failure_reason, "Data integrity failures detected");
        confidence.fatal_error_detected = true;
        confidence.level = BM_CONFIDENCE_FAILED;
        return confidence;
    }
    
    // Phase 3: Stability Test
    stability_test_results_t stability_results;
    confidence.stability_score = test_long_duration_stability(&stability_results);
    
    // Calculate total score
    confidence.total_score = confidence.dma_controller_score +
                           confidence.memory_coherency_score +
                           confidence.timing_score +
                           confidence.data_integrity_score +
                           confidence.burst_transfer_score +
                           confidence.error_recovery_score +
                           confidence.stability_score;
    
    confidence.max_possible_score = 452;  // Sum of all maximum scores
    confidence.confidence_percentage = (float)confidence.total_score / confidence.max_possible_score * 100;
    
    // Determine confidence level
    if (confidence.total_score >= 400) {
        confidence.level = BM_CONFIDENCE_HIGH;
    } else if (confidence.total_score >= 300) {
        confidence.level = BM_CONFIDENCE_MEDIUM;
    } else if (confidence.total_score >= 200) {
        confidence.level = BM_CONFIDENCE_LOW;
    } else {
        confidence.level = BM_CONFIDENCE_FAILED;
    }
    
    log_info("Bus mastering test complete: %s (%.1f%%, %d/%d points)",
             get_confidence_level_name(confidence.level),
             confidence.confidence_percentage,
             confidence.total_score,
             confidence.max_possible_score);
    
    return confidence;
}
```

## User Interface Integration

### Configuration Parameter

```dos
REM Enable automatic bus mastering capability testing
DEVICE=3CPD.COM /BUSMASTER=AUTO /BM_TEST=FULL
DEVICE=3CPD.COM /BUSMASTER=AUTO /BM_TEST=QUICK  
DEVICE=3CPD.COM /BUSMASTER=AUTO /BM_TEST=OFF
```

### Runtime Commands

```dos
REM Test bus mastering capability manually
3CPD /TEST_BUSMASTER /VERBOSE

REM Show last test results
3CPD /SHOW_BM_TEST

REM Force retest
3CPD /RETEST_BUSMASTER
```

### Test Results Display

```
3Com Packet Driver - Bus Mastering Capability Test Results
=========================================================

System: 80286 with Intel 82C206 chipset
Test Date: 1995-03-15 14:30:22

Phase 1 - Basic Capability Tests:
  DMA Controller:     45/70    (64%)  ✓ PASS
  Memory Coherency:   85/120   (71%)  ✓ PASS  
  Timing Tests:       35/60    (58%)  ⚠ WARN

Phase 2 - Stress Tests:
  Data Integrity:     65/85    (76%)  ✓ PASS
  Burst Transfers:    40/82    (49%)  ⚠ WARN
  Error Recovery:     70/85    (82%)  ✓ PASS

Phase 3 - Stability Test:
  Long Duration:      25/50    (50%)  ⚠ WARN
  Success Rate:       92.3%

Overall Score: 365/452 (80.7%)
Confidence Level: MEDIUM

Recommendation: Bus mastering can be used with conservative settings.
                Enable /BUSMASTER_COMPAT=286 /BM_TIMEOUT=150 /BM_VERIFY=ON

Warning: Burst transfers >512 bytes show timing issues.
         Consider /BM_MAX_BURST=512 for optimal stability.
```

## Implementation Notes

### Integration Points

1. **Driver Initialization**: Automatic testing during startup if `/BUSMASTER=AUTO`
2. **Manual Testing**: User-initiated testing via runtime commands
3. **Periodic Retesting**: Optional periodic retesting during operation
4. **Configuration Adaptation**: Automatic parameter adjustment based on test results

### Performance Impact

- **Full Test**: ~45 seconds during initialization
- **Quick Test**: ~10 seconds (reduced stability testing)
- **Runtime Impact**: <1% when not testing
- **Memory Usage**: ~8KB for test buffers

### Safety Considerations

- **Conservative Defaults**: Test failures result in programmed I/O mode
- **Non-Destructive**: All tests preserve existing data
- **Timeout Protection**: All tests have timeout limits
- **Graceful Degradation**: Partial failures still allow limited bus mastering

This comprehensive testing framework provides objective, automated assessment of bus mastering capabilities, allowing users to safely determine optimal configuration for their specific hardware combination.
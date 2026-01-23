/**
 * @file health_diagnostics.h
 * @brief Health diagnostics integration header
 * 
 * GPT-5 Stage 2: Lightweight data collection for hot path integration
 * Provides minimal overhead macros and functions for health monitoring
 */

#ifndef _HEALTH_DIAGNOSTICS_H_
#define _HEALTH_DIAGNOSTICS_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Error counter categories (must match memory_layout.asm) */
#define HEALTH_ERROR_TX         0
#define HEALTH_ERROR_RX         1
#define HEALTH_ERROR_DMA        2
#define HEALTH_ERROR_MEMORY     3
#define HEALTH_ERROR_HARDWARE   4
#define HEALTH_ERROR_API        5
#define HEALTH_ERROR_BUFFER     6
#define HEALTH_ERROR_TIMEOUT    7
#define HEALTH_ERROR_CATEGORIES 8

/* Performance metric indices (must match memory_layout.asm) */
#define HEALTH_METRIC_TX_RATE_BASE    0    /* [0-3] TX rates per NIC */
#define HEALTH_METRIC_RX_RATE_BASE    4    /* [4-7] RX rates per NIC */
#define HEALTH_METRIC_BUFFER_BASE     8    /* [8-11] Buffer utilization per NIC */
#define HEALTH_METRIC_CPU_UTIL        12   /* CPU utilization estimate */
#define HEALTH_METRIC_MEMORY_PRESSURE 13   /* Memory pressure indicator */
#define HEALTH_METRIC_ISR_FREQUENCY   14   /* ISR frequency */
#define HEALTH_METRIC_API_FREQUENCY   15   /* API call frequency */
#define HEALTH_METRIC_COUNT           16

/* External data from memory_layout.asm */
extern unsigned long health_error_counters[HEALTH_ERROR_CATEGORIES];
extern unsigned short health_performance_metrics[HEALTH_METRIC_COUNT];
extern unsigned long health_last_diagnostic_time;
extern unsigned short health_diagnostic_flags;

/* Ultra-lightweight inline functions for hot path */

/**
 * @brief Increment error counter (minimal overhead)
 * @param category Error category (HEALTH_ERROR_*)
 */
#define HEALTH_INC_ERROR(category) \
    do { if ((category) < HEALTH_ERROR_CATEGORIES) health_error_counters[category]++; } while(0)

/**
 * @brief Update performance metric (minimal overhead)
 * @param index Metric index (HEALTH_METRIC_*)
 * @param value New value
 */
#define HEALTH_SET_METRIC(index, value) \
    do { if ((index) < HEALTH_METRIC_COUNT) health_performance_metrics[index] = (value); } while(0)

/**
 * @brief Increment performance metric (minimal overhead)
 * @param index Metric index (HEALTH_METRIC_*)
 */
#define HEALTH_INC_METRIC(index) \
    do { if ((index) < HEALTH_METRIC_COUNT) health_performance_metrics[index]++; } while(0)

/* Inline assembly versions for maximum performance */
#ifdef __WATCOMC__

/**
 * @brief Fast error counter increment (assembly inline)
 * GPT-5: 8086-compatible 32-bit increment using 16-bit + carry pattern
 */
#pragma aux health_inc_error_fast = \
    "mov  bx, ax"           \
    "shl  bx, 2"           \
    "add  word ptr health_error_counters[bx], 1"    \
    "adc  word ptr health_error_counters[bx+2], 0"  \
    parm [ax] \
    modify [bx flags];

void health_inc_error_fast(unsigned short category);

/**
 * @brief Flag-safe error counter increment (preserves CPU flags)
 * GPT-5: Use in ISRs and hot paths where flags must be preserved
 */
#pragma aux health_inc_error_safe = \
    "pushf"                 \
    "mov  bx, ax"           \
    "shl  bx, 2"           \
    "add  word ptr health_error_counters[bx], 1"    \
    "adc  word ptr health_error_counters[bx+2], 0"  \
    "popf"                  \
    parm [ax] \
    modify [bx];

void health_inc_error_safe(unsigned short category);

/**
 * @brief Fast metric update (assembly inline)
 */
#pragma aux health_set_metric_fast = \
    "mov  bx, ax"           \
    "shl  bx, 1"           \
    "mov  health_performance_metrics[bx], dx" \
    parm [ax] [dx] \
    modify [bx flags];

void health_set_metric_fast(unsigned short index, unsigned short value);

/**
 * @brief Flag-safe metric update (preserves CPU flags)
 * GPT-5: Use in ISRs and hot paths where flags must be preserved
 */
#pragma aux health_set_metric_safe = \
    "pushf"                 \
    "mov  bx, ax"           \
    "shl  bx, 1"           \
    "mov  health_performance_metrics[bx], dx" \
    "popf"                  \
    parm [ax] [dx] \
    modify [bx];

void health_set_metric_safe(unsigned short index, unsigned short value);

/* Use fast assembly versions by default */
#define HEALTH_INC_ERROR_FAST(category) health_inc_error_fast(category)
#define HEALTH_SET_METRIC_FAST(index, value) health_set_metric_fast(index, value)

/* Flag-safe variants for ISRs and critical paths */
#define HEALTH_INC_ERROR_SAFE(category) health_inc_error_safe(category)
#define HEALTH_SET_METRIC_SAFE(index, value) health_set_metric_safe(index, value)

#else

/* C versions for non-Watcom compilers */
#define HEALTH_INC_ERROR_FAST(category) HEALTH_INC_ERROR(category)
#define HEALTH_SET_METRIC_FAST(index, value) HEALTH_SET_METRIC(index, value)
#define HEALTH_INC_ERROR_SAFE(category) HEALTH_INC_ERROR(category)
#define HEALTH_SET_METRIC_SAFE(index, value) HEALTH_SET_METRIC(index, value)

#endif /* __WATCOMC__ */

/*
 * GPT-5 Flag Safety Guidelines:
 * 
 * HEALTH_INC_ERROR_FAST / HEALTH_SET_METRIC_FAST:
 *   - Modifies CPU flags (ADD, ADC, MOV operations)
 *   - Use only where flags are not needed after the macro
 *   - Fastest performance (1-3 instructions)
 * 
 * HEALTH_INC_ERROR_SAFE / HEALTH_SET_METRIC_SAFE:  
 *   - Preserves CPU flags using PUSHF/POPF
 *   - Use in ISRs, loops, and flag-dependent code paths
 *   - Small overhead (2-3 additional instructions)
 * 
 * Example Usage:
 *   ; Safe for ISR use:
 *   health_record_tx_completion(nic, 1);  // Uses SAFE variants internally
 * 
 *   ; Fast path where flags are dead:
 *   HEALTH_INC_ERROR_FAST(HEALTH_ERROR_TX);
 *   ; ... other code that doesn't depend on flags
 */

/* Integration functions for specific subsystems */

/**
 * @brief Record TX packet completion
 * @param nic_index NIC index (0-3)
 * @param success 1 if successful, 0 if error
 */
static inline void health_record_tx_completion(int nic_index, int success)
{
    if (success) {
        if (nic_index >= 0 && nic_index < 4) {
            HEALTH_SET_METRIC_SAFE(HEALTH_METRIC_TX_RATE_BASE + nic_index, 
                                 health_performance_metrics[HEALTH_METRIC_TX_RATE_BASE + nic_index] + 1);
        }
    } else {
        HEALTH_INC_ERROR_SAFE(HEALTH_ERROR_TX);
    }
}

/**
 * @brief Record RX packet reception
 * @param nic_index NIC index (0-3)
 * @param success 1 if successful, 0 if error
 */
static inline void health_record_rx_completion(int nic_index, int success)
{
    if (success) {
        if (nic_index >= 0 && nic_index < 4) {
            HEALTH_SET_METRIC_SAFE(HEALTH_METRIC_RX_RATE_BASE + nic_index,
                                 health_performance_metrics[HEALTH_METRIC_RX_RATE_BASE + nic_index] + 1);
        }
    } else {
        HEALTH_INC_ERROR_SAFE(HEALTH_ERROR_RX);
    }
}

/**
 * @brief Record API call
 */
static inline void health_record_api_call(void)
{
    HEALTH_SET_METRIC_SAFE(HEALTH_METRIC_API_FREQUENCY,
                         health_performance_metrics[HEALTH_METRIC_API_FREQUENCY] + 1);
}

/**
 * @brief Record ISR entry  
 */
static inline void health_record_isr_entry(void)
{
    HEALTH_SET_METRIC_SAFE(HEALTH_METRIC_ISR_FREQUENCY,
                         health_performance_metrics[HEALTH_METRIC_ISR_FREQUENCY] + 1);
}

/**
 * @brief Record DMA error
 */
static inline void health_record_dma_error(void)
{
    HEALTH_INC_ERROR_SAFE(HEALTH_ERROR_DMA);
}

/**
 * @brief Record memory error
 */
static inline void health_record_memory_error(void)
{
    HEALTH_INC_ERROR_SAFE(HEALTH_ERROR_MEMORY);
}

/**
 * @brief Record hardware error
 */
static inline void health_record_hardware_error(void)
{
    HEALTH_INC_ERROR_SAFE(HEALTH_ERROR_HARDWARE);
}

/**
 * @brief Record buffer error
 */
static inline void health_record_buffer_error(void)
{
    HEALTH_INC_ERROR_SAFE(HEALTH_ERROR_BUFFER);
}

/**
 * @brief Record timeout error
 */
static inline void health_record_timeout_error(void)
{
    HEALTH_INC_ERROR_SAFE(HEALTH_ERROR_TIMEOUT);
}

/**
 * @brief Update buffer utilization
 * @param nic_index NIC index (0-3)
 * @param utilization_percent Buffer utilization percentage (0-100)
 */
static inline void health_update_buffer_utilization(int nic_index, int utilization_percent)
{
    if (nic_index >= 0 && nic_index < 4) {
        HEALTH_SET_METRIC_SAFE(HEALTH_METRIC_BUFFER_BASE + nic_index, utilization_percent);
    }
}

/**
 * @brief Update CPU utilization estimate
 * @param cpu_percent CPU utilization percentage (0-100)
 */
static inline void health_update_cpu_utilization(int cpu_percent)
{
    HEALTH_SET_METRIC_SAFE(HEALTH_METRIC_CPU_UTIL, cpu_percent);
}

/**
 * @brief Update memory pressure indicator
 * @param pressure_percent Memory pressure percentage (0-100)
 */
static inline void health_update_memory_pressure(int pressure_percent)
{
    HEALTH_SET_METRIC_SAFE(HEALTH_METRIC_MEMORY_PRESSURE, pressure_percent);
}

/* Conditional compilation for debug/release builds */
#ifdef HEALTH_DIAGNOSTICS_DISABLED
/* Compile out all health diagnostics for maximum performance */
#undef HEALTH_INC_ERROR
#undef HEALTH_SET_METRIC
#undef HEALTH_INC_METRIC
#undef HEALTH_INC_ERROR_FAST
#undef HEALTH_SET_METRIC_FAST

#define HEALTH_INC_ERROR(category) do { } while(0)
#define HEALTH_SET_METRIC(index, value) do { } while(0)
#define HEALTH_INC_METRIC(index) do { } while(0)
#define HEALTH_INC_ERROR_FAST(category) do { } while(0)
#define HEALTH_SET_METRIC_FAST(index, value) do { } while(0)

#define health_record_tx_completion(nic, success) do { } while(0)
#define health_record_rx_completion(nic, success) do { } while(0)
#define health_record_api_call() do { } while(0)
#define health_record_isr_entry() do { } while(0)
#define health_record_dma_error() do { } while(0)
#define health_record_memory_error() do { } while(0)
#define health_record_hardware_error() do { } while(0)
#define health_record_buffer_error() do { } while(0)
#define health_record_timeout_error() do { } while(0)
#define health_update_buffer_utilization(nic, util) do { } while(0)
#define health_update_cpu_utilization(cpu) do { } while(0)
#define health_update_memory_pressure(pressure) do { } while(0)

#endif /* HEALTH_DIAGNOSTICS_DISABLED */

#ifdef __cplusplus
}
#endif

#endif /* _HEALTH_DIAGNOSTICS_H_ */
/**
 * @file diagnostics.h
 * @brief Diagnostic and logging functions
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef _DIAGNOSTICS_H_
#define _DIAGNOSTICS_H_

/* Maximum number of NICs supported */
#ifndef MAX_NICS
#define MAX_NICS 8
#endif

/* Alert types for real-time monitoring */
#define ALERT_TYPE_ERROR_RATE_HIGH      0
#define ALERT_TYPE_UTILIZATION_HIGH     1
#define ALERT_TYPE_MEMORY_LOW           2
#define ALERT_TYPE_NIC_FAILURE          3
#define ALERT_TYPE_ROUTING_FAILURE      4
#define ALERT_TYPE_API_ERROR            5
#define ALERT_TYPE_PERFORMANCE_DEGRADED 6
#define ALERT_TYPE_BOTTLENECK_DETECTED  7
#define ALERT_TYPE_HARDWARE_FAILURE     8
#define ALERT_TYPE_NETWORK_DOWN         9

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"
#include "hardware.h"
#include "packet.h"      /* For packet_buffer_t */

/* Open Watcom compatibility for standard C macros */
#if defined(__WATCOMC__)
    /* Watcom supports __FILE__ and __LINE__ but not __FUNCTION__ */
    #ifndef __FUNCTION__
        #define __FUNCTION__ "?"
    #endif
#elif !defined(__FUNCTION__)
    #define __FUNCTION__ "?"
#endif

/* Diagnostic levels */
typedef enum {
    DIAG_LEVEL_NONE,                        /* No diagnostics */
    DIAG_LEVEL_ERROR,                       /* Errors only */
    DIAG_LEVEL_WARNING,                     /* Warnings and errors */
    DIAG_LEVEL_INFO,                        /* Information, warnings, errors */
    DIAG_LEVEL_DEBUG,                       /* Debug information */
    DIAG_LEVEL_TRACE                        /* Trace all operations */
} diag_level_t;

/* Diagnostic categories */
#define DIAG_CAT_HARDWARE       BIT(0)      /* Hardware diagnostics */
#define DIAG_CAT_NETWORK        BIT(1)      /* Network diagnostics */
#define DIAG_CAT_MEMORY         BIT(2)      /* Memory diagnostics */
#define DIAG_CAT_INTERRUPT      BIT(3)      /* Interrupt diagnostics */
#define DIAG_CAT_PACKET         BIT(4)      /* Packet diagnostics */
#define DIAG_CAT_CONFIG         BIT(5)      /* Configuration diagnostics */
#define DIAG_CAT_PERFORMANCE    BIT(6)      /* Performance diagnostics */
#define DIAG_CAT_DRIVER         BIT(7)      /* Driver diagnostics */
#define DIAG_CAT_ALL            0xFF        /* All categories */

/* Diagnostic test types */
typedef enum {
    DIAG_TEST_NONE,
    DIAG_TEST_HARDWARE,                     /* Hardware self-test */
    DIAG_TEST_MEMORY,                       /* Memory test */
    DIAG_TEST_INTERRUPT,                    /* Interrupt test */
    DIAG_TEST_LOOPBACK,                     /* Loopback test */
    DIAG_TEST_NETWORK,                      /* Network connectivity test */
    DIAG_TEST_PERFORMANCE,                  /* Performance benchmark */
    DIAG_TEST_STRESS,                       /* Stress test */
    DIAG_TEST_ALL                           /* All tests */
} diag_test_t;

/* Diagnostic result structure */
typedef struct {
    diag_test_t test_type;                  /* Test type */
    bool passed;                            /* Test passed */
    uint32_t error_code;                    /* Error code if failed */
    char description[128];                  /* Test description */
    uint32_t duration_ms;                   /* Test duration */
    uint32_t timestamp;                     /* Test timestamp */
} diag_result_t;

/* Log entry structure */
typedef struct log_entry {
    uint32_t timestamp;                     /* Entry timestamp */
    diag_level_t level;                     /* Log level */
    uint32_t category;                      /* Category flags */
    char message[256];                      /* Log message */
    const char *function;                   /* Function name */
    const char *file;                       /* Source file */
    uint32_t line;                          /* Line number */
    struct log_entry *next;                 /* Next entry */
} log_entry_t;

/* Network health assessment structure */
typedef struct network_health {
    uint8_t overall_score;        /* 0-100 health score */
    uint8_t nic_health[MAX_NICS]; /* Per-NIC health scores */
    uint32_t error_rate;          /* Errors per 1000 packets */
    uint32_t utilization;         /* Network utilization percentage */
    uint16_t active_flows;        /* Number of active flows */
    uint16_t arp_table_usage;     /* ARP table utilization */
    uint32_t route_failures;      /* Recent routing failures */
    uint32_t api_errors;          /* API-related errors */
    uint32_t last_update;         /* Timestamp of last update */
} network_health_t;

/* Ring buffer for efficient logging in DOS memory */
typedef struct log_ring_buffer {
    log_entry_t *entries;                   /* Ring buffer entries */
    uint16_t size;                          /* Buffer size */
    uint16_t write_index;                   /* Write position */
    uint16_t read_index;                    /* Read position */
    uint16_t count;                         /* Number of entries */
    bool wrapped;                           /* Buffer has wrapped */
} log_ring_buffer_t;

/* Enhanced logging configuration */
typedef struct log_config {
    bool console_enabled;                   /* Console output */
    bool file_enabled;                      /* File output */
    bool network_enabled;                   /* Network output */
    diag_level_t min_level;                 /* Minimum log level */
    uint32_t category_filter;               /* Category filter mask */
    char file_path[64];                     /* Log file path (DOS 8.3) */
    uint16_t ring_buffer_size;              /* Ring buffer size */
} log_config_t;

/* Performance counters with enhanced metrics */
typedef struct {
    uint32_t packets_sent;                  /* Packets transmitted */
    uint32_t packets_received;              /* Packets received */
    uint32_t bytes_sent;                    /* Bytes transmitted */
    uint32_t bytes_received;                /* Bytes received */
    uint32_t interrupts_handled;            /* Interrupts handled */
    uint32_t errors_detected;               /* Errors detected */
    uint32_t timeouts;                      /* Timeout events */
    uint32_t retransmissions;               /* Retransmissions */
    uint32_t start_time;                    /* Start timestamp */
    uint32_t last_update;                   /* Last update timestamp */
    /* Enhanced metrics for Group 3C */
    uint32_t cpu_usage_samples;             /* CPU usage sample count */
    uint32_t memory_peak_usage;             /* Peak memory usage */
    uint32_t buffer_overruns;               /* Buffer overrun count */
    uint32_t packet_drops;                  /* Dropped packet count */
} perf_counters_t;

/* Flow tracking for connection symmetry */
typedef struct flow_entry {
    uint32_t src_ip;                        /* Source IP address */
    uint32_t dest_ip;                       /* Destination IP address */
    uint16_t src_port;                      /* Source port */
    uint16_t dest_port;                     /* Destination port */
    uint8_t protocol;                       /* Protocol (TCP/UDP) */
    uint8_t nic_index;                      /* NIC used for flow */
    uint32_t packet_count;                  /* Packets in flow */
    uint32_t byte_count;                    /* Bytes in flow */
    uint32_t last_seen;                     /* Last activity timestamp */
    struct flow_entry *next;                /* Next flow entry */
} flow_entry_t;

/* Historical statistics tracking */
typedef struct historical_sample {
    uint32_t timestamp;
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t errors_detected;
    uint32_t memory_usage;
    uint8_t network_health;
    uint8_t cpu_utilization;
    struct historical_sample *next;
} historical_sample_t;

/* Trend analysis result */
typedef struct trend_analysis {
    int32_t packet_trend;           /* Packets/sec change */
    int32_t error_trend;            /* Error rate change */
    int32_t health_trend;           /* Health score change */
    int32_t memory_trend;           /* Memory usage change */
    uint32_t analysis_window_ms;    /* Analysis time window */
    uint32_t sample_count;          /* Number of samples */
} trend_analysis_t;

/* Comprehensive diagnostics state */
typedef struct diag_system_state {
    network_health_t health;                /* Network health assessment */
    log_ring_buffer_t log_buffer;           /* Ring buffer for logs */
    log_config_t log_config;                /* Logging configuration */
    flow_entry_t *active_flows;             /* Active flow tracking */
    uint16_t flow_count;                    /* Number of active flows */
    uint32_t flow_timeout;                  /* Flow timeout (ms) */
    bool monitoring_enabled;                /* Real-time monitoring */
    uint32_t alert_thresholds[8];           /* Alert thresholds */
    
    /* Historical tracking */
    historical_sample_t *history_head;      /* Historical data samples */
    uint16_t history_count;                 /* Number of history samples */
    uint16_t max_history_samples;           /* Maximum history to keep */
    uint32_t sample_interval_ms;            /* Sampling interval */
    uint32_t last_sample_time;              /* Last sample timestamp */
    trend_analysis_t current_trends;        /* Current trend analysis */
} diag_system_state_t;

/* Global diagnostic state */
extern diag_level_t g_diag_level;
extern uint32_t g_diag_categories;
extern perf_counters_t g_perf_counters;
extern bool g_diagnostics_enabled;
extern diag_system_state_t g_diag_state;
extern network_health_t g_network_health;

/* Function prototypes */
int diagnostics_init(void);
void diagnostics_cleanup(void);
int diagnostics_enable(bool enable);
bool diagnostics_is_enabled(void);

/* Logging functions */
void log_message(diag_level_t level, uint32_t category, const char *function,
                const char *file, uint32_t line, const char *format, ...);
void log_error(const char *format, ...);
void log_warning(const char *format, ...);
void log_info(const char *format, ...);
void log_debug(const char *format, ...);
void log_trace(const char *format, ...);

/* Logging macros */
#define LOG_ERROR(fmt, ...)   log_message(DIAG_LEVEL_ERROR, DIAG_CAT_DRIVER, \
                                         __FUNCTION__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) log_message(DIAG_LEVEL_WARNING, DIAG_CAT_DRIVER, \
                                         __FUNCTION__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)    log_message(DIAG_LEVEL_INFO, DIAG_CAT_DRIVER, \
                                         __FUNCTION__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)   log_message(DIAG_LEVEL_DEBUG, DIAG_CAT_DRIVER, \
                                         __FUNCTION__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...)   log_message(DIAG_LEVEL_TRACE, DIAG_CAT_DRIVER, \
                                         __FUNCTION__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

/* Diagnostic tests */
int diag_run_test(diag_test_t test_type, nic_info_t *nic, diag_result_t *result);
int diag_run_all_tests(nic_info_t *nic, diag_result_t *results, uint32_t max_results);
int diag_hardware_test(nic_info_t *nic, diag_result_t *result);
int diag_memory_test(diag_result_t *result);
int diag_interrupt_test(nic_info_t *nic, diag_result_t *result);
int diag_loopback_test(nic_info_t *nic, diag_result_t *result);
int diag_network_test(nic_info_t *nic, diag_result_t *result);
int diag_performance_test(nic_info_t *nic, diag_result_t *result);

/* Hardware diagnostics */
int diag_check_hardware_registers(nic_info_t *nic);
int diag_test_hardware_interrupts(nic_info_t *nic);
void diag_dump_hardware_state(nic_info_t *nic);
void diag_dump_registers(nic_info_t *nic);

/* Performance monitoring */
void perf_counters_init(perf_counters_t *counters);
void perf_counters_reset(perf_counters_t *counters);
void perf_counters_update_tx(perf_counters_t *counters, uint32_t bytes);
void perf_counters_update_rx(perf_counters_t *counters, uint32_t bytes);
const perf_counters_t* perf_get_counters(void);
void perf_print_counters(const perf_counters_t *counters);

/* System information */
void diag_print_system_info(void);
void diag_print_driver_info(void);
void diag_print_hardware_info(void);
void diag_print_memory_info(void);
void diag_print_network_info(void);

/* Network health monitoring */
int diag_health_init(void);
void diag_health_update(void);
uint8_t diag_calculate_network_health(void);
uint8_t diag_calculate_nic_health(uint8_t nic_index);
int diag_health_check_thresholds(void);
void diag_health_reset_counters(void);

/* Enhanced logging system with ring buffer */
int diag_log_init_ring_buffer(uint16_t size);
void diag_log_cleanup_ring_buffer(void);
int diag_log_write_entry(const log_entry_t *entry);
int diag_log_read_entries(log_entry_t *buffer, uint16_t max_entries);
int diag_log_configure(const log_config_t *config);
void diag_log_set_output_targets(bool console, bool file, bool network);

/* Flow tracking for multi-homing */
int diag_flow_init(uint16_t max_flows, uint32_t timeout_ms);
void diag_flow_cleanup(void);
int diag_flow_track_packet(const packet_buffer_t *packet, uint8_t nic_index);
void diag_flow_age_entries(void);
flow_entry_t* diag_flow_lookup(uint32_t src_ip, uint32_t dest_ip, uint16_t src_port, uint16_t dest_port, uint8_t protocol);

/* Statistics integration from Groups 3A/3B */
int diag_integrate_arp_stats(void);
int diag_integrate_routing_stats(void);
int diag_integrate_api_stats(void);
void diag_update_comprehensive_stats(void);

/* Historical tracking and trend analysis */
int diag_history_init(uint16_t max_samples, uint32_t sample_interval_ms);
void diag_history_cleanup(void);
int diag_history_add_sample(void);
void diag_history_age_samples(uint32_t max_age_ms);
int diag_trend_analysis(uint32_t window_ms, trend_analysis_t *result);
const historical_sample_t* diag_history_get_samples(uint16_t *count);
int diag_history_export(char *buffer, uint32_t buffer_size);
void diag_history_print_summary(void);

/* Real-time monitoring and alerting */
int diag_monitoring_init(void);
void diag_monitoring_enable(bool enable);
int diag_set_alert_threshold(uint8_t metric_type, uint32_t threshold);
int diag_check_alerts(void);
void diag_generate_alert(uint8_t alert_type, const char *message);

/* Bottleneck detection and analysis */
int diag_detect_bottlenecks(void);
void diag_analyze_packet_flow(void);
int diag_check_memory_pressure(void);
int diag_check_cpu_utilization(void);

/* Error correlation and pattern analysis */
int diag_correlate_errors(void);
void diag_pattern_analysis(void);
int diag_error_trend_analysis(uint32_t time_window_ms);
void diag_report_error(uint8_t error_type, uint8_t nic_index, uint32_t error_code, const char *description);

/* Error type constants for correlation */
#define ERROR_TYPE_TX_FAILURE       1
#define ERROR_TYPE_CRC_ERROR        2
#define ERROR_TYPE_TIMEOUT          3
#define ERROR_TYPE_BUFFER_OVERRUN   4
#define ERROR_TYPE_INTERRUPT_ERROR  5
#define ERROR_TYPE_MEMORY_ERROR     6
#define ERROR_TYPE_ROUTING_ERROR    7
#define ERROR_TYPE_API_ERROR        8

/* Enhanced Diagnostic Logging with /LOG=ON Support */
int diag_configure_logging(const char *log_param);
int diag_enhanced_hardware_test(nic_info_t *nic);
int diag_advanced_error_correlation(void);
int diag_enhanced_bottleneck_detection(void);
void diag_print_comprehensive_report(void);

/* Debug Logging Framework */
int debug_logging_init(void);
void debug_logging_cleanup(void);
int debug_logging_configure_from_param(const char *log_param);
int debug_logging_configure_network(const char *network_config);
int debug_logging_set_level(diag_level_t level);
int debug_logging_set_category_filter(uint32_t category_mask);
int debug_logging_set_output_targets(bool console, bool file, bool network, bool buffer);
int debug_logging_set_file_path(const char *file_path);
int debug_logging_set_rate_limiting(bool enabled, uint32_t messages_per_sec);
int debug_log_message(diag_level_t level, uint32_t category, const char *function,
                     const char *file, uint32_t line, const char *format, ...);
int debug_log_error(const char *format, ...);
int debug_log_warning(const char *format, ...);
int debug_log_info(const char *format, ...);
int debug_log_debug(const char *format, ...);
int debug_log_trace(const char *format, ...);
int debug_logging_read_buffer(log_entry_t *entries, uint16_t max_entries, uint16_t *count_read);
int debug_logging_get_statistics(uint32_t *total_entries, uint32_t *dropped_entries, 
                                 uint32_t *buffer_overflows, uint32_t *file_errors);
int debug_logging_print_dashboard(void);
int debug_logging_ne2000_emulation(diag_level_t level, const char *operation, 
                                   uint16_t reg, uint16_t value, const char *description);
int debug_logging_system_ready(void);

/* Diagnostic utilities */
const char* diag_level_to_string(diag_level_t level);
const char* diag_test_to_string(diag_test_t test);
uint32_t diag_get_timestamp(void);
const char* diag_health_score_to_string(uint8_t score);
const char* diag_alert_type_to_string(uint8_t alert_type);
const char* diag_trend_to_string(int32_t trend);

#ifdef __cplusplus
}
#endif

#endif /* _DIAGNOSTICS_H_ */

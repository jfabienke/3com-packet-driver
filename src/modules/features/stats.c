/**
 * @file stats.c
 * @brief STATS.MOD - Advanced Statistics Collection Feature Module
 * 
 * Phase 3A: Dynamic Module Loading - Stream 3 Feature Implementation
 * 
 * This module provides comprehensive statistics collection and analysis:
 * - Per-NIC packet counters
 * - Error rate monitoring  
 * - Performance metrics collection
 * - Historical data management
 * - Export capabilities
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#include "module_api.h"
#include <stdio.h>
#include <string.h>

/* Statistics Collection Constants */
#define MAX_HISTORY_ENTRIES     256
#define HISTORY_INTERVAL_MS     1000    /* 1 second intervals */
#define MAX_ERROR_TYPES         16
#define MAX_PROTOCOL_TYPES      32

/* Statistics Categories */
#define STAT_CATEGORY_BASIC     0x01    /* Basic packet counters */
#define STAT_CATEGORY_ERRORS    0x02    /* Error statistics */
#define STAT_CATEGORY_PROTOCOLS 0x04    /* Protocol statistics */
#define STAT_CATEGORY_PERFORMANCE 0x08  /* Performance metrics */
#define STAT_CATEGORY_ALL       0xFF    /* All categories */

/* Error Types */
#define ERROR_TYPE_CRC          0
#define ERROR_TYPE_ALIGNMENT    1
#define ERROR_TYPE_LENGTH       2
#define ERROR_TYPE_OVERRUN      3
#define ERROR_TYPE_UNDERRUN     4
#define ERROR_TYPE_COLLISION    5
#define ERROR_TYPE_LATE_COLLISION 6
#define ERROR_TYPE_CARRIER_LOST 7
#define ERROR_TYPE_NO_CARRIER   8
#define ERROR_TYPE_DMA_ERROR    9
#define ERROR_TYPE_FIFO_ERROR   10
#define ERROR_TYPE_TIMEOUT      11

/* Protocol Types (Ethernet types) */
#define PROTOCOL_IP             0x0800
#define PROTOCOL_ARP            0x0806
#define PROTOCOL_IPX            0x8137
#define PROTOCOL_NETBEUI        0x8191
#define PROTOCOL_IPV6           0x86DD

/* Historical Data Entry */
typedef struct {
    uint32_t timestamp;         /* Sample timestamp */
    uint32_t tx_packets;        /* TX packets in interval */
    uint32_t rx_packets;        /* RX packets in interval */
    uint32_t tx_bytes;          /* TX bytes in interval */
    uint32_t rx_bytes;          /* RX bytes in interval */
    uint32_t errors;            /* Errors in interval */
    uint16_t cpu_usage;         /* CPU usage percentage */
    uint16_t memory_usage;      /* Memory usage percentage */
} history_entry_t;

/* Per-NIC Extended Statistics */
typedef struct {
    /* Basic counters */
    nic_stats_t basic;          /* Basic NIC statistics */
    
    /* Error breakdown */
    uint32_t error_counts[MAX_ERROR_TYPES];
    
    /* Protocol breakdown */
    uint32_t protocol_counts[MAX_PROTOCOL_TYPES];
    uint32_t protocol_bytes[MAX_PROTOCOL_TYPES];
    
    /* Performance metrics */
    uint32_t avg_packet_size;   /* Average packet size */
    uint32_t peak_tx_rate;      /* Peak TX rate (bytes/sec) */
    uint32_t peak_rx_rate;      /* Peak RX rate (bytes/sec) */
    uint32_t utilization_pct;   /* Link utilization percentage */
    
    /* Timing statistics */
    uint32_t min_packet_time;   /* Minimum packet processing time */
    uint32_t max_packet_time;   /* Maximum packet processing time */
    uint32_t avg_packet_time;   /* Average packet processing time */
    
    /* Historical data */
    history_entry_t history[MAX_HISTORY_ENTRIES];
    uint16_t history_head;      /* History buffer head */
    uint16_t history_count;     /* Number of history entries */
    
    /* State tracking */
    uint32_t last_sample_time;  /* Last sample timestamp */
    uint32_t sample_interval;   /* Sampling interval */
    bool active;                /* Statistics collection active */
} extended_nic_stats_t;

/* Global Statistics */
typedef struct {
    uint32_t total_packets;     /* Total packets across all NICs */
    uint32_t total_bytes;       /* Total bytes across all NICs */
    uint32_t total_errors;      /* Total errors across all NICs */
    uint32_t uptime_seconds;    /* System uptime */
    uint32_t collection_start;  /* Statistics collection start time */
    uint16_t active_nics;       /* Number of active NICs */
    uint16_t enabled_categories;/* Enabled statistic categories */
} global_stats_t;

/* Statistics Export Format */
typedef enum {
    EXPORT_FORMAT_TEXT = 0,     /* Plain text format */
    EXPORT_FORMAT_CSV = 1,      /* Comma-separated values */
    EXPORT_FORMAT_BINARY = 2    /* Binary format */
} export_format_t;

/* Module Context */
typedef struct {
    extended_nic_stats_t nic_stats[MAX_NICS_SUPPORTED];
    global_stats_t global_stats;
    core_services_t* core_services;
    bool collection_enabled;
    uint16_t collection_categories;
    uint32_t collection_interval;
    char export_filename[64];
} stats_context_t;

/* Global module context */
static stats_context_t stats_ctx;

/* Forward declarations */
static void stats_packet_handler(packet_t* packet);
static void stats_timer_callback(void);
static bool stats_collect_sample(uint8_t nic_id);
static bool stats_update_performance_metrics(uint8_t nic_id, const packet_t* packet, bool is_tx);
static void stats_classify_error(uint8_t nic_id, uint16_t error_type);
static uint16_t stats_classify_protocol(const packet_t* packet);
static void stats_add_history_entry(uint8_t nic_id, const history_entry_t* entry);

/* Export functions */
static bool stats_export_to_file(const char* filename, export_format_t format);
static bool stats_export_text(FILE* file);
static bool stats_export_csv(FILE* file);
static bool stats_export_binary(FILE* file);

/* API functions */
static bool stats_api_get_nic_stats(uint8_t nic_id, extended_nic_stats_t* stats);
static bool stats_api_get_global_stats(global_stats_t* stats);
static bool stats_api_reset_stats(uint8_t nic_id);
static bool stats_api_set_collection_interval(uint32_t interval_ms);
static bool stats_api_export_stats(const char* filename, uint8_t format);
static bool stats_api_enable_categories(uint16_t categories);

/* Module API registration */
static const api_registration_t stats_apis[] = {
    {"get_nic_stats", stats_api_get_nic_stats},
    {"get_global_stats", stats_api_get_global_stats},
    {"reset_stats", stats_api_reset_stats},
    {"set_interval", stats_api_set_collection_interval},
    {"export_stats", stats_api_export_stats},
    {"enable_categories", stats_api_enable_categories},
    {NULL, NULL}  /* Terminator */
};

/* ============================================================================
 * Module Header and Initialization
 * ============================================================================ */

/* Module header - must be first in the file */
const module_header_t module_header = {
    .magic = MODULE_MAGIC,
    .version = 0x0100,  /* Version 1.0 */
    .header_size = sizeof(module_header_t),
    .module_size = 0,   /* Filled by linker */
    .module_class = MODULE_CLASS_FEATURE,
    .family_id = FAMILY_UNKNOWN,
    .feature_flags = FEATURE_STATISTICS,
    .api_version = MODULE_API_VERSION,
    .init_offset = (uint16_t)stats_init,
    .vtable_offset = 0,  /* No vtable for feature modules */
    .cleanup_offset = (uint16_t)stats_cleanup,
    .info_offset = 0,
    .deps_count = 0,
    .deps_offset = 0,
    .min_dos_version = 0x0300,  /* DOS 3.0+ */
    .min_cpu_family = 2,        /* 286+ */
    .name = "STATS",
    .description = "Advanced Statistics Engine",
    .author = "3Com/Phase3A",
    .build_timestamp = 0,       /* Filled by build system */
    .checksum = 0,              /* Calculated by build system */
    .reserved = {0}
};

/**
 * @brief Feature module initialization function
 */
bool stats_init(core_services_t* core, const module_config_t* config)
{
    if (!core) {
        return false;
    }
    
    /* Initialize module context */
    memset(&stats_ctx, 0, sizeof(stats_context_t));
    stats_ctx.core_services = core;
    stats_ctx.collection_enabled = true;
    stats_ctx.collection_categories = STAT_CATEGORY_ALL;
    stats_ctx.collection_interval = HISTORY_INTERVAL_MS;
    strcpy(stats_ctx.export_filename, "3CPD_STATS.TXT");
    
    /* Initialize per-NIC statistics */
    for (int i = 0; i < MAX_NICS_SUPPORTED; i++) {
        stats_ctx.nic_stats[i].sample_interval = HISTORY_INTERVAL_MS;
        stats_ctx.nic_stats[i].active = false;
        stats_ctx.nic_stats[i].min_packet_time = 0xFFFFFFFF;
        stats_ctx.nic_stats[i].max_packet_time = 0;
        stats_ctx.nic_stats[i].avg_packet_time = 0;
    }
    
    /* Initialize global statistics */
    stats_ctx.global_stats.collection_start = core->timing.get_ticks();
    stats_ctx.global_stats.enabled_categories = STAT_CATEGORY_ALL;
    
    /* Register packet handlers for all protocols */
    for (int i = 0; i < MAX_PROTOCOL_TYPES; i++) {
        core->register_packet_handler(0xFFFF, stats_packet_handler);  /* Catch all */
    }
    
    /* Install timer for periodic collection */
    if (!core->timing.install_timer(stats_ctx.collection_interval, 
                                   stats_timer_callback, 1)) {
        core->log_message(LOG_LEVEL_ERROR, "STATS",
            "Failed to install statistics collection timer");
        return false;
    }
    
    /* Register APIs */
    if (!core->register_apis("STATS", stats_apis)) {
        core->log_message(LOG_LEVEL_ERROR, "STATS",
            "Failed to register statistics APIs");
        return false;
    }
    
    core->log_message(LOG_LEVEL_INFO, "STATS",
        "Advanced statistics engine initialized (interval: %dms)",
        stats_ctx.collection_interval);
    
    return true;
}

/**
 * @brief Module cleanup function
 */
void stats_cleanup(void)
{
    if (stats_ctx.core_services) {
        /* Remove timer */
        stats_ctx.core_services->timing.remove_timer(1);
        
        /* Unregister packet handlers */
        stats_ctx.core_services->unregister_packet_handler(0xFFFF);
        
        /* Unregister APIs */
        stats_ctx.core_services->unregister_apis("STATS");
        
        /* Export final statistics */
        stats_export_to_file(stats_ctx.export_filename, EXPORT_FORMAT_TEXT);
        
        stats_ctx.core_services->log_message(LOG_LEVEL_INFO, "STATS",
            "Advanced statistics engine cleanup complete");
    }
    
    /* Clear context */
    memset(&stats_ctx, 0, sizeof(stats_context_t));
}

/* ============================================================================
 * Statistics Collection Functions
 * ============================================================================ */

/**
 * @brief Packet handler for statistics collection
 */
static void stats_packet_handler(packet_t* packet)
{
    uint8_t nic_id;
    uint16_t protocol_type;
    uint32_t start_time, end_time, processing_time;
    
    if (!packet || !stats_ctx.collection_enabled) {
        return;
    }
    
    nic_id = packet->nic_id;
    if (nic_id >= MAX_NICS_SUPPORTED) {
        return;
    }
    
    /* Mark start time for performance measurement */
    start_time = stats_ctx.core_services->timing.get_microseconds();
    
    /* Activate NIC statistics if not already active */
    if (!stats_ctx.nic_stats[nic_id].active) {
        stats_ctx.nic_stats[nic_id].active = true;
        stats_ctx.global_stats.active_nics++;
    }
    
    /* Update basic packet counters */
    if (packet->flags & 0x01) {  /* TX packet */
        stats_ctx.nic_stats[nic_id].basic.tx_packets++;
        stats_ctx.nic_stats[nic_id].basic.tx_bytes += packet->length;
    } else {  /* RX packet */
        stats_ctx.nic_stats[nic_id].basic.rx_packets++;
        stats_ctx.nic_stats[nic_id].basic.rx_bytes += packet->length;
    }
    
    /* Update global counters */
    stats_ctx.global_stats.total_packets++;
    stats_ctx.global_stats.total_bytes += packet->length;
    
    /* Classify and count protocol */
    if (stats_ctx.collection_categories & STAT_CATEGORY_PROTOCOLS) {
        protocol_type = stats_classify_protocol(packet);
        if (protocol_type < MAX_PROTOCOL_TYPES) {
            stats_ctx.nic_stats[nic_id].protocol_counts[protocol_type]++;
            stats_ctx.nic_stats[nic_id].protocol_bytes[protocol_type] += packet->length;
        }
    }
    
    /* Update performance metrics */
    if (stats_ctx.collection_categories & STAT_CATEGORY_PERFORMANCE) {
        stats_update_performance_metrics(nic_id, packet, packet->flags & 0x01);
    }
    
    /* Calculate processing time */
    end_time = stats_ctx.core_services->timing.get_microseconds();
    processing_time = end_time - start_time;
    
    /* Update timing statistics */
    extended_nic_stats_t* nic_stats = &stats_ctx.nic_stats[nic_id];
    if (processing_time < nic_stats->min_packet_time) {
        nic_stats->min_packet_time = processing_time;
    }
    if (processing_time > nic_stats->max_packet_time) {
        nic_stats->max_packet_time = processing_time;
    }
    
    /* Update average (simple moving average) */
    nic_stats->avg_packet_time = (nic_stats->avg_packet_time * 15 + processing_time) / 16;
}

/**
 * @brief Timer callback for periodic statistics collection
 */
static void stats_timer_callback(void)
{
    uint32_t current_time;
    
    if (!stats_ctx.collection_enabled) {
        return;
    }
    
    current_time = stats_ctx.core_services->timing.get_ticks();
    
    /* Update global uptime */
    stats_ctx.global_stats.uptime_seconds = 
        (current_time - stats_ctx.global_stats.collection_start) / 18;  /* 18.2 ticks/sec */
    
    /* Collect samples for all active NICs */
    for (int i = 0; i < MAX_NICS_SUPPORTED; i++) {
        if (stats_ctx.nic_stats[i].active) {
            stats_collect_sample(i);
        }
    }
}

/**
 * @brief Collect statistical sample for a NIC
 */
static bool stats_collect_sample(uint8_t nic_id)
{
    extended_nic_stats_t* nic_stats;
    history_entry_t entry;
    uint32_t current_time;
    static uint32_t last_tx_packets[MAX_NICS_SUPPORTED];
    static uint32_t last_rx_packets[MAX_NICS_SUPPORTED];
    static uint32_t last_tx_bytes[MAX_NICS_SUPPORTED];
    static uint32_t last_rx_bytes[MAX_NICS_SUPPORTED];
    
    if (nic_id >= MAX_NICS_SUPPORTED) {
        return false;
    }
    
    nic_stats = &stats_ctx.nic_stats[nic_id];
    current_time = stats_ctx.core_services->timing.get_ticks();
    
    /* Skip if not enough time has passed */
    if (current_time - nic_stats->last_sample_time < nic_stats->sample_interval / 55) {
        return false;  /* Convert ms to ticks (approx) */
    }
    
    /* Calculate interval values */
    entry.timestamp = current_time;
    entry.tx_packets = nic_stats->basic.tx_packets - last_tx_packets[nic_id];
    entry.rx_packets = nic_stats->basic.rx_packets - last_rx_packets[nic_id];
    entry.tx_bytes = nic_stats->basic.tx_bytes - last_tx_bytes[nic_id];
    entry.rx_bytes = nic_stats->basic.rx_bytes - last_rx_bytes[nic_id];
    entry.errors = nic_stats->basic.tx_errors + nic_stats->basic.rx_errors;
    
    /* Get system metrics */
    memory_stats_t mem_stats;
    if (stats_ctx.core_services->memory.get_stats(&mem_stats)) {
        entry.memory_usage = (uint16_t)((mem_stats.current_usage * 100) / mem_stats.conventional_total);
    } else {
        entry.memory_usage = 0;
    }
    
    /* Estimate CPU usage (simplified) */
    entry.cpu_usage = (uint16_t)((entry.tx_packets + entry.rx_packets) / 10);  /* Rough estimate */
    if (entry.cpu_usage > 100) entry.cpu_usage = 100;
    
    /* Update peak rates */
    uint32_t interval_sec = nic_stats->sample_interval / 1000;
    if (interval_sec > 0) {
        uint32_t tx_rate = entry.tx_bytes / interval_sec;
        uint32_t rx_rate = entry.rx_bytes / interval_sec;
        
        if (tx_rate > nic_stats->peak_tx_rate) {
            nic_stats->peak_tx_rate = tx_rate;
        }
        if (rx_rate > nic_stats->peak_rx_rate) {
            nic_stats->peak_rx_rate = rx_rate;
        }
        
        /* Calculate utilization (assuming 10Mbps link) */
        uint32_t total_rate = tx_rate + rx_rate;
        nic_stats->utilization_pct = (uint32_t)((total_rate * 100) / (10 * 1024 * 1024 / 8));
        if (nic_stats->utilization_pct > 100) {
            nic_stats->utilization_pct = 100;
        }
    }
    
    /* Calculate average packet size */
    if (entry.tx_packets + entry.rx_packets > 0) {
        nic_stats->avg_packet_size = (entry.tx_bytes + entry.rx_bytes) / 
                                   (entry.tx_packets + entry.rx_packets);
    }
    
    /* Add to history */
    stats_add_history_entry(nic_id, &entry);
    
    /* Update last values */
    last_tx_packets[nic_id] = nic_stats->basic.tx_packets;
    last_rx_packets[nic_id] = nic_stats->basic.rx_packets;
    last_tx_bytes[nic_id] = nic_stats->basic.tx_bytes;
    last_rx_bytes[nic_id] = nic_stats->basic.rx_bytes;
    nic_stats->last_sample_time = current_time;
    
    return true;
}

/**
 * @brief Update performance metrics for a packet
 */
static bool stats_update_performance_metrics(uint8_t nic_id, const packet_t* packet, bool is_tx)
{
    /* Performance metrics are updated in the packet handler */
    return true;
}

/**
 * @brief Classify error type for statistics
 */
static void stats_classify_error(uint8_t nic_id, uint16_t error_type)
{
    if (nic_id >= MAX_NICS_SUPPORTED || error_type >= MAX_ERROR_TYPES) {
        return;
    }
    
    stats_ctx.nic_stats[nic_id].error_counts[error_type]++;
    stats_ctx.global_stats.total_errors++;
}

/**
 * @brief Classify protocol type from packet
 */
static uint16_t stats_classify_protocol(const packet_t* packet)
{
    if (!packet || packet->length < 14) {
        return 0xFFFF;  /* Unknown */
    }
    
    /* Extract EtherType from Ethernet header */
    uint16_t ethertype = (packet->data[12] << 8) | packet->data[13];
    
    /* Map common protocols to indices */
    switch (ethertype) {
        case PROTOCOL_IP: return 0;
        case PROTOCOL_ARP: return 1;
        case PROTOCOL_IPX: return 2;
        case PROTOCOL_NETBEUI: return 3;
        case PROTOCOL_IPV6: return 4;
        default: return 31;  /* Other */
    }
}

/**
 * @brief Add entry to history buffer
 */
static void stats_add_history_entry(uint8_t nic_id, const history_entry_t* entry)
{
    extended_nic_stats_t* nic_stats;
    
    if (nic_id >= MAX_NICS_SUPPORTED || !entry) {
        return;
    }
    
    nic_stats = &stats_ctx.nic_stats[nic_id];
    
    /* Add entry to circular buffer */
    nic_stats->history[nic_stats->history_head] = *entry;
    nic_stats->history_head = (nic_stats->history_head + 1) % MAX_HISTORY_ENTRIES;
    
    if (nic_stats->history_count < MAX_HISTORY_ENTRIES) {
        nic_stats->history_count++;
    }
}

/* ============================================================================
 * Export Functions
 * ============================================================================ */

/**
 * @brief Export statistics to file
 */
static bool stats_export_to_file(const char* filename, export_format_t format)
{
    FILE* file;
    bool result = false;
    
    if (!filename) {
        filename = stats_ctx.export_filename;
    }
    
    file = fopen(filename, "w");
    if (!file) {
        stats_ctx.core_services->log_message(LOG_LEVEL_ERROR, "STATS",
            "Failed to open export file: %s", filename);
        return false;
    }
    
    switch (format) {
        case EXPORT_FORMAT_TEXT:
            result = stats_export_text(file);
            break;
        case EXPORT_FORMAT_CSV:
            result = stats_export_csv(file);
            break;
        case EXPORT_FORMAT_BINARY:
            result = stats_export_binary(file);
            break;
    }
    
    fclose(file);
    
    if (result) {
        stats_ctx.core_services->log_message(LOG_LEVEL_INFO, "STATS",
            "Statistics exported to: %s", filename);
    }
    
    return result;
}

/**
 * @brief Export statistics in text format
 */
static bool stats_export_text(FILE* file)
{
    fprintf(file, "3Com Packet Driver Statistics Report\n");
    fprintf(file, "=====================================\n\n");
    
    /* Global statistics */
    fprintf(file, "Global Statistics:\n");
    fprintf(file, "  Uptime: %d seconds\n", stats_ctx.global_stats.uptime_seconds);
    fprintf(file, "  Total Packets: %ld\n", stats_ctx.global_stats.total_packets);
    fprintf(file, "  Total Bytes: %ld\n", stats_ctx.global_stats.total_bytes);
    fprintf(file, "  Total Errors: %ld\n", stats_ctx.global_stats.total_errors);
    fprintf(file, "  Active NICs: %d\n\n", stats_ctx.global_stats.active_nics);
    
    /* Per-NIC statistics */
    for (int i = 0; i < MAX_NICS_SUPPORTED; i++) {
        if (stats_ctx.nic_stats[i].active) {
            extended_nic_stats_t* nic = &stats_ctx.nic_stats[i];
            
            fprintf(file, "NIC %d Statistics:\n", i);
            fprintf(file, "  TX Packets: %ld, Bytes: %ld\n", 
                   nic->basic.tx_packets, nic->basic.tx_bytes);
            fprintf(file, "  RX Packets: %ld, Bytes: %ld\n",
                   nic->basic.rx_packets, nic->basic.rx_bytes);
            fprintf(file, "  Errors: %ld\n", nic->basic.tx_errors + nic->basic.rx_errors);
            fprintf(file, "  Average Packet Size: %ld bytes\n", nic->avg_packet_size);
            fprintf(file, "  Peak TX Rate: %ld bytes/sec\n", nic->peak_tx_rate);
            fprintf(file, "  Peak RX Rate: %ld bytes/sec\n", nic->peak_rx_rate);
            fprintf(file, "  Utilization: %ld%%\n", nic->utilization_pct);
            fprintf(file, "  Packet Times: Min=%ldus, Max=%ldus, Avg=%ldus\n",
                   nic->min_packet_time, nic->max_packet_time, nic->avg_packet_time);
            fprintf(file, "\n");
        }
    }
    
    return true;
}

/**
 * @brief Export statistics in CSV format  
 */
static bool stats_export_csv(FILE* file)
{
    /* CSV header */
    fprintf(file, "NIC,TX_Packets,TX_Bytes,RX_Packets,RX_Bytes,Errors,Avg_Size,Peak_TX,Peak_RX,Utilization\n");
    
    /* Per-NIC data */
    for (int i = 0; i < MAX_NICS_SUPPORTED; i++) {
        if (stats_ctx.nic_stats[i].active) {
            extended_nic_stats_t* nic = &stats_ctx.nic_stats[i];
            
            fprintf(file, "%d,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld\n",
                   i, nic->basic.tx_packets, nic->basic.tx_bytes,
                   nic->basic.rx_packets, nic->basic.rx_bytes,
                   nic->basic.tx_errors + nic->basic.rx_errors,
                   nic->avg_packet_size, nic->peak_tx_rate, nic->peak_rx_rate,
                   nic->utilization_pct);
        }
    }
    
    return true;
}

/**
 * @brief Export statistics in binary format
 */
static bool stats_export_binary(FILE* file)
{
    /* Write magic number and version */
    uint32_t magic = 0x53544154;  /* 'STAT' */
    uint16_t version = 0x0100;
    
    fwrite(&magic, sizeof(magic), 1, file);
    fwrite(&version, sizeof(version), 1, file);
    
    /* Write global statistics */
    fwrite(&stats_ctx.global_stats, sizeof(global_stats_t), 1, file);
    
    /* Write per-NIC statistics */
    for (int i = 0; i < MAX_NICS_SUPPORTED; i++) {
        if (stats_ctx.nic_stats[i].active) {
            uint8_t nic_id = i;
            fwrite(&nic_id, sizeof(nic_id), 1, file);
            fwrite(&stats_ctx.nic_stats[i], sizeof(extended_nic_stats_t), 1, file);
        }
    }
    
    return true;
}

/* ============================================================================
 * API Functions
 * ============================================================================ */

static bool stats_api_get_nic_stats(uint8_t nic_id, extended_nic_stats_t* stats)
{
    if (nic_id >= MAX_NICS_SUPPORTED || !stats) {
        return false;
    }
    
    *stats = stats_ctx.nic_stats[nic_id];
    return true;
}

static bool stats_api_get_global_stats(global_stats_t* stats)
{
    if (!stats) {
        return false;
    }
    
    *stats = stats_ctx.global_stats;
    return true;
}

static bool stats_api_reset_stats(uint8_t nic_id)
{
    if (nic_id >= MAX_NICS_SUPPORTED) {
        return false;
    }
    
    memset(&stats_ctx.nic_stats[nic_id], 0, sizeof(extended_nic_stats_t));
    stats_ctx.nic_stats[nic_id].min_packet_time = 0xFFFFFFFF;
    
    return true;
}

static bool stats_api_set_collection_interval(uint32_t interval_ms)
{
    if (interval_ms < 100 || interval_ms > 60000) {
        return false;
    }
    
    stats_ctx.collection_interval = interval_ms;
    
    /* Update all NIC intervals */
    for (int i = 0; i < MAX_NICS_SUPPORTED; i++) {
        stats_ctx.nic_stats[i].sample_interval = interval_ms;
    }
    
    return true;
}

static bool stats_api_export_stats(const char* filename, uint8_t format)
{
    if (format > EXPORT_FORMAT_BINARY) {
        return false;
    }
    
    return stats_export_to_file(filename, (export_format_t)format);
}

static bool stats_api_enable_categories(uint16_t categories)
{
    stats_ctx.collection_categories = categories;
    stats_ctx.global_stats.enabled_categories = categories;
    
    return true;
}
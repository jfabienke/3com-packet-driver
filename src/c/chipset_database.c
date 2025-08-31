/**
 * @file chipset_database.c
 * @brief Community chipset behavior database
 *
 * 3Com Packet Driver - Chipset Database
 *
 * This module implements the community chipset behavior database that
 * records real-world testing results from actual hardware configurations.
 * It builds a comprehensive knowledge base of chipset behavior for the
 * retro computing community.
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include "../include/chipset_database.h"
#include "../include/chipset_detect.h"
#include "../include/cache_coherency.h"
#include "../include/cpu_detect.h"
#include "../include/logging.h"
#include "../include/hardware.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Database configuration */
static chipset_database_config_t database_config = {0};
static bool database_initialized = false;

/* Export file handles */
static FILE* csv_export_file = NULL;
static FILE* json_export_file = NULL;

/* Statistics tracking */
static chipset_database_stats_t database_stats = {0};

/* Forward declarations */
static bool open_export_files(void);
static void close_export_files(void);
static bool write_csv_header(void);
static bool write_csv_record(const chipset_test_record_t* record);
static bool write_json_record(const chipset_test_record_t* record);
static uint32_t generate_submission_id(void);
static void update_database_statistics(const chipset_test_record_t* record);

/**
 * Initialize the chipset database system
 */
bool initialize_chipset_database(const chipset_database_config_t* config) {
    if (!config) {
        log_error("Invalid database configuration provided");
        return false;
    }
    
    log_info("Initializing chipset database system...");
    
    /* Configure database settings */
    database_config = *config;
    
    /* Initialize statistics */
    memset(&database_stats, 0, sizeof(database_stats));
    database_stats.initialization_time = get_current_timestamp();
    
    /* Open export files if enabled */
    if (database_config.enable_export) {
        if (!open_export_files()) {
            log_warning("Failed to open export files - continuing without export");
            database_config.enable_export = false;
        }
    }
    
    database_initialized = true;
    
    log_info("Chipset database initialized - export: %s", 
             database_config.enable_export ? "enabled" : "disabled");
    
    return true;
}

/**
 * Record a chipset test result in the database
 */
bool record_chipset_test_result(const coherency_analysis_t* coherency_analysis,
                               const chipset_detection_result_t* chipset_detection) {
    chipset_test_record_t record = {0};
    
    if (!database_initialized) {
        log_warning("Database not initialized - cannot record test result");
        return false;
    }
    
    if (!coherency_analysis) {
        log_error("Invalid coherency analysis provided");
        return false;
    }
    
    log_debug("Recording chipset test result...");
    
    /* Fill in hardware identification */
    if (chipset_detection && chipset_detection->chipset.found) {
        record.chipset_vendor_id = chipset_detection->chipset.vendor_id;
        record.chipset_device_id = chipset_detection->chipset.device_id;
        strncpy(record.chipset_name, chipset_detection->chipset.name, 
                sizeof(record.chipset_name) - 1);
    } else {
        record.chipset_vendor_id = 0x0000;
        record.chipset_device_id = 0x0000;
        strcpy(record.chipset_name, "Unknown");
    }
    
    /* CPU information */
    record.cpu_family = coherency_analysis->cpu.family;
    record.cpu_model = coherency_analysis->cpu.model;
    record.cpu_speed_mhz = coherency_analysis->cpu.speed_mhz;
    
    /* Cache configuration */
    record.cache_enabled = coherency_analysis->cache_enabled;
    record.write_back_cache = coherency_analysis->write_back_cache;
    record.cache_size_kb = coherency_analysis->cpu.cache_size;
    record.cache_line_size = coherency_analysis->cpu.cache_line_size;
    
    /* Test results */
    record.bus_master_result = coherency_analysis->bus_master;
    record.coherency_result = coherency_analysis->coherency;
    record.snooping_result = coherency_analysis->snooping;
    record.selected_tier = coherency_analysis->selected_tier;
    
    /* System information */
    record.is_pci_system = (chipset_detection && 
                           chipset_detection->detection_method == CHIPSET_DETECT_PCI_SUCCESS);
    
    /* Get BIOS information if available */
    get_bios_information(record.bios_vendor, sizeof(record.bios_vendor),
                        record.bios_version, sizeof(record.bios_version));
    
    /* Test metadata */
    record.test_date = get_current_timestamp();
    record.driver_version = DRIVER_VERSION;
    record.test_confidence = coherency_analysis->confidence;
    record.submission_id = generate_submission_id();
    
    /* Export to files if enabled */
    if (database_config.enable_export) {
        if (database_config.export_csv && csv_export_file) {
            if (!write_csv_record(&record)) {
                log_warning("Failed to write CSV record");
            }
        }
        
        if (database_config.export_json && json_export_file) {
            if (!write_json_record(&record)) {
                log_warning("Failed to write JSON record");
            }
        }
    }
    
    /* Update statistics */
    update_database_statistics(&record);
    
    log_info("Test result recorded: ID=%08X, Chipset=%s, Tier=%d", 
             record.submission_id, record.chipset_name, record.selected_tier);
    
    return true;
}

/**
 * Open export files for writing
 */
static bool open_export_files(void) {
    bool success = true;
    
    /* Open CSV file */
    if (database_config.export_csv) {
        csv_export_file = fopen(database_config.csv_filename, "a");
        if (!csv_export_file) {
            log_error("Failed to open CSV export file: %s", database_config.csv_filename);
            success = false;
        } else {
            /* Check if file is empty and write header */
            fseek(csv_export_file, 0, SEEK_END);
            if (ftell(csv_export_file) == 0) {
                if (!write_csv_header()) {
                    log_warning("Failed to write CSV header");
                }
            }
        }
    }
    
    /* Open JSON file */
    if (database_config.export_json) {
        json_export_file = fopen(database_config.json_filename, "a");
        if (!json_export_file) {
            log_error("Failed to open JSON export file: %s", database_config.json_filename);
            success = false;
        }
    }
    
    return success;
}

/**
 * Close export files
 */
static void close_export_files(void) {
    if (csv_export_file) {
        fclose(csv_export_file);
        csv_export_file = NULL;
    }
    
    if (json_export_file) {
        fclose(json_export_file);
        json_export_file = NULL;
    }
}

/**
 * Write CSV header
 */
static bool write_csv_header(void) {
    if (!csv_export_file) {
        return false;
    }
    
    fprintf(csv_export_file, 
            "submission_id,test_date,chipset_vendor,chipset_device,chipset_name,"
            "cpu_family,cpu_model,cpu_speed_mhz,cache_enabled,write_back_cache,"
            "cache_size_kb,cache_line_size,bus_master_result,coherency_result,"
            "snooping_result,selected_tier,is_pci_system,bios_vendor,bios_version,"
            "driver_version,test_confidence\n");
    
    fflush(csv_export_file);
    return true;
}

/**
 * Write CSV record
 */
static bool write_csv_record(const chipset_test_record_t* record) {
    if (!csv_export_file || !record) {
        return false;
    }
    
    fprintf(csv_export_file,
            "%08X,%u,%04X,%04X,\"%s\",%u,%u,%u,%s,%s,%u,%u,%d,%d,%d,%d,%s,\"%s\",\"%s\",%04X,%u\n",
            record->submission_id,
            record->test_date,
            record->chipset_vendor_id,
            record->chipset_device_id,
            record->chipset_name,
            record->cpu_family,
            record->cpu_model,
            record->cpu_speed_mhz,
            record->cache_enabled ? "true" : "false",
            record->write_back_cache ? "true" : "false",
            record->cache_size_kb,
            record->cache_line_size,
            record->bus_master_result,
            record->coherency_result,
            record->snooping_result,
            record->selected_tier,
            record->is_pci_system ? "true" : "false",
            record->bios_vendor,
            record->bios_version,
            record->driver_version,
            record->test_confidence);
    
    fflush(csv_export_file);
    return true;
}

/**
 * Write JSON record
 */
static bool write_json_record(const chipset_test_record_t* record) {
    if (!json_export_file || !record) {
        return false;
    }
    
    fprintf(json_export_file, "{\n");
    fprintf(json_export_file, "  \"submission_id\": \"%08X\",\n", record->submission_id);
    fprintf(json_export_file, "  \"test_date\": %u,\n", record->test_date);
    fprintf(json_export_file, "  \"hardware\": {\n");
    fprintf(json_export_file, "    \"chipset\": {\n");
    fprintf(json_export_file, "      \"vendor_id\": \"0x%04X\",\n", record->chipset_vendor_id);
    fprintf(json_export_file, "      \"device_id\": \"0x%04X\",\n", record->chipset_device_id);
    fprintf(json_export_file, "      \"name\": \"%s\"\n", record->chipset_name);
    fprintf(json_export_file, "    },\n");
    fprintf(json_export_file, "    \"cpu\": {\n");
    fprintf(json_export_file, "      \"family\": %u,\n", record->cpu_family);
    fprintf(json_export_file, "      \"model\": %u,\n", record->cpu_model);
    fprintf(json_export_file, "      \"speed_mhz\": %u\n", record->cpu_speed_mhz);
    fprintf(json_export_file, "    },\n");
    fprintf(json_export_file, "    \"cache\": {\n");
    fprintf(json_export_file, "      \"enabled\": %s,\n", record->cache_enabled ? "true" : "false");
    fprintf(json_export_file, "      \"write_back\": %s,\n", record->write_back_cache ? "true" : "false");
    fprintf(json_export_file, "      \"size_kb\": %u,\n", record->cache_size_kb);
    fprintf(json_export_file, "      \"line_size\": %u\n", record->cache_line_size);
    fprintf(json_export_file, "    }\n");
    fprintf(json_export_file, "  },\n");
    fprintf(json_export_file, "  \"test_results\": {\n");
    fprintf(json_export_file, "    \"bus_master\": %d,\n", record->bus_master_result);
    fprintf(json_export_file, "    \"coherency\": %d,\n", record->coherency_result);
    fprintf(json_export_file, "    \"snooping\": %d,\n", record->snooping_result);
    fprintf(json_export_file, "    \"selected_tier\": %d\n", record->selected_tier);
    fprintf(json_export_file, "  },\n");
    fprintf(json_export_file, "  \"system_info\": {\n");
    fprintf(json_export_file, "    \"pci_system\": %s,\n", record->is_pci_system ? "true" : "false");
    fprintf(json_export_file, "    \"bios_vendor\": \"%s\",\n", record->bios_vendor);
    fprintf(json_export_file, "    \"bios_version\": \"%s\"\n", record->bios_version);
    fprintf(json_export_file, "  },\n");
    fprintf(json_export_file, "  \"metadata\": {\n");
    fprintf(json_export_file, "    \"driver_version\": \"0x%04X\",\n", record->driver_version);
    fprintf(json_export_file, "    \"test_confidence\": %u\n", record->test_confidence);
    fprintf(json_export_file, "  }\n");
    fprintf(json_export_file, "},\n");
    
    fflush(json_export_file);
    return true;
}

/**
 * Generate unique submission ID
 */
static uint32_t generate_submission_id(void) {
    static uint32_t counter = 0;
    uint32_t timestamp = get_current_timestamp();
    
    /* Combine timestamp with counter for uniqueness */
    return (timestamp & 0xFFFFFF00) | (++counter & 0xFF);
}

/**
 * Update database statistics
 */
static void update_database_statistics(const chipset_test_record_t* record) {
    if (!record) {
        return;
    }
    
    database_stats.total_submissions++;
    
    /* Track chipset types */
    if (record->is_pci_system) {
        database_stats.pci_systems++;
    } else {
        database_stats.pre_pci_systems++;
    }
    
    /* Track cache configurations */
    if (record->write_back_cache) {
        database_stats.write_back_systems++;
    } else {
        database_stats.write_through_systems++;
    }
    
    /* Track test results */
    switch (record->bus_master_result) {
        case BUS_MASTER_OK:
            database_stats.bus_master_ok++;
            break;
        case BUS_MASTER_PARTIAL:
            database_stats.bus_master_partial++;
            break;
        case BUS_MASTER_BROKEN:
            database_stats.bus_master_broken++;
            break;
    }
    
    switch (record->coherency_result) {
        case COHERENCY_OK:
            database_stats.coherency_ok++;
            break;
        case COHERENCY_PROBLEM:
            database_stats.coherency_problems++;
            break;
        case COHERENCY_UNKNOWN:
            database_stats.coherency_unknown++;
            break;
    }
    
    switch (record->snooping_result) {
        case SNOOPING_FULL:
            database_stats.snooping_full++;
            break;
        case SNOOPING_PARTIAL:
            database_stats.snooping_partial++;
            break;
        case SNOOPING_NONE:
            database_stats.snooping_none++;
            break;
        case SNOOPING_UNKNOWN:
            database_stats.snooping_unknown++;
            break;
    }
    
    /* Track tier selections */
    switch (record->selected_tier) {
        case CACHE_TIER_1_CLFLUSH:
            database_stats.tier1_selections++;
            break;
        case CACHE_TIER_2_WBINVD:
            database_stats.tier2_selections++;
            break;
        case CACHE_TIER_3_SOFTWARE:
            database_stats.tier3_selections++;
            break;
        case CACHE_TIER_4_FALLBACK:
            database_stats.tier4_selections++;
            break;
        case TIER_DISABLE_BUS_MASTER:
            database_stats.disabled_bus_master++;
            break;
    }
}

/**
 * Get database statistics
 */
chipset_database_stats_t get_database_statistics(void) {
    return database_stats;
}

/**
 * Print database statistics
 */
void print_database_statistics(void) {
    printf("\n=== Chipset Database Statistics ===\n");
    printf("Total Submissions: %u\n", database_stats.total_submissions);
    printf("PCI Systems: %u\n", database_stats.pci_systems);
    printf("Pre-PCI Systems: %u\n", database_stats.pre_pci_systems);
    printf("Write-Back Cache: %u\n", database_stats.write_back_systems);
    printf("Write-Through Cache: %u\n", database_stats.write_through_systems);
    printf("\n");
    printf("Bus Master Results:\n");
    printf("  OK: %u\n", database_stats.bus_master_ok);
    printf("  Partial: %u\n", database_stats.bus_master_partial);
    printf("  Broken: %u\n", database_stats.bus_master_broken);
    printf("\n");
    printf("Coherency Results:\n");
    printf("  OK: %u\n", database_stats.coherency_ok);
    printf("  Problems: %u\n", database_stats.coherency_problems);
    printf("  Unknown: %u\n", database_stats.coherency_unknown);
    printf("\n");
    printf("Snooping Results:\n");
    printf("  Full: %u\n", database_stats.snooping_full);
    printf("  Partial: %u\n", database_stats.snooping_partial);
    printf("  None: %u\n", database_stats.snooping_none);
    printf("  Unknown: %u\n", database_stats.snooping_unknown);
    printf("\n");
    printf("Tier Selections:\n");
    printf("  Tier 1 (CLFLUSH): %u\n", database_stats.tier1_selections);
    printf("  Tier 2 (WBINVD): %u\n", database_stats.tier2_selections);
    printf("  Tier 3 (Software): %u\n", database_stats.tier3_selections);
    printf("  Tier 4 (Fallback): %u\n", database_stats.tier4_selections);
    printf("  Disabled: %u\n", database_stats.disabled_bus_master);
    printf("==================================\n");
}

/**
 * Display community contribution message
 */
void display_community_contribution_message(const chipset_test_record_t* record) {
    if (!record) {
        return;
    }
    
    printf("\n📊 COMMUNITY CONTRIBUTION:\n");
    printf("Your test results have been recorded for the community!\n");
    printf("\n");
    printf("Submission ID: %08X\n", record->submission_id);
    printf("Chipset: %s\n", record->chipset_name);
    printf("Selected Tier: %d\n", record->selected_tier);
    printf("Test Confidence: %u%%\n", record->test_confidence);
    printf("\n");
    printf("This data helps improve driver compatibility for the\n");
    printf("entire retro computing community. Thank you!\n");
    
    if (database_config.enable_export) {
        printf("\n");
        printf("Test data exported to:\n");
        if (database_config.export_csv) {
            printf("  CSV: %s\n", database_config.csv_filename);
        }
        if (database_config.export_json) {
            printf("  JSON: %s\n", database_config.json_filename);
        }
        printf("\n");
        printf("You can share these files with the community at:\n");
        printf("https://github.com/3com-packet-driver/chipset-database\n");
    }
}

/**
 * Cleanup database resources
 */
void cleanup_chipset_database(void) {
    if (!database_initialized) {
        return;
    }
    
    close_export_files();
    database_initialized = false;
    
    log_info("Chipset database cleanup completed");
}

/**
 * Get BIOS information (simplified implementation)
 */
void get_bios_information(char* vendor, size_t vendor_size, 
                         char* version, size_t version_size) {
    /* Simplified BIOS detection - real implementation would 
       read from BIOS data area or DMI tables */
    strncpy(vendor, "Unknown", vendor_size - 1);
    vendor[vendor_size - 1] = '\0';
    
    strncpy(version, "Unknown", version_size - 1);
    version[version_size - 1] = '\0';
    
    /* BIOS string detection using DOS system calls and memory scanning */
}
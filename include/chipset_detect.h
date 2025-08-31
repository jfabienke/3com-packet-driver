/**
 * @file chipset_detect.h
 * @brief Safe chipset detection declarations
 *
 * 3Com Packet Driver - Safe Chipset Detection
 *
 * This header defines the interface for safe chipset detection using
 * only standardized PCI methods. NO risky I/O port probing is performed.
 * All chipset information is used for diagnostic purposes only.
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef CHIPSET_DETECT_H
#define CHIPSET_DETECT_H

#include <stdint.h>
#include <stdbool.h>

/* Chipset era classification */
typedef enum {
    CHIPSET_ERA_UNKNOWN,    /* Unknown or undetectable */
    CHIPSET_ERA_ISA,        /* Pure ISA chipsets (pre-1991) */
    CHIPSET_ERA_EISA,       /* EISA chipsets (1991-1993) */
    CHIPSET_ERA_VLB,        /* VESA Local Bus (1992-1995) */
    CHIPSET_ERA_PCI         /* PCI chipsets (1993+) */
} chipset_era_t;

/* Chipset detection method */
typedef enum {
    CHIPSET_DETECT_NONE,        /* No safe detection method available */
    CHIPSET_DETECT_PCI_SUCCESS, /* Successfully detected via PCI */
    CHIPSET_DETECT_PCI_FAILED   /* PCI BIOS present but detection failed */
} chipset_detection_method_t;

/* Chipset detection confidence level */
typedef enum {
    CHIPSET_CONFIDENCE_UNKNOWN, /* No detection possible */
    CHIPSET_CONFIDENCE_LOW,     /* Limited information available */
    CHIPSET_CONFIDENCE_MEDIUM,  /* PCI detected but chipset unknown */
    CHIPSET_CONFIDENCE_HIGH     /* Known chipset in database */
} chipset_confidence_t;

/* Basic chipset information */
typedef struct {
    uint16_t vendor_id;             /* PCI vendor ID */
    uint16_t device_id;             /* PCI device ID */
    char name[64];                  /* Human-readable chipset name */
    chipset_era_t era;              /* Chipset era classification */
    bool found;                     /* Whether chipset was detected */
    bool supports_bus_master;       /* Chipset supports bus mastering */
    bool reliable_snooping;         /* Documented reliable cache snooping */
} chipset_info_t;

/* Additional PCI device information */
#define MAX_ADDITIONAL_PCI_DEVICES 16

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
} pci_device_info_t;

typedef struct {
    pci_device_info_t pci_devices[MAX_ADDITIONAL_PCI_DEVICES];
    uint8_t pci_device_count;
    uint8_t total_pci_devices_found;
    bool has_isa_bridge;
    char isa_bridge_name[32];
} chipset_additional_info_t;

/* Complete chipset detection result */
typedef struct {
    chipset_detection_method_t detection_method;   /* How chipset was detected */
    chipset_confidence_t confidence;               /* Confidence in detection */
    chipset_info_t chipset;                       /* Detected chipset information */
    chipset_additional_info_t additional_info;    /* Additional PCI devices found */
    char diagnostic_info[128];                    /* Human-readable diagnostic info */
} chipset_detection_result_t;

/* Chipset-based recommendation */
typedef struct {
    bool use_runtime_testing;      /* Always true - runtime testing is primary */
    bool expect_cache_management;  /* Expect cache management to be needed */
    bool expect_no_snooping;       /* Expect no hardware snooping */
    char reasoning[128];           /* Explanation for recommendation */
} chipset_recommendation_t;

/* Function declarations */

/* Main detection functions */
chipset_detection_result_t detect_system_chipset(void);
chipset_additional_info_t scan_additional_pci_devices(void);

/* Chipset database lookups */
bool chipset_supports_reliable_snooping(const chipset_info_t* chipset);
bool chipset_era_supports_bus_master(chipset_era_t era);

/* Recommendation generation */
chipset_recommendation_t generate_chipset_recommendation(const chipset_detection_result_t* detection);

/* Information and display functions */
const char* get_chipset_confidence_description(chipset_confidence_t confidence);
const char* get_chipset_detection_method_description(chipset_detection_method_t method);
void print_chipset_detection_results(const chipset_detection_result_t* result);

/* Utility functions */
bool is_pci_system(void);
bool is_safe_to_detect_chipset(void);
const char* get_chipset_era_description(chipset_era_t era);

/* Constants for chipset detection */
#define CHIPSET_VENDOR_INTEL    0x8086
#define CHIPSET_VENDOR_VIA      0x1106
#define CHIPSET_VENDOR_SIS      0x1039
#define CHIPSET_VENDOR_ALI      0x10B9
#define CHIPSET_VENDOR_OPTI     0x1045
#define CHIPSET_VENDOR_AMD      0x1022

/* Intel chipset device IDs */
#define CHIPSET_INTEL_82437FX   0x122D  /* Triton */
#define CHIPSET_INTEL_82437VX   0x7030  /* Triton II */
#define CHIPSET_INTEL_82439TX   0x7100  /* 430TX */
#define CHIPSET_INTEL_82450GX   0x84C4  /* Orion (server) */
#define CHIPSET_INTEL_82441FX   0x1237  /* Natoma */

/* VIA chipset device IDs */
#define CHIPSET_VIA_VT82C585VP  0x0585  /* Apollo VP */
#define CHIPSET_VIA_VT82C595    0x0595  /* Apollo VP2 */
#define CHIPSET_VIA_VT82C597    0x0597  /* Apollo VP3 */

/* SiS chipset device IDs */
#define CHIPSET_SIS_496         0x0496  /* SiS 496/497 */
#define CHIPSET_SIS_5571        0x5571  /* Trinity */

/* Detection result validation macros */
#define IS_VALID_CHIPSET_DETECTION(result) \
    ((result) && (result)->detection_method != CHIPSET_DETECT_NONE)

#define CHIPSET_DETECTION_RELIABLE(result) \
    ((result) && (result)->confidence >= CHIPSET_CONFIDENCE_MEDIUM)

#define CHIPSET_HAS_DOCUMENTED_SNOOPING(result) \
    ((result) && (result)->chipset.found && (result)->chipset.reliable_snooping)

#define IS_PCI_ERA_CHIPSET(result) \
    ((result) && (result)->chipset.era == CHIPSET_ERA_PCI)

/* Safety validation macros */
#define SAFE_FOR_PCI_DETECTION() \
    (is_pci_system())

#define SHOULD_SKIP_CHIPSET_DETECTION() \
    (!is_safe_to_detect_chipset())

/* Diagnostic macros */
#define CHIPSET_SUPPORTS_BUS_MASTER(info) \
    ((info) && (info)->supports_bus_master)

#define CHIPSET_ERA_MODERN(era) \
    ((era) >= CHIPSET_ERA_VLB)

#endif /* CHIPSET_DETECT_H */
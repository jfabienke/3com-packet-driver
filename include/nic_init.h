/**
 * @file nic_init.h
 * @brief NIC-specific initialization routines
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef _NIC_INIT_H_
#define _NIC_INIT_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"
#include "nic_defs.h"
#include "hardware.h"
#include "3c509b.h"
#include "3c515.h"

/* NIC initialization configuration structure */
typedef struct nic_init_config {
    nic_type_t nic_type;                        /* Type of NIC */
    uint16_t io_base;                           /* I/O base address */
    uint8_t irq;                                /* IRQ line */
    uint8_t dma_channel;                        /* DMA channel (if used) */
    uint32_t flags;                             /* Initialization flags */
    bool auto_detect;                           /* Auto-detect settings */
    bool force_settings;                        /* Force manual settings */
} nic_init_config_t;

/* NIC initialization flags */
#define NIC_INIT_FLAG_AUTO_IRQ          BIT(0)  /* Auto-detect IRQ */
#define NIC_INIT_FLAG_AUTO_IO           BIT(1)  /* Auto-detect I/O base */
#define NIC_INIT_FLAG_SKIP_TEST         BIT(2)  /* Skip self-test */
#define NIC_INIT_FLAG_FULL_DUPLEX       BIT(3)  /* Force full duplex */
#define NIC_INIT_FLAG_10MBPS            BIT(4)  /* Force 10 Mbps */
#define NIC_INIT_FLAG_100MBPS           BIT(5)  /* Force 100 Mbps */
#define NIC_INIT_FLAG_PROMISCUOUS       BIT(6)  /* Enable promiscuous mode */
#define NIC_INIT_FLAG_NO_RESET          BIT(7)  /* Skip hardware reset */

/* Bus type constants */
#define NIC_BUS_UNKNOWN     0x00    /* Unknown bus type */
#define NIC_BUS_ISA         0x01    /* ISA bus */
#define NIC_BUS_EISA        0x02    /* EISA bus */
#define NIC_BUS_MCA         0x03    /* MicroChannel Architecture */
#define NIC_BUS_PCI         0x04    /* PCI bus */
#define NIC_BUS_PCMCIA      0x05    /* PC Card/PCMCIA */
#define NIC_BUS_CARDBUS     0x06    /* CardBus */

/* PCI-specific generic information structure */
typedef struct pci_generic_info {
    /* Base Address Registers (BARs) */
    struct {
        uint32_t address;               /* BAR base address */
        uint32_t size;                  /* BAR size (0 if not sized) */
        uint8_t type;                   /* 0=memory, 1=I/O, 2=64-bit memory */
        uint8_t flags;                  /* Additional BAR flags */
    } bars[6];                          /* Up to 6 BARs per PCI device */
    
    /* PCI Capabilities discovered */
    struct {
        uint8_t power_mgmt_cap;         /* Power Management capability offset */
        uint8_t msi_cap;                /* MSI capability offset */
        uint8_t msix_cap;               /* MSI-X capability offset */
        uint8_t pci_express_cap;        /* PCIe capability offset */
        uint8_t vpd_cap;                /* Vital Product Data capability offset */
        uint8_t reserved[3];            /* Reserved for future capabilities */
    } capabilities;
    
    /* 3Com generation and capabilities (from 3com_pci.h) */
    uint8_t generation;                 /* IS_VORTEX, IS_BOOMERANG, IS_CYCLONE, IS_TORNADO */
    uint16_t hw_capabilities;           /* HAS_MII, HAS_HWCKSM, etc. */
    uint8_t io_size;                    /* I/O region size (32, 64, or 128 bytes) */
    
    /* Device characteristics from PCI config space */
    uint8_t header_type;                /* PCI header type */
    uint8_t interrupt_line;             /* Interrupt line from config */
    uint8_t interrupt_pin;              /* Interrupt pin (A=1, B=2, C=3, D=4) */
    uint8_t latency_timer;              /* Latency timer value */
    uint16_t subsystem_vendor_id;       /* Subsystem vendor ID */
    uint16_t subsystem_device_id;       /* Subsystem device ID */
    uint16_t command_register;          /* PCI command register */
    uint16_t status_register;           /* PCI status register */
    
    /* Generic network controller information */
    uint8_t class_code;                 /* PCI class code (should be 0x02 for network) */
    uint8_t subclass_code;              /* PCI subclass (0x00=Ethernet, 0x01=Token Ring, etc.) */
    uint8_t prog_interface;             /* Programming interface */
    uint8_t multifunction;              /* 1 if multifunction device */
} pci_generic_info_t;

/* NIC detection information (extended for Phase 0A) */
typedef struct nic_detect_info {
    nic_type_t type;                            /* Detected NIC type */
    uint16_t vendor_id;                         /* Vendor ID */
    uint16_t device_id;                         /* Device ID */
    uint8_t revision;                           /* Revision */
    uint16_t io_base;                           /* I/O base address */
    uint8_t irq;                                /* IRQ line */
    uint8_t mac[ETH_ALEN];                      /* MAC address */
    uint32_t capabilities;                      /* Hardware capabilities */
    bool pnp_capable;                           /* Plug and Play capable */
    bool detected;                              /* Successfully detected */
    
    /* === Phase 0A Extensions: Enhanced Detection === */
    uint8_t variant_id;                         /* 3c509 family variant identifier */
    uint16_t media_capabilities;                /* Detected media capabilities */
    media_type_t detected_media;                /* Auto-detected media type */
    uint8_t detection_method;                   /* How the NIC was detected */
    uint16_t product_id;                        /* Product ID from EEPROM */
    uint32_t pnp_vendor_id;                     /* PnP vendor ID (if PnP) */
    uint32_t pnp_device_id;                     /* PnP device ID (if PnP) */
    uint8_t connector_type;                     /* Physical connector type */
    uint16_t special_features;                  /* Special hardware features */
    
    /* === Negotiated Link Parameters === */
    uint8_t negotiated_duplex;                  /* Negotiated duplex (0=half, 1=full) */
    uint16_t negotiated_speed;                  /* Negotiated speed (10, 100) */

    /* === Generic Bus Information === */
    uint8_t bus_type;                           /* Bus type (NIC_BUS_*) */
    
    /* PCI-specific location information */
    uint8_t pci_bus;                            /* PCI bus number */
    uint8_t pci_device;                         /* PCI device number */
    uint8_t pci_function;                       /* PCI function number */
    
    /* Generic PCI information (valid only if bus_type == NIC_BUS_PCI) */
    pci_generic_info_t pci_info;                /* Generic PCI device information */
} nic_detect_info_t;

/* Supported I/O base addresses for 3C509B */
static const uint16_t NIC_3C509B_IO_BASES[] = {
    0x200, 0x210, 0x220, 0x230, 0x240, 0x250, 0x260, 0x270,
    0x280, 0x290, 0x2A0, 0x2B0, 0x2C0, 0x2D0, 0x2E0, 0x2F0,
    0x300, 0x310, 0x320, 0x330, 0x340, 0x350, 0x360, 0x370,
    0x380, 0x390, 0x3A0, 0x3B0, 0x3C0, 0x3D0, 0x3E0, 0x3F0
};

/* Supported I/O base addresses for 3C515-TX */
static const uint16_t NIC_3C515_IO_BASES[] = {
    0x200, 0x210, 0x220, 0x230, 0x240, 0x250, 0x260, 0x270,
    0x280, 0x290, 0x2A0, 0x2B0, 0x2C0, 0x2D0, 0x2E0, 0x2F0,
    0x300, 0x310, 0x320, 0x330, 0x340, 0x350, 0x360, 0x370
};

/* Common IRQ lines for ISA NICs */
static const uint8_t NIC_COMMON_IRQS[] = {
    3, 5, 7, 9, 10, 11, 12, 15
};

/* Number of elements in arrays */
#define NIC_3C509B_IO_COUNT     (sizeof(NIC_3C509B_IO_BASES) / sizeof(uint16_t))
#define NIC_3C515_IO_COUNT      (sizeof(NIC_3C515_IO_BASES) / sizeof(uint16_t))
#define NIC_COMMON_IRQ_COUNT    (sizeof(NIC_COMMON_IRQS) / sizeof(uint8_t))

/* Detection method constants for Phase 0A */
#define DETECT_METHOD_UNKNOWN       0x00    /* Unknown detection method */
#define DETECT_METHOD_ISA_PROBE     0x01    /* Direct ISA I/O probing */
#define DETECT_METHOD_PNP           0x02    /* Plug and Play enumeration */
#define DETECT_METHOD_EISA          0x03    /* EISA configuration */
#define DETECT_METHOD_USER_CONFIG   0x04    /* User-specified configuration */
#define DETECT_METHOD_EEPROM_SCAN   0x05    /* EEPROM scanning */
#define DETECT_METHOD_AUTO_DETECT   0x06    /* Automatic detection */
#define DETECT_METHOD_VARIANT_DB    0x07    /* Variant database lookup */
#define DETECT_METHOD_PCI_SCAN      0x08    /* PCI bus scanning */
#define DETECT_METHOD_PCI_BIOS      0x09    /* PCI BIOS services */

/* Global NIC initialization state */
extern bool g_nic_init_system_ready;

/* Main NIC initialization functions */
int nic_init_system(void);
void nic_init_cleanup(void);
int nic_init_all_detected(void);
int nic_init_count_detected(void);

/* Individual NIC initialization */
int nic_init_single(nic_info_t *nic, const nic_init_config_t *config);
int nic_init_from_detection(nic_info_t *nic, const nic_detect_info_t *detect_info);
int nic_cleanup_single(nic_info_t *nic);
int nic_reset_single(nic_info_t *nic);

/* NIC detection functions */
int nic_detect_all(nic_detect_info_t *detect_list, int max_nics);
int nic_detect_3c509b(nic_detect_info_t *info_list, int max_count);
int nic_detect_3c515(nic_detect_info_t *info_list, int max_count);
bool nic_is_present_at_address(nic_type_t type, uint16_t io_base);

/* Hardware-specific initialization */
int nic_init_3c509b(nic_info_t *nic, const nic_init_config_t *config);
int nic_init_3c515(nic_info_t *nic, const nic_init_config_t *config);
int nic_configure_3c509b(nic_info_t *nic);
int nic_configure_3c515(nic_info_t *nic);

/* Hardware detection helpers */
/* Using int instead of bool for C89 compatibility (returns 0=false, 1=true) */
int nic_probe_3c509b_at_address(uint16_t io_base, nic_detect_info_t *info);
int nic_probe_3c515_at_address(uint16_t io_base, nic_detect_info_t *info);
int nic_read_mac_address_3c509b(uint16_t io_base, uint8_t *mac);
int nic_read_mac_address_3c515(uint16_t io_base, uint8_t *mac);

/* PnP and EISA detection */
int nic_detect_pnp_3c509b(nic_detect_info_t *info_list, int max_count);
int nic_detect_eisa_3c509b(void);
/* Using int instead of bool for C89 compatibility */
int nic_is_pnp_capable(uint16_t io_base);

/* IRQ detection and configuration */
int nic_detect_irq(nic_info_t *nic);
int nic_test_irq(nic_info_t *nic, uint8_t irq);
int nic_configure_irq(nic_info_t *nic, uint8_t irq);
bool nic_is_irq_available(uint8_t irq);

/* Speed and duplex configuration */
int nic_configure_speed_duplex(nic_info_t *nic, int speed, bool full_duplex);
int nic_auto_negotiate_speed(nic_info_t *nic);
int nic_detect_link_speed(nic_info_t *nic);
bool nic_is_link_up(nic_info_t *nic);

/* Media type configuration */
typedef enum {
    NIC_MEDIA_AUTO = 0,                         /* Auto-detect */
    NIC_MEDIA_10BASE_T,                         /* 10BASE-T */
    NIC_MEDIA_10BASE_2,                         /* 10BASE-2 (BNC) */
    NIC_MEDIA_AUI,                              /* AUI */
    NIC_MEDIA_100BASE_TX,                       /* 100BASE-TX */
    NIC_MEDIA_100BASE_FX                        /* 100BASE-FX */
} nic_media_type_t;

int nic_configure_media_type(nic_info_t *nic, nic_media_type_t media);
nic_media_type_t nic_detect_media_type(nic_info_t *nic);
const char* nic_media_type_to_string(nic_media_type_t media);

/* Buffer and DMA initialization */
int nic_init_buffers(nic_info_t *nic);
int nic_cleanup_buffers(nic_info_t *nic);
int nic_configure_dma(nic_info_t *nic);
int nic_test_dma(nic_info_t *nic);

/* Self-test and validation */
int nic_run_self_test(nic_info_t *nic);
int nic_validate_configuration(nic_info_t *nic);
int nic_test_packet_transmission(nic_info_t *nic);
int nic_test_loopback(nic_info_t *nic);

/* Power management */
int nic_set_power_state(nic_info_t *nic, int power_state);
int nic_wake_on_lan_configure(nic_info_t *nic, bool enable);
int nic_suspend(nic_info_t *nic);
int nic_resume(nic_info_t *nic);

/* Configuration helpers */
void nic_init_config_defaults(nic_init_config_t *config, nic_type_t type);
int nic_load_config_from_environment(nic_init_config_t *config);
int nic_save_config_to_nvram(nic_info_t *nic, const nic_init_config_t *config);
int nic_load_config_from_nvram(nic_info_t *nic, nic_init_config_t *config);

/* Status and information */
void nic_print_detection_info(const nic_detect_info_t *info);
void nic_print_initialization_status(const nic_info_t *nic);
void nic_print_capabilities(const nic_info_t *nic);
const char* nic_init_error_to_string(int error_code);

/* Advanced features */
int nic_configure_multicast_filter(nic_info_t *nic, const uint8_t *mc_list, int count);
int nic_configure_vlan_filtering(nic_info_t *nic, uint16_t *vlan_list, int count);
int nic_configure_flow_control(nic_info_t *nic, bool enable);
int nic_configure_checksum_offload(nic_info_t *nic, bool enable);

/* Error handling and recovery */
int nic_handle_init_error(nic_info_t *nic, int error_code);
int nic_recover_from_error(nic_info_t *nic);
int nic_reinitialize(nic_info_t *nic);

/* Statistics and monitoring */
typedef struct nic_init_stats {
    uint32_t total_detections;                  /* Total detection attempts */
    uint32_t successful_detections;             /* Successful detections */
    uint32_t total_initializations;             /* Total init attempts */
    uint32_t successful_initializations;        /* Successful initializations */
    uint32_t failed_initializations;            /* Failed initializations */
    uint32_t resets_performed;                  /* Hardware resets performed */
    uint32_t self_tests_run;                    /* Self-tests executed */
    uint32_t self_tests_passed;                 /* Self-tests passed */
} nic_init_stats_t;

/* Init statistics - always available (32 bytes - cheap telemetry)
 * Large diagnostic structs (coherency_analysis, chipset_detection) are
 * guarded by INIT_DIAG to save ~8KB DGROUP in release builds */
extern nic_init_stats_t g_nic_init_stats;

void nic_init_stats_clear(void);
const nic_init_stats_t* nic_init_get_stats(void);
void nic_init_print_stats(void);

/* Hardware register access helpers */
int nic_safe_register_read(nic_info_t *nic, uint16_t offset, uint16_t *value);
int nic_safe_register_write(nic_info_t *nic, uint16_t offset, uint16_t value);
int nic_wait_for_register_bit(nic_info_t *nic, uint16_t offset, uint16_t mask, 
                             bool set, uint32_t timeout_ms);

/* Timing and delay functions */
void nic_delay_microseconds(uint32_t microseconds);
void nic_delay_milliseconds(uint32_t milliseconds);
uint32_t nic_get_system_tick_count(void);

/* === Phase 0A Extensions: Enhanced Detection and Variant Management === */

/* Enhanced detection functions */
int nic_detect_with_variant_info(nic_detect_info_t *detect_list, int max_nics);
int nic_detect_specific_variant(uint8_t variant_id, nic_detect_info_t *info_list, int max_count);
int nic_enhanced_probe_at_address(uint16_t io_base, nic_detect_info_t *info);

/* Variant identification and management */
int nic_identify_variant_from_eeprom(uint16_t io_base, uint8_t *variant_id, uint16_t *product_id);
int nic_configure_variant_specific(nic_info_t *nic, const nic_detect_info_t *detect_info);
int nic_validate_variant_compatibility(uint8_t variant_id, const nic_init_config_t *config);

/* Media detection and configuration */
int nic_detect_available_media(nic_info_t *nic, uint16_t *media_mask);
int nic_auto_select_optimal_media(nic_info_t *nic);
int nic_configure_media_from_variant(nic_info_t *nic, uint8_t variant_id);
int nic_test_media_connectivity(nic_info_t *nic, media_type_t media);

/* Auto-negotiation management */
int nic_enable_auto_negotiation(nic_info_t *nic);
int nic_disable_auto_negotiation(nic_info_t *nic);
int nic_restart_auto_negotiation(nic_info_t *nic);
int nic_get_auto_negotiation_status(nic_info_t *nic, uint8_t *status_flags);
int nic_configure_auto_negotiation_params(nic_info_t *nic, uint16_t advertise_mask);

/* PnP integration functions */
int nic_detect_pnp_with_variants(nic_detect_info_t *info_list, int max_count);
int nic_configure_from_pnp_data(nic_info_t *nic, const pnp_device_id_t *pnp_info);
int nic_validate_pnp_configuration(const nic_detect_info_t *detect_info);

/* Enhanced hardware feature detection */
int nic_detect_special_features(nic_info_t *nic, uint16_t *feature_mask);
int nic_configure_hardware_features(nic_info_t *nic, uint16_t feature_mask);
int nic_test_hardware_features(nic_info_t *nic);

/* Configuration persistence and validation */
int nic_save_variant_config_to_eeprom(nic_info_t *nic);
int nic_load_variant_config_from_eeprom(nic_info_t *nic);
int nic_validate_eeprom_integrity(uint16_t io_base);

/* Advanced diagnostic and reporting */
int nic_generate_detection_report(const nic_detect_info_t *detect_list, int count, 
                                 char *report_buffer, size_t buffer_size);
int nic_print_variant_capabilities(const nic_info_t *nic);
int nic_print_media_status(const nic_info_t *nic);
const char* nic_detection_method_to_string(uint8_t method);

/* Utility functions for variant database access */
int nic_find_matching_variants(uint16_t product_id, uint8_t *variant_list, int max_variants);
int nic_get_variant_media_matrix(uint8_t variant_id, media_type_t *media_list, int max_media);
int nic_compare_variant_capabilities(uint8_t variant1, uint8_t variant2);

#ifdef __cplusplus
}
#endif

#endif /* _NIC_INIT_H_ */

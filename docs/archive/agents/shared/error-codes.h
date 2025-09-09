/* Error Codes - 3Com Packet Driver Modular Architecture
 * Version: 1.0
 * Date: 2025-08-22
 * 
 * Standardized error codes for all modules
 * DO NOT MODIFY without updating all agent prompts
 */

#ifndef ERROR_CODES_H
#define ERROR_CODES_H

/* Success */
#define SUCCESS                     0x0000

/* Generic Errors (0x0001-0x001F) */
#define ERROR_INVALID_PARAM         0x0001
#define ERROR_OUT_OF_MEMORY         0x0002
#define ERROR_NOT_IMPLEMENTED       0x0003
#define ERROR_ACCESS_DENIED         0x0004
#define ERROR_TIMEOUT               0x0005
#define ERROR_BUSY                  0x0006
#define ERROR_NOT_FOUND             0x0007
#define ERROR_ALREADY_EXISTS        0x0008
#define ERROR_INVALID_STATE         0x0009
#define ERROR_BUFFER_TOO_SMALL      0x000A
#define ERROR_CHECKSUM_FAILED       0x000B
#define ERROR_VERSION_MISMATCH      0x000C

/* Module Loader Errors (0x0020-0x003F) */
#define ERROR_MODULE_NOT_FOUND      0x0020
#define ERROR_MODULE_INVALID        0x0021
#define ERROR_MODULE_INCOMPATIBLE   0x0022
#define ERROR_MODULE_LOAD_FAILED    0x0023
#define ERROR_MODULE_INIT_FAILED    0x0024
#define ERROR_MODULE_ALREADY_LOADED 0x0025
#define ERROR_MODULE_DEPENDENCY     0x0026
#define ERROR_MODULE_ABI_MISMATCH   0x0027
#define ERROR_MODULE_CHECKSUM       0x0028
#define ERROR_MODULE_RELOCATION     0x0029
#define ERROR_MODULE_SYMBOL         0x002A

/* Hardware Errors (0x0040-0x005F) */
#define ERROR_HARDWARE_NOT_FOUND    0x0040
#define ERROR_HARDWARE_INIT_FAILED  0x0041
#define ERROR_HARDWARE_IO_ERROR     0x0042
#define ERROR_HARDWARE_TIMEOUT      0x0043
#define ERROR_HARDWARE_RESET_FAILED 0x0044
#define ERROR_HARDWARE_IRQ_CONFLICT 0x0045
#define ERROR_HARDWARE_DMA_ERROR    0x0046
#define ERROR_HARDWARE_LINK_DOWN    0x0047
#define ERROR_HARDWARE_EEPROM       0x0048
#define ERROR_HARDWARE_REGISTERS    0x0049

/* Network Errors (0x0060-0x007F) */
#define ERROR_PACKET_TOO_LARGE      0x0060
#define ERROR_PACKET_INVALID        0x0061
#define ERROR_PACKET_DROPPED        0x0062
#define ERROR_QUEUE_FULL            0x0063
#define ERROR_QUEUE_EMPTY           0x0064
#define ERROR_TX_UNDERRUN           0x0065
#define ERROR_RX_OVERRUN            0x0066
#define ERROR_COLLISION             0x0067
#define ERROR_CRC_ERROR             0x0068
#define ERROR_FRAME_ERROR           0x0069

/* Memory Management Errors (0x0080-0x009F) */
#define ERROR_XMS_NOT_AVAILABLE     0x0080
#define ERROR_XMS_ALLOCATION        0x0081
#define ERROR_XMS_HANDLE_INVALID    0x0082
#define ERROR_BUFFER_ALIGNMENT      0x0083
#define ERROR_DMA_BOUNDARY          0x0084
#define ERROR_CONVENTIONAL_MEMORY   0x0085
#define ERROR_UMB_NOT_AVAILABLE     0x0086
#define ERROR_POOL_EXHAUSTED        0x0087

/* Packet Driver API Errors (0x00A0-0x00BF) */
#define ERROR_PKTDRV_FUNCTION       0x00A0
#define ERROR_PKTDRV_HANDLE         0x00A1
#define ERROR_PKTDRV_TYPE           0x00A2
#define ERROR_PKTDRV_MODE           0x00A3
#define ERROR_PKTDRV_ADDRESS        0x00A4
#define ERROR_PKTDRV_NO_PACKETS     0x00A5
#define ERROR_PKTDRV_BAD_PACKET     0x00A6
#define ERROR_PKTDRV_CANT_SEND      0x00A7
#define ERROR_PKTDRV_CANT_SET       0x00A8
#define ERROR_PKTDRV_BAD_ADDRESS    0x00A9

/* PnP/Configuration Errors (0x00C0-0x00DF) */
#define ERROR_PNP_NO_CARDS          0x00C0
#define ERROR_PNP_ISOLATION_FAILED  0x00C1
#define ERROR_PNP_ACTIVATION_FAILED 0x00C2
#define ERROR_PNP_RESOURCE_CONFLICT 0x00C3
#define ERROR_PCI_CONFIG_READ       0x00C4
#define ERROR_PCI_CONFIG_WRITE      0x00C5
#define ERROR_PCMCIA_CIS_READ       0x00C6
#define ERROR_PCMCIA_TUPLE_INVALID  0x00C7
#define ERROR_CARDBUS_POWER         0x00C8

/* CPU/Optimization Errors (0x00E0-0x00FF) */
#define ERROR_CPU_DETECTION         0x00E0
#define ERROR_PATCH_FAILED          0x00E1
#define ERROR_PATCH_INVALID         0x00E2
#define ERROR_SMC_ALIGNMENT         0x00E3
#define ERROR_PREFETCH_FLUSH        0x00E4

/* Error Severity Masks */
#define ERROR_SEVERITY_MASK         0xF000
#define ERROR_SEVERITY_INFO         0x1000
#define ERROR_SEVERITY_WARNING      0x2000
#define ERROR_SEVERITY_ERROR        0x3000
#define ERROR_SEVERITY_CRITICAL     0x4000

/* Error Categories */
#define ERROR_CATEGORY_MASK         0x0F00
#define ERROR_CATEGORY_GENERIC      0x0000
#define ERROR_CATEGORY_MODULE       0x0100
#define ERROR_CATEGORY_HARDWARE     0x0200
#define ERROR_CATEGORY_NETWORK      0x0300
#define ERROR_CATEGORY_MEMORY       0x0400
#define ERROR_CATEGORY_API          0x0500
#define ERROR_CATEGORY_CONFIG       0x0600
#define ERROR_CATEGORY_CPU          0x0700

/* Macros for error handling */
#define IS_SUCCESS(err)             ((err) == SUCCESS)
#define IS_ERROR(err)               ((err) != SUCCESS)
#define GET_ERROR_SEVERITY(err)     ((err) & ERROR_SEVERITY_MASK)
#define GET_ERROR_CATEGORY(err)     ((err) & ERROR_CATEGORY_MASK)
#define GET_ERROR_CODE(err)         ((err) & 0x00FF)

#define MAKE_ERROR(severity, category, code) \
    ((severity) | (category) | (code))

/* Common error checking macros */
#define RETURN_IF_ERROR(expr) \
    do { \
        uint16_t _err = (expr); \
        if (IS_ERROR(_err)) return _err; \
    } while(0)

#define LOG_ERROR(err, msg) \
    log_error(__FILE__, __LINE__, (err), (msg))

#endif /* ERROR_CODES_H */
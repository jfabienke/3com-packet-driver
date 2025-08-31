/**
 * @file hal_errors.h
 * @brief Hardware Abstraction Layer error codes and return values
 *
 * Groups 6A & 6B - C Interface Architecture
 * Defines comprehensive error codes matching assembly layer definitions
 * with consistent error handling and reporting mechanisms.
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef _HAL_ERRORS_H_
#define _HAL_ERRORS_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"
#include <stdint.h>

/**
 * @brief HAL return codes and error definitions
 *
 * These error codes are designed to match assembly layer return values
 * and provide consistent error reporting across the hardware abstraction layer.
 * All error codes are negative values, with 0 indicating success.
 */

/* Success code */
#define HAL_SUCCESS                     0    /* Operation completed successfully */

/* Generic error codes (-1 to -19) */
#define HAL_ERROR_GENERIC              -1    /* Generic/unspecified error */
#define HAL_ERROR_INVALID_PARAM        -2    /* Invalid parameter */
#define HAL_ERROR_NOT_IMPLEMENTED      -3    /* Function not implemented */
#define HAL_ERROR_NOT_SUPPORTED        -4    /* Operation not supported */
#define HAL_ERROR_PERMISSION_DENIED    -5    /* Permission denied */
#define HAL_ERROR_BUSY                 -6    /* Resource busy */
#define HAL_ERROR_TIMEOUT              -7    /* Operation timed out */
#define HAL_ERROR_INTERRUPTED          -8    /* Operation interrupted */
#define HAL_ERROR_RETRY_NEEDED         -9    /* Retry operation */
#define HAL_ERROR_WOULD_BLOCK         -10    /* Operation would block */
#define HAL_ERROR_INVALID_STATE       -11    /* Invalid state for operation */
#define HAL_ERROR_CHECKSUM            -12    /* Checksum/verification error */
#define HAL_ERROR_VERSION_MISMATCH    -13    /* Version incompatibility */
#define HAL_ERROR_CONFIGURATION       -14    /* Configuration error */
#define HAL_ERROR_INITIALIZATION      -15    /* Initialization failure */
#define HAL_ERROR_CLEANUP             -16    /* Cleanup failure */
#define HAL_ERROR_RESOURCE_EXHAUSTED  -17    /* Resource exhausted */
#define HAL_ERROR_QUOTA_EXCEEDED      -18    /* Quota exceeded */
#define HAL_ERROR_INTERNAL            -19    /* Internal error */

/* Memory-related errors (-20 to -29) */
#define HAL_ERROR_MEMORY              -20    /* Memory allocation failure */
#define HAL_ERROR_OUT_OF_MEMORY       -21    /* Out of memory */
#define HAL_ERROR_MEMORY_CORRUPTED    -22    /* Memory corruption detected */
#define HAL_ERROR_INVALID_ADDRESS     -23    /* Invalid memory address */
#define HAL_ERROR_ALIGNMENT           -24    /* Memory alignment error */
#define HAL_ERROR_MEMORY_LOCKED       -25    /* Memory is locked */
#define HAL_ERROR_MEMORY_FRAGMENTED   -26    /* Memory too fragmented */
#define HAL_ERROR_BUFFER_TOO_SMALL    -27    /* Buffer too small */
#define HAL_ERROR_BUFFER_OVERFLOW     -28    /* Buffer overflow */
#define HAL_ERROR_DMA_MEMORY          -29    /* DMA memory error */

/* Hardware-related errors (-30 to -49) */
#define HAL_ERROR_HARDWARE_FAILURE    -30    /* General hardware failure */
#define HAL_ERROR_DEVICE_NOT_FOUND    -31    /* Hardware device not found */
#define HAL_ERROR_DEVICE_BUSY         -32    /* Hardware device busy */
#define HAL_ERROR_DEVICE_ERROR        -33    /* Device reported error */
#define HAL_ERROR_RESET_FAILED        -34    /* Hardware reset failed */
#define HAL_ERROR_SELF_TEST_FAILED    -35    /* Self-test failed */
#define HAL_ERROR_FIRMWARE_ERROR      -36    /* Firmware error */
#define HAL_ERROR_CALIBRATION_FAILED  -37    /* Hardware calibration failed */
#define HAL_ERROR_TEMPERATURE         -38    /* Temperature out of range */
#define HAL_ERROR_POWER_FAILURE       -39    /* Power supply failure */
#define HAL_ERROR_CLOCK_FAILURE       -40    /* Clock/timing failure */
#define HAL_ERROR_REGISTER_ACCESS     -41    /* Register access error */
#define HAL_ERROR_EEPROM_ERROR        -42    /* EEPROM access error */
#define HAL_ERROR_FLASH_ERROR         -43    /* Flash memory error */
#define HAL_ERROR_SENSOR_FAILURE      -44    /* Sensor failure */
#define HAL_ERROR_ACTUATOR_FAILURE    -45    /* Actuator failure */
#define HAL_ERROR_BUS_ERROR           -46    /* System bus error */
#define HAL_ERROR_ARBITRATION_LOST    -47    /* Bus arbitration lost */
#define HAL_ERROR_PROTOCOL_ERROR      -48    /* Hardware protocol error */
#define HAL_ERROR_HARDWARE_MISMATCH   -49    /* Hardware mismatch */

/* Network/Communication errors (-50 to -69) */
#define HAL_ERROR_LINK_DOWN           -50    /* Network link is down */
#define HAL_ERROR_NO_CARRIER          -51    /* No carrier signal */
#define HAL_ERROR_COLLISION           -52    /* Network collision */
#define HAL_ERROR_MEDIA_FAILURE       -53    /* Physical media failure */
#define HAL_ERROR_CABLE_FAULT         -54    /* Cable fault detected */
#define HAL_ERROR_CONNECTOR_FAULT     -55    /* Connector fault */
#define HAL_ERROR_SIGNAL_QUALITY      -56    /* Poor signal quality */
#define HAL_ERROR_SPEED_NEGOTIATION   -57    /* Speed negotiation failed */
#define HAL_ERROR_DUPLEX_MISMATCH     -58    /* Duplex mode mismatch */
#define HAL_ERROR_AUTONEG_FAILED      -59    /* Auto-negotiation failed */
#define HAL_ERROR_FLOW_CONTROL        -60    /* Flow control error */
#define HAL_ERROR_FRAME_ERROR         -61    /* Frame format error */
#define HAL_ERROR_CRC_ERROR           -62    /* CRC error */
#define HAL_ERROR_LENGTH_ERROR        -63    /* Length field error */
#define HAL_ERROR_JABBER              -64    /* Jabber condition */
#define HAL_ERROR_RUNT_FRAME          -65    /* Runt frame */
#define HAL_ERROR_OVERSIZED_FRAME     -66    /* Oversized frame */
#define HAL_ERROR_ALIGNMENT_ERROR     -67    /* Alignment error */
#define HAL_ERROR_PARITY_ERROR        -68    /* Parity error */
#define HAL_ERROR_SEQUENCING_ERROR    -69    /* Sequencing error */

/* DMA-related errors (-70 to -79) */
#define HAL_ERROR_DMA                 -70    /* General DMA error */
#define HAL_ERROR_DMA_TIMEOUT         -71    /* DMA operation timeout */
#define HAL_ERROR_DMA_UNDERRUN        -72    /* DMA underrun */
#define HAL_ERROR_DMA_OVERRUN         -73    /* DMA overrun */
#define HAL_ERROR_DMA_ALIGNMENT       -74    /* DMA alignment error */
#define HAL_ERROR_DMA_BUSY            -75    /* DMA engine busy */
#define HAL_ERROR_DMA_ABORT           -76    /* DMA operation aborted */
#define HAL_ERROR_DMA_DESCRIPTOR      -77    /* DMA descriptor error */
#define HAL_ERROR_DMA_COHERENCY       -78    /* DMA coherency error */
#define HAL_ERROR_DMA_MAPPING         -79    /* DMA mapping error */

/* Interrupt-related errors (-80 to -89) */
#define HAL_ERROR_INTERRUPT           -80    /* General interrupt error */
#define HAL_ERROR_IRQ_NOT_AVAILABLE   -81    /* IRQ not available */
#define HAL_ERROR_IRQ_CONFLICT        -82    /* IRQ conflict */
#define HAL_ERROR_IRQ_STORM           -83    /* Interrupt storm detected */
#define HAL_ERROR_IRQ_MISSED          -84    /* Missed interrupt */
#define HAL_ERROR_IRQ_SPURIOUS        -85    /* Spurious interrupt */
#define HAL_ERROR_IRQ_LATENCY         -86    /* Interrupt latency too high */
#define HAL_ERROR_IRQ_OVERLOAD        -87    /* Interrupt overload */
#define HAL_ERROR_IRQ_HANDLER         -88    /* Interrupt handler error */
#define HAL_ERROR_IRQ_MASK            -89    /* Interrupt mask error */

/* Driver/Software errors (-90 to -99) */
#define HAL_ERROR_DRIVER_VERSION      -90    /* Driver version error */
#define HAL_ERROR_API_MISMATCH        -91    /* API version mismatch */
#define HAL_ERROR_CONTEXT_INVALID     -92    /* Invalid context */
#define HAL_ERROR_HANDLE_INVALID      -93    /* Invalid handle */
#define HAL_ERROR_LOCK_FAILED         -94    /* Lock acquisition failed */
#define HAL_ERROR_DEADLOCK            -95    /* Deadlock detected */
#define HAL_ERROR_RACE_CONDITION      -96    /* Race condition */
#define HAL_ERROR_ASSERTION_FAILED    -97    /* Assertion failed */
#define HAL_ERROR_STACK_OVERFLOW      -98    /* Stack overflow */
#define HAL_ERROR_HEAP_CORRUPTION     -99    /* Heap corruption */

/* NIC-specific 3C509B errors (-100 to -119) */
#define HAL_ERROR_3C509B_ID_PORT      -100   /* ID port access error */
#define HAL_ERROR_3C509B_RESET        -101   /* 3C509B reset failed */
#define HAL_ERROR_3C509B_WINDOW       -102   /* Window selection error */
#define HAL_ERROR_3C509B_EEPROM       -103   /* EEPROM access error */
#define HAL_ERROR_3C509B_FIFO         -104   /* FIFO error */
#define HAL_ERROR_3C509B_TX_STUCK     -105   /* Transmitter stuck */
#define HAL_ERROR_3C509B_RX_STUCK     -106   /* Receiver stuck */
#define HAL_ERROR_3C509B_XCVR         -107   /* Transceiver error */
#define HAL_ERROR_3C509B_MEDIA        -108   /* Media detection error */
#define HAL_ERROR_3C509B_LINK_BEAT    -109   /* Link beat failure */
#define HAL_ERROR_3C509B_CONFIG       -110   /* Configuration error */
#define HAL_ERROR_3C509B_STATS        -111   /* Statistics error */
#define HAL_ERROR_3C509B_MULTICAST    -112   /* Multicast setup error */
#define HAL_ERROR_3C509B_LOOPBACK     -113   /* Loopback test failed */
#define HAL_ERROR_3C509B_COLLISION    -114   /* Collision handling error */
#define HAL_ERROR_3C509B_JABBER_GUARD -115   /* Jabber guard error */
#define HAL_ERROR_3C509B_CONNECTOR    -116   /* Connector error */
#define HAL_ERROR_3C509B_DIAGNOSTIC   -117   /* Diagnostic error */
#define HAL_ERROR_3C509B_THERMAL      -118   /* Thermal protection */
#define HAL_ERROR_3C509B_UNKNOWN      -119   /* Unknown 3C509B error */

/* NIC-specific 3C515-TX errors (-120 to -139) */
#define HAL_ERROR_3C515_BUSMASTER     -120   /* Bus mastering error */
#define HAL_ERROR_3C515_DESCRIPTOR    -121   /* Descriptor error */
#define HAL_ERROR_3C515_UPLOAD        -122   /* Upload DMA error */
#define HAL_ERROR_3C515_DOWNLOAD      -123   /* Download DMA error */
#define HAL_ERROR_3C515_STALL         -124   /* Stall condition */
#define HAL_ERROR_3C515_UNSTALL       -125   /* Unstall failed */
#define HAL_ERROR_3C515_RING_FULL     -126   /* Ring buffer full */
#define HAL_ERROR_3C515_RING_EMPTY    -127   /* Ring buffer empty */
#define HAL_ERROR_3C515_AUTONEG       -128   /* Auto-negotiation error */
#define HAL_ERROR_3C515_100BASETX     -129   /* 100BASE-TX error */
#define HAL_ERROR_3C515_MII           -130   /* MII interface error */
#define HAL_ERROR_3C515_PHY           -131   /* PHY error */
#define HAL_ERROR_3C515_PCI           -132   /* PCI interface error */
#define HAL_ERROR_3C515_POWER_MGMT    -133   /* Power management error */
#define HAL_ERROR_3C515_WAKE_ON_LAN   -134   /* Wake-on-LAN error */
#define HAL_ERROR_3C515_CHECKSUM_HW   -135   /* Hardware checksum error */
#define HAL_ERROR_3C515_VLAN          -136   /* VLAN handling error */
#define HAL_ERROR_3C515_FLOW_CTRL     -137   /* Flow control error */
#define HAL_ERROR_3C515_BOOMERANG     -138   /* Boomerang mode error */
#define HAL_ERROR_3C515_UNKNOWN       -139   /* Unknown 3C515-TX error */

/* Assembly layer interface errors (-140 to -149) */
#define HAL_ERROR_ASM_CALL_FAILED     -140   /* Assembly call failed */
#define HAL_ERROR_ASM_INVALID_OPCODE  -141   /* Invalid opcode */
#define HAL_ERROR_ASM_REGISTER_ERROR  -142   /* Register manipulation error */
#define HAL_ERROR_ASM_STACK_ERROR     -143   /* Stack manipulation error */
#define HAL_ERROR_ASM_CALLING_CONV    -144   /* Calling convention error */
#define HAL_ERROR_ASM_RETURN_CODE     -145   /* Invalid return code */
#define HAL_ERROR_ASM_PARAMETER       -146   /* Parameter passing error */
#define HAL_ERROR_ASM_ALIGNMENT       -147   /* Data alignment error */
#define HAL_ERROR_ASM_MEMORY_MODEL    -148   /* Memory model error */
#define HAL_ERROR_ASM_INTERFACE       -149   /* Interface mismatch */

/* Error severity levels */
typedef enum {
    HAL_SEVERITY_INFO     = 0,  /* Informational */
    HAL_SEVERITY_WARNING  = 1,  /* Warning condition */
    HAL_SEVERITY_ERROR    = 2,  /* Error condition */
    HAL_SEVERITY_CRITICAL = 3,  /* Critical error */
    HAL_SEVERITY_FATAL    = 4   /* Fatal error */
} hal_error_severity_t;

/* Error category classifications */
typedef enum {
    HAL_ERROR_CAT_GENERIC     = 0,  /* Generic errors */
    HAL_ERROR_CAT_MEMORY      = 1,  /* Memory-related */
    HAL_ERROR_CAT_HARDWARE    = 2,  /* Hardware-related */
    HAL_ERROR_CAT_NETWORK     = 3,  /* Network-related */
    HAL_ERROR_CAT_DMA         = 4,  /* DMA-related */
    HAL_ERROR_CAT_INTERRUPT   = 5,  /* Interrupt-related */
    HAL_ERROR_CAT_DRIVER      = 6,  /* Driver/software */
    HAL_ERROR_CAT_3C509B      = 7,  /* 3C509B specific */
    HAL_ERROR_CAT_3C515       = 8,  /* 3C515-TX specific */
    HAL_ERROR_CAT_ASSEMBLY    = 9   /* Assembly interface */
} hal_error_category_t;

/* Error information structure */
typedef struct hal_error_info {
    int error_code;                     /* Error code */
    hal_error_severity_t severity;      /* Error severity */
    hal_error_category_t category;      /* Error category */
    const char *message;               /* Error message */
    const char *description;           /* Detailed description */
    const char *recovery_hint;         /* Recovery suggestion */
} hal_error_info_t;

/* Function prototypes */

/**
 * @brief Convert error code to string representation
 * @param error_code HAL error code
 * @return String description of error code
 */
const char* hal_error_to_string(int error_code);

/**
 * @brief Get detailed error information
 * @param error_code HAL error code
 * @return Pointer to error information structure
 */
const hal_error_info_t* hal_get_error_info(int error_code);

/**
 * @brief Get error severity level
 * @param error_code HAL error code
 * @return Error severity level
 */
hal_error_severity_t hal_get_error_severity(int error_code);

/**
 * @brief Get error category
 * @param error_code HAL error code
 * @return Error category
 */
hal_error_category_t hal_get_error_category(int error_code);

/**
 * @brief Check if error code indicates success
 * @param error_code HAL error/return code
 * @return true if success, false if error
 */
static inline bool hal_is_success(int error_code) {
    return (error_code == HAL_SUCCESS);
}

/**
 * @brief Check if error code indicates failure
 * @param error_code HAL error/return code
 * @return true if error, false if success
 */
static inline bool hal_is_error(int error_code) {
    return (error_code < 0);
}

/**
 * @brief Check if error is recoverable
 * @param error_code HAL error code
 * @return true if recoverable, false if fatal
 */
bool hal_is_recoverable_error(int error_code);

/**
 * @brief Check if error is hardware-related
 * @param error_code HAL error code
 * @return true if hardware error, false otherwise
 */
static inline bool hal_is_hardware_error(int error_code) {
    return (error_code >= HAL_ERROR_HARDWARE_FAILURE && error_code <= HAL_ERROR_HARDWARE_MISMATCH);
}

/**
 * @brief Check if error is memory-related
 * @param error_code HAL error code
 * @return true if memory error, false otherwise
 */
static inline bool hal_is_memory_error(int error_code) {
    return (error_code >= HAL_ERROR_MEMORY && error_code <= HAL_ERROR_DMA_MEMORY);
}

/**
 * @brief Check if error is network-related
 * @param error_code HAL error code
 * @return true if network error, false otherwise
 */
static inline bool hal_is_network_error(int error_code) {
    return (error_code >= HAL_ERROR_LINK_DOWN && error_code <= HAL_ERROR_SEQUENCING_ERROR);
}

/**
 * @brief Map assembly error code to HAL error code
 * @param asm_error Assembly layer error code
 * @param nic_type NIC type for specific mapping
 * @return Corresponding HAL error code
 */
int hal_map_asm_error(int asm_error, int nic_type);

/* Error handling utility macros */
#define HAL_RETURN_ON_ERROR(expr) \
    do { \
        int _result = (expr); \
        if (hal_is_error(_result)) return _result; \
    } while(0)

#define HAL_LOG_ERROR(code, fmt, ...) \
    do { \
        LOG_ERROR("HAL Error %d (%s): " fmt, \
                  code, hal_error_to_string(code), ##__VA_ARGS__); \
    } while(0)

#define HAL_VALIDATE_POINTER(ptr) \
    do { \
        if (!(ptr)) return HAL_ERROR_INVALID_PARAM; \
    } while(0)

#define HAL_VALIDATE_RANGE(val, min, max) \
    do { \
        if ((val) < (min) || (val) > (max)) return HAL_ERROR_INVALID_PARAM; \
    } while(0)

#ifdef __cplusplus
}
#endif

#endif /* _HAL_ERRORS_H_ */
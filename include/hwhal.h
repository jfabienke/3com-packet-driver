/**
 * @file hardware_hal.h
 * @brief Hardware Abstraction Layer (HAL) vtable structure and interface
 *
 * Groups 6A & 6B - C Interface Architecture
 * Defines the complete HAL vtable structure with all 12 function pointers
 * for hardware abstraction between C layer and assembly implementations.
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef _HARDWARE_HAL_H_
#define _HARDWARE_HAL_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"
#include "nicctx.h"
#include "errhndl.h"
#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
struct nic_context;
struct eeprom_config;

/* Hardware error codes - matching assembly layer definitions */
#define HAL_SUCCESS                    0
#define HAL_ERROR_INVALID_PARAM       -1
#define HAL_ERROR_HARDWARE_FAILURE    -2
#define HAL_ERROR_TIMEOUT             -3
#define HAL_ERROR_NOT_SUPPORTED       -4
#define HAL_ERROR_RESOURCE_BUSY       -5
#define HAL_ERROR_INITIALIZATION      -6
#define HAL_ERROR_MEMORY              -7
#define HAL_ERROR_DMA                 -8
#define HAL_ERROR_INTERRUPT           -9
#define HAL_ERROR_LINK_DOWN          -10
#define HAL_ERROR_MEDIA_FAILURE      -11
#define HAL_ERROR_CHECKSUM           -12

/* Link status definitions */
#define HAL_LINK_DOWN       0
#define HAL_LINK_UP         1
#define HAL_LINK_UNKNOWN   -1

/* Media types */
#define HAL_MEDIA_AUTO      0
#define HAL_MEDIA_10_HALF   1
#define HAL_MEDIA_10_FULL   2
#define HAL_MEDIA_100_HALF  3
#define HAL_MEDIA_100_FULL  4

/* Receive modes */
#define HAL_RX_MODE_NORMAL      0x00
#define HAL_RX_MODE_PROMISCUOUS 0x01
#define HAL_RX_MODE_MULTICAST   0x02
#define HAL_RX_MODE_BROADCAST   0x04
#define HAL_RX_MODE_ALL_MULTI   0x08

/* Hardware statistics structure */
typedef struct hal_statistics {
    /* Packet counters */
    uint32_t tx_packets;        /* Packets transmitted */
    uint32_t rx_packets;        /* Packets received */
    uint32_t tx_bytes;          /* Bytes transmitted */
    uint32_t rx_bytes;          /* Bytes received */
    
    /* Error counters */
    uint32_t tx_errors;         /* Transmit errors */
    uint32_t rx_errors;         /* Receive errors */
    uint32_t tx_dropped;        /* Dropped TX packets */
    uint32_t rx_dropped;        /* Dropped RX packets */
    
    /* Collision and error details */
    uint32_t collisions;        /* Total collisions */
    uint32_t tx_carrier_errors; /* Carrier lost errors */
    uint32_t tx_aborted_errors; /* Aborted transmissions */
    uint32_t tx_window_errors;  /* Late collision errors */
    uint32_t tx_heartbeat_errors; /* Heartbeat errors */
    
    /* Receive error details */
    uint32_t rx_crc_errors;     /* CRC errors */
    uint32_t rx_frame_errors;   /* Frame alignment errors */
    uint32_t rx_fifo_errors;    /* FIFO overrun errors */
    uint32_t rx_missed_errors;  /* Missed packets */
    uint32_t rx_length_errors;  /* Length field errors */
    uint32_t rx_over_errors;    /* Oversized packets */
    
    /* Hardware-specific counters */
    uint32_t interrupts;        /* Interrupt count */
    uint32_t link_changes;      /* Link state changes */
    uint32_t dma_errors;        /* DMA-related errors */
    uint32_t hardware_resets;   /* Hardware reset count */
} hal_statistics_t;

/* Multicast address structure */
typedef struct hal_multicast {
    uint8_t count;              /* Number of addresses */
    uint8_t addresses[16][6];   /* Multicast addresses */
} hal_multicast_t;

/* NOTE: hardware_hal_vtable_t was removed 2026-01-25.
 * The C nic_ops_t vtable (get_3c509b_ops(), get_3c515_ops()) is the
 * production path. The ASM HAL layer was unused dead code.
 */
const char* hal_error_to_string(int error_code);
const char* hal_media_type_to_string(int media_type);

/* HAL utility functions for error handling */
#ifndef HAL_IS_SUCCESS_DEFINED
#define HAL_IS_SUCCESS_DEFINED
static bool hal_is_success(int result) {
    return (result == HAL_SUCCESS);
}
#endif

#ifndef HAL_IS_ERROR_DEFINED
#define HAL_IS_ERROR_DEFINED
static bool hal_is_error(int result) {
    return (result < 0);
}
#endif

#ifndef HAL_IS_TIMEOUT_ERROR_DEFINED
#define HAL_IS_TIMEOUT_ERROR_DEFINED
static bool hal_is_timeout_error(int result) {
    return (result == HAL_ERROR_TIMEOUT);
}
#endif

#ifndef HAL_IS_HARDWARE_ERROR_DEFINED
#define HAL_IS_HARDWARE_ERROR_DEFINED
static bool hal_is_hardware_error(int result) {
    return (result == HAL_ERROR_HARDWARE_FAILURE ||
            result == HAL_ERROR_DMA ||
            result == HAL_ERROR_MEDIA_FAILURE);
}
#endif

/* HAL validation macros */
#define HAL_VALIDATE_CONTEXT(ctx) \
    do { \
        if (!ctx) return HAL_ERROR_INVALID_PARAM; \
    } while(0)

#define HAL_VALIDATE_FUNCTION(vtable, func) \
    do { \
        if (!vtable->func) return HAL_ERROR_NOT_SUPPORTED; \
    } while(0)

#define HAL_CALL_WITH_ERROR_HANDLING(ctx, func, ...) \
    do { \
        HAL_VALIDATE_CONTEXT(ctx); \
        HAL_VALIDATE_FUNCTION(ctx->hal_vtable, func); \
        int _result = ctx->hal_vtable->func(ctx, ##__VA_ARGS__); \
        if (hal_is_error(_result)) { \
            handle_adapter_error(ctx, ERROR_TYPE_HARDWARE); \
        } \
        return _result; \
    } while(0)

#ifdef __cplusplus
}
#endif

#endif /* _HARDWARE_HAL_H_ */

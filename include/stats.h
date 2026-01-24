/**
 * @file stats.h
 * @brief Statistics gathering and reporting
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef _STATS_H_
#define _STATS_H_

/* Guard for nic_stats_t to prevent redefinition conflicts */
#define NIC_STATS_T_DEFINED 1

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include <stdint.h>
#include "config.h"

/* Constants */
#define MAX_NICS 8

/* Statistic types */
#define STAT_TYPE_TX_PACKETS    0
#define STAT_TYPE_TX_BYTES      1
#define STAT_TYPE_TX_ERRORS     2
#define STAT_TYPE_RX_PACKETS    3
#define STAT_TYPE_RX_BYTES      4
#define STAT_TYPE_RX_ERRORS     5
#define STAT_TYPE_COLLISIONS    6
#define STAT_TYPE_CRC_ERRORS    7

/* Error codes */
#define STATS_SUCCESS               0
#define STATS_ERR_INVALID_PARAM    -1
#define STATS_ERR_NOT_INITIALIZED  -2
#define STATS_ERR_INVALID_NIC      -3
#define STATS_ERR_INVALID_TYPE     -4

/* Global driver statistics */
typedef struct {
    uint32_t start_time;            /* Driver start time */
    uint32_t uptime;                /* Driver uptime in ticks */
    uint32_t tx_packets;            /* Total transmitted packets */
    uint32_t tx_bytes;              /* Total transmitted bytes */
    uint32_t tx_errors;             /* Total transmit errors */
    uint32_t rx_packets;            /* Total received packets */
    uint32_t rx_bytes;              /* Total received bytes */
    uint32_t rx_errors;             /* Total receive errors */
    uint32_t dropped_packets;       /* Total dropped packets */
    uint32_t interrupts_handled;    /* Total interrupts handled */
    uint32_t memory_allocated;      /* Total memory allocated */
} driver_stats_t;

/* NIC-specific statistics */
typedef struct {
    uint32_t tx_packets;            /* Transmitted packets */
    uint32_t tx_bytes;              /* Transmitted bytes */
    uint32_t tx_errors;             /* Transmit errors */
    uint32_t rx_packets;            /* Received packets */
    uint32_t rx_bytes;              /* Received bytes */
    uint32_t rx_errors;             /* Receive errors */
    uint32_t collisions;            /* Collision count */
    uint32_t crc_errors;            /* CRC errors */
    uint32_t frame_errors;          /* Frame errors */
    uint32_t overrun_errors;        /* Overrun errors */
    uint32_t last_activity;         /* Last activity timestamp */
} nic_stats_t;

/* Hardware register statistics (raw values read from NIC) */
typedef struct {
    int nic_id;                     /* NIC identifier */
    uint32_t timestamp;             /* Timestamp of register read */
    int register_count;             /* Number of registers read */
    uint32_t register_values[32];   /* Raw register values */
    uint32_t status_reg;            /* Status register value */
    uint32_t tx_status;             /* TX status register */
    uint32_t rx_status;             /* RX status register */
    uint32_t carrier_lost;          /* Carrier lost counter */
    uint32_t sqe_errors;            /* SQE test errors */
    uint32_t multiple_collisions;   /* Multiple collision counter */
    uint32_t single_collisions;     /* Single collision counter */
    uint32_t late_collisions;       /* Late collision counter */
    uint32_t rx_overruns;           /* Receive overrun counter */
    uint32_t frames_xmitted;        /* Frames transmitted counter */
    uint32_t frames_received;       /* Frames received counter */
    uint32_t bytes_received;        /* Bytes received counter */
    uint32_t frame_alignment;       /* Frame alignment error counter */
    uint32_t tx_bytes_ok;           /* TX bytes OK counter */
    uint32_t rx_bytes_ok;           /* RX bytes OK counter */
    uint8_t tx_active;              /* TX in progress flag */
    uint8_t rx_active;              /* RX in progress flag */
    uint8_t error_flags;            /* Error flag bits */
} hardware_register_stats_t;

/* Function prototypes */
uint32_t stats_get_timestamp(void);
int stats_subsystem_init(const config_t *config);
void stats_increment_tx_packets(void);
void stats_add_tx_bytes(uint32_t bytes);
void stats_increment_tx_errors(void);
void stats_increment_rx_packets(void);
void stats_add_rx_bytes(uint32_t bytes);
void stats_increment_rx_errors(void);
void stats_increment_dropped_packets(void);
int stats_update_nic(int nic_id, int stat_type, uint32_t value);
int stats_get_global(driver_stats_t *stats);
int stats_get_nic(int nic_id, nic_stats_t *stats);
int stats_reset_all(void);
int stats_reset_nic(int nic_id);
void stats_print_global(void);
void stats_print_nic(int nic_id);
void stats_print_all(void);
int stats_is_initialized(void);
void stats_increment_interrupts(void);
void stats_update_memory(int32_t bytes);
int stats_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* _STATS_H_ */

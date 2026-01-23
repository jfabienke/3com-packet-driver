/**
 * @file packet_api.h
 * @brief Packet Driver API structures and functions
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file defines the standard Packet Driver API as specified by
 * FTP Software, Inc. and implements the interface for DOS applications.
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef _PACKET_API_H_
#define _PACKET_API_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"

/* Packet Driver API version */
#define PACKET_DRIVER_VERSION   0x0100  /* Version 1.0 */
#define PACKET_DRIVER_CLASS     1       /* Ethernet class */
#define PACKET_DRIVER_TYPE      1       /* DIX Ethernet type */

/* Standard Packet Driver function codes */
#define PACKET_DRIVER_INFO      1       /* Get driver information */
#define PACKET_ACCESS_TYPE      2       /* Access packet type */
#define PACKET_RELEASE_TYPE     3       /* Release packet type */
#define PACKET_SEND_PKT         4       /* Send packet */
#define PACKET_TERMINATE        5       /* Terminate driver */
#define PACKET_GET_ADDRESS      6       /* Get station address */
#define PACKET_RESET_INTERFACE  7       /* Reset interface */
#define PACKET_GET_PARAMETERS   8       /* Get interface parameters */
#define PACKET_AS_SEND_PKT      9       /* Alternate send packet */
#define PACKET_SET_RCV_MODE     10      /* Set receive mode */
#define PACKET_GET_RCV_MODE     11      /* Get receive mode */
#define PACKET_SET_MULTICAST    12      /* Set multicast list */
#define PACKET_GET_MULTICAST    13      /* Get multicast list */
#define PACKET_GET_STATISTICS   14      /* Get interface statistics */
#define PACKET_SET_ADDRESS      15      /* Set station address */

/* Packet Driver error codes */
#define PACKET_NO_ERROR         0       /* No error */
#define PACKET_BAD_HANDLE       1       /* Invalid handle */
#define PACKET_NO_CLASS         2       /* No such class */
#define PACKET_NO_TYPE          3       /* No such type */
#define PACKET_NO_NUMBER        4       /* No such number */
#define PACKET_BAD_TYPE         5       /* Bad packet type */
#define PACKET_NO_MULTICAST     6       /* Multicast not supported */
#define PACKET_CANT_TERMINATE   7       /* Can't terminate */
#define PACKET_BAD_MODE         8       /* Bad mode */
#define PACKET_NO_SPACE         9       /* No space */
#define PACKET_TYPE_INUSE       10      /* Type already in use */
#define PACKET_BAD_COMMAND      11      /* Bad command */
#define PACKET_CANT_SEND        12      /* Can't send */
#define PACKET_CANT_SET         13      /* Can't set hardware address */
#define PACKET_BAD_ADDRESS      14      /* Bad hardware address */
#define PACKET_CANT_RESET       15      /* Can't reset interface */

/* Receive modes */\n#define RCV_MODE_OFF            0       /* Turn off receiver */
#define RCV_MODE_DIRECT         1       /* Receive only to this address */
#define RCV_MODE_BROADCAST      2       /* Receive direct + broadcast */
#define RCV_MODE_MULTICAST      3       /* Receive direct + broadcast + multicast */
#define RCV_MODE_PROMISCUOUS    4       /* Receive all packets */
#define RCV_MODE_ALL_MULTICAST  5       /* Receive all multicast */

/* Packet type constants */
#define PACKET_TYPE_IP          0x0800  /* Internet Protocol */
#define PACKET_TYPE_ARP         0x0806  /* Address Resolution Protocol */
#define PACKET_TYPE_RARP        0x8035  /* Reverse ARP */
#define PACKET_TYPE_IPX         0x8137  /* Novell IPX */
#define PACKET_TYPE_ALL         0xFFFF  /* All packet types */

/* Handle management */
#define MAX_HANDLES             16      /* Maximum number of handles */
#define INVALID_HANDLE          0xFFFF  /* Invalid handle value */

/* Driver information structure */
typedef struct {
    uint8_t version;                    /* Driver version */
    uint8_t class;                      /* Interface class */
    uint16_t type;                      /* Interface type */
    uint8_t number;                     /* Interface number */
    uint8_t basic;                      /* Basic functions supported */
    uint16_t extended;                  /* Extended functions supported */
    char name[15];                      /* Driver name (null-terminated) */
} PACKED driver_info_t;

/* Interface parameters structure */
typedef struct {
    uint8_t length;                     /* Structure length */
    uint8_t addr_len;                   /* Address length */
    uint8_t header_len;                 /* Header length */
    uint16_t recv_bufs;                 /* Receive buffer count */
    uint16_t recv_buf_len;              /* Receive buffer length */
    uint16_t send_bufs;                 /* Send buffer count */
    uint16_t send_buf_len;              /* Send buffer length */
} PACKED interface_params_t;

/* Statistics structure */
typedef struct {
    uint32_t packets_in;                /* Packets received */
    uint32_t packets_out;               /* Packets sent */
    uint32_t bytes_in;                  /* Bytes received */
    uint32_t bytes_out;                 /* Bytes sent */
    uint32_t errors_in;                 /* Receive errors */
    uint32_t errors_out;                /* Send errors */
    uint32_t packets_dropped;           /* Packets dropped */
} PACKED statistics_t;

/* Packet handle structure */
typedef struct packet_handle {
    uint16_t handle;                    /* Handle number */
    uint16_t packet_type;               /* Packet type */
    uint8_t recv_mode;                  /* Receive mode */
    void (*receiver)(void);             /* Receiver function */
    bool in_use;                        /* Handle in use flag */
    struct packet_handle* next;         /* Next handle in chain */
} packet_handle_t;

/* Multicast address structure */
typedef struct {
    uint8_t addr_len;                   /* Address length */
    uint8_t addr_count;                 /* Number of addresses */
    uint8_t addresses[16][ETH_ALEN];    /* Multicast addresses */
} PACKED multicast_list_t;

/* Global API state */
extern packet_handle_t g_packet_handles[MAX_HANDLES];
extern statistics_t g_packet_stats;
extern uint8_t g_current_recv_mode;
extern multicast_list_t g_multicast_list;

/* API function prototypes */

/* Core API functions */
int packet_driver_info(driver_info_t* info);
int packet_access_type(uint16_t if_class, uint16_t if_type, 
                      uint16_t if_number, uint16_t packet_type,
                      void (*receiver)(void), uint16_t* handle);
int packet_release_type(uint16_t handle);
int packet_send_pkt(const uint8_t* packet, uint16_t length);
int packet_terminate(void);
int packet_get_address(uint16_t if_number, uint8_t* address, uint16_t* length);
int packet_reset_interface(uint16_t if_number);
int packet_get_parameters(uint16_t if_number, interface_params_t* params);

/* Extended API functions */
int packet_as_send_pkt(uint16_t handle, const uint8_t* packet, uint16_t length);
int packet_set_rcv_mode(uint16_t if_number, uint8_t mode);
int packet_get_rcv_mode(uint16_t if_number, uint8_t* mode);
int packet_set_multicast_list(uint16_t if_number, const multicast_list_t* list);
int packet_get_multicast_list(uint16_t if_number, multicast_list_t* list);
int packet_get_statistics(uint16_t if_number, statistics_t* stats);
int packet_set_address(uint16_t if_number, const uint8_t* address, uint16_t length);

/* Handle management */
uint16_t allocate_handle(void);
void release_handle(uint16_t handle);
packet_handle_t* get_handle_info(uint16_t handle);
bool is_valid_handle(uint16_t handle);

/* Packet reception */
void packet_receive_handler(uint8_t* packet, uint16_t length, uint16_t packet_type);
void call_receivers(uint8_t* packet, uint16_t length, uint16_t packet_type);
bool should_receive_packet(uint16_t packet_type, const uint8_t* dest_addr);

/* Statistics management */
void update_statistics(bool is_receive, uint16_t length, bool error);
void reset_statistics(void);

/* Utility functions */
uint16_t extract_packet_type(const uint8_t* packet);
bool is_broadcast_address(const uint8_t* address);
bool is_multicast_address(const uint8_t* address);
bool is_our_address(const uint8_t* address);
bool is_in_multicast_list(const uint8_t* address);

/* DOS interrupt interface */
void packet_driver_interrupt(void);     /* Main interrupt handler */
void setup_packet_interrupt(uint8_t vector);
void remove_packet_interrupt(void);

/* Validation functions */
bool validate_interface_number(uint16_t if_number);
bool validate_packet_type(uint16_t packet_type);
bool validate_receive_mode(uint8_t mode);
bool validate_address_length(uint16_t length);

/* Error handling */
const char* packet_error_string(int error_code);
void set_packet_error(int error_code);
int get_last_packet_error(void);

#ifdef __cplusplus
}
#endif

#endif /* _PACKET_API_H_ */
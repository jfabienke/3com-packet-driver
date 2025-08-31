/**
 * @file network_logging.h
 * @brief Network Logging Interface
 * 
 * Provides UDP-based remote logging for debugging 3Com packet driver
 * from target DOS systems to development host.
 */

#ifndef NETWORK_LOGGING_H
#define NETWORK_LOGGING_H

#include <stdint.h>

/* Network logging result codes */
#define NETLOG_SUCCESS           0
#define NETLOG_ERROR_INVALID    -1
#define NETLOG_ERROR_NETWORK    -2
#define NETLOG_ERROR_BUFFER     -3
#define NETLOG_ERROR_CONFIG     -4

/* Network logging configuration */
typedef struct {
    uint32_t dest_ip;         /* Destination IP address */
    uint16_t dest_port;       /* Destination UDP port */
    uint16_t source_port;     /* Source UDP port */
    uint8_t enabled;          /* Network logging enabled */
    uint8_t hw_available;     /* Hardware available for networking */
} netlog_config_t;

/* Network log packet format */
#pragma pack(1)
typedef struct {
    uint32_t magic;           /* 0x3C515LOG */
    uint32_t timestamp;       /* Milliseconds since driver load */
    uint16_t sequence;        /* Packet sequence number */
    uint8_t level;           /* Log level */
    uint8_t category;        /* Log category bits */
    uint16_t length;         /* Message length */
    char message[232];       /* Log message (total packet = 256 bytes) */
} netlog_packet_t;
#pragma pack()

/**
 * @brief Initialize network logging system
 * 
 * @param dest_ip Destination IP address (network byte order)
 * @param dest_port Destination UDP port (host byte order)
 * @return NETLOG_SUCCESS or error code
 */
int netlog_init(uint32_t dest_ip, uint16_t dest_port);

/**
 * @brief Send log message via UDP
 * 
 * @param level Log level
 * @param category Category mask
 * @param message Message text
 * @return NETLOG_SUCCESS or error code
 */
int netlog_send_message(uint8_t level, uint8_t category, const char *message);

/**
 * @brief Configure network logging from string
 * 
 * Format: "192.168.1.100:1234" or "off"
 * 
 * @param config_str Configuration string
 * @return NETLOG_SUCCESS or error code
 */
int netlog_configure(const char *config_str);

/**
 * @brief Check if network logging is available
 * 
 * @return 1 if available, 0 if not
 */
int netlog_is_available(void);

/**
 * @brief Get network logging statistics
 * 
 * @param packets_sent Number of packets sent
 * @param send_errors Number of send errors
 * @param sequence Current sequence number
 */
void netlog_get_stats(uint16_t *packets_sent, uint16_t *send_errors, uint16_t *sequence);

/**
 * @brief Cleanup network logging
 */
void netlog_cleanup(void);

/* Default configuration */
#define NETLOG_DEFAULT_PORT      1234
#define NETLOG_MAGIC            0x3C515C0DL  /* "3C515LOG" */

#endif /* NETWORK_LOGGING_H */
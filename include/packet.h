/**
 * @file packet.h
 * @brief Minimal packet structure for 3Com driver compatibility
 *
 * Provides a simple packet structure that wraps existing packet_buffer_t
 * functionality for compatibility with the 3Com multi-generation driver.
 *
 * 3Com Packet Driver - Packet Management Interface
 *
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef _PACKET_H_
#define _PACKET_H_

#include "packet_ops.h"
#include "common.h"

/* Simple packet structure compatible with existing buffer management */
typedef struct packet {
    uint8_t *data;          /* Packet data pointer (from packet_buffer_t) */
    uint16_t length;        /* Actual packet length */
    uint16_t capacity;      /* Buffer capacity */
    uint8_t nic_index;      /* Source/destination NIC index */
    uint8_t priority;       /* Packet priority */
    uint16_t flags;         /* Packet flags */
    struct packet *next;    /* Next packet in queue */
} packet_t;

/* Minimum and maximum packet sizes */
#define MIN_PACKET_SIZE     60      /* Minimum Ethernet frame size */
#define MAX_PACKET_SIZE     1514    /* Maximum Ethernet frame size */

/* Wrapper functions using existing buffer management */
static inline packet_t* packet_alloc(uint16_t size)
{
    /* Ensure minimum packet size */
    if (size < MIN_PACKET_SIZE) {
        size = MIN_PACKET_SIZE;
    }
    
    /* Use existing packet buffer allocation */
    packet_buffer_t *buf = packet_buffer_allocate(size);
    if (!buf) {
        return NULL;
    }
    
    /* Cast to packet_t - structures are compatible */
    packet_t *pkt = (packet_t *)buf;
    pkt->nic_index = 0;  /* Default to first NIC */
    
    return pkt;
}

static inline void packet_free(packet_t *pkt)
{
    if (pkt) {
        /* Use existing packet buffer free */
        packet_buffer_free((packet_buffer_t *)pkt);
    }
}

/* Queue management using existing functions */
static inline int packet_enqueue(packet_queue_t *queue, packet_t *pkt)
{
    return packet_queue_enqueue(queue, (packet_buffer_t *)pkt);
}

static inline packet_t* packet_dequeue(packet_queue_t *queue)
{
    return (packet_t *)packet_queue_dequeue(queue);
}

#endif /* _PACKET_H_ */
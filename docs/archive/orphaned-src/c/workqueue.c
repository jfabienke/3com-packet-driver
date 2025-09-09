/**
 * @file workqueue.c
 * @brief Single Producer Single Consumer (SPSC) work queue for ISR deferral
 * 
 * Provides lock-free communication between interrupt handlers and bottom-half
 * workers. Designed for maximum performance with minimal memory footprint.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "workqueue.h"
#include "common.h"

/* Work queue implementation using lock-free SPSC circular buffer */

/**
 * Work item types
 */
typedef enum {
    WORK_RX_PACKET = 1,     /* RX packet available */
    WORK_TX_COMPLETE = 2,   /* TX completion */
    WORK_ERROR = 3,         /* Error condition */
    WORK_STATS = 4          /* Statistics update */
} work_type_t;

/**
 * Work item structure (16 bytes for cache alignment)
 */
typedef struct {
    uint8_t type;           /* work_type_t */
    uint8_t device_id;      /* Device that generated work */
    uint16_t data1;         /* Type-specific data */
    uint32_t data2;         /* Type-specific data */
    void *ptr;              /* Type-specific pointer */
    uint32_t timestamp;     /* Work generation time (optional) */
} work_item_t;

/**
 * SPSC work queue structure
 */
struct work_queue {
    volatile uint16_t head;     /* Producer index (ISR writes) */
    volatile uint16_t tail;     /* Consumer index (worker reads) */
    uint16_t mask;              /* Size mask (size must be power of 2) */
    uint16_t size;              /* Queue size */
    work_item_t *items;         /* Ring buffer */
    
    /* Statistics */
    uint32_t enqueued;          /* Total items enqueued */
    uint32_t dequeued;          /* Total items dequeued */
    uint32_t overruns;          /* Queue full events */
    uint32_t spurious;          /* Empty queue polls */
};

/* Global work queues (one per device) */
#define MAX_DEVICES 4
#define WORK_QUEUE_SIZE 32      /* Must be power of 2 */

static work_item_t work_items[MAX_DEVICES][WORK_QUEUE_SIZE];
static struct work_queue work_queues[MAX_DEVICES];
static uint8_t num_queues = 0;

/* Per-device work pending flags (for ISR) */
volatile uint8_t work_pending[MAX_DEVICES];

/**
 * Initialize work queue system
 */
int workqueue_init(void)
{
    memset(work_queues, 0, sizeof(work_queues));
    memset(work_items, 0, sizeof(work_items));
    memset((void*)work_pending, 0, sizeof(work_pending));
    
    num_queues = 0;
    return 0;
}

/**
 * Create work queue for a device
 */
int workqueue_create(uint8_t device_id)
{
    if (device_id >= MAX_DEVICES || num_queues >= MAX_DEVICES) {
        return -1;
    }
    
    struct work_queue *wq = &work_queues[device_id];
    
    wq->head = 0;
    wq->tail = 0;
    wq->size = WORK_QUEUE_SIZE;
    wq->mask = WORK_QUEUE_SIZE - 1;
    wq->items = work_items[device_id];
    
    /* Clear statistics */
    wq->enqueued = 0;
    wq->dequeued = 0;
    wq->overruns = 0;
    wq->spurious = 0;
    
    work_pending[device_id] = 0;
    
    if (device_id >= num_queues) {
        num_queues = device_id + 1;
    }
    
    return 0;
}

/**
 * Enqueue work item (called from ISR)
 * CRITICAL: This must be fast and lock-free
 */
int workqueue_enqueue_rx(uint8_t device_id, uint16_t length, void *buffer)
{
    if (device_id >= num_queues) {
        return -1;
    }
    
    struct work_queue *wq = &work_queues[device_id];
    uint16_t head = wq->head;
    uint16_t next_head = (head + 1) & wq->mask;
    
    /* Check if queue is full */
    if (next_head == wq->tail) {
        wq->overruns++;
        return -1;  /* Queue full */
    }
    
    /* Add work item */
    work_item_t *item = &wq->items[head];
    item->type = WORK_RX_PACKET;
    item->device_id = device_id;
    item->data1 = length;
    item->data2 = 0;
    item->ptr = buffer;
    
    /* Memory barrier: ensure item is written before updating head */
    __asm__ volatile("" ::: "memory");
    
    /* Update head pointer (makes item visible to consumer) */
    wq->head = next_head;
    wq->enqueued++;
    
    /* Set work pending flag for polling */
    work_pending[device_id] = 1;
    
    return 0;
}

/**
 * Enqueue TX completion work
 */
int workqueue_enqueue_tx_complete(uint8_t device_id, uint16_t descriptor_id)
{
    if (device_id >= num_queues) {
        return -1;
    }
    
    struct work_queue *wq = &work_queues[device_id];
    uint16_t head = wq->head;
    uint16_t next_head = (head + 1) & wq->mask;
    
    if (next_head == wq->tail) {
        wq->overruns++;
        return -1;
    }
    
    work_item_t *item = &wq->items[head];
    item->type = WORK_TX_COMPLETE;
    item->device_id = device_id;
    item->data1 = descriptor_id;
    item->data2 = 0;
    item->ptr = NULL;
    
    __asm__ volatile("" ::: "memory");
    wq->head = next_head;
    wq->enqueued++;
    work_pending[device_id] = 1;
    
    return 0;
}

/**
 * Enqueue error work
 */
int workqueue_enqueue_error(uint8_t device_id, uint16_t error_code, uint32_t error_data)
{
    if (device_id >= num_queues) {
        return -1;
    }
    
    struct work_queue *wq = &work_queues[device_id];
    uint16_t head = wq->head;
    uint16_t next_head = (head + 1) & wq->mask;
    
    if (next_head == wq->tail) {
        wq->overruns++;
        return -1;
    }
    
    work_item_t *item = &wq->items[head];
    item->type = WORK_ERROR;
    item->device_id = device_id;
    item->data1 = error_code;
    item->data2 = error_data;
    item->ptr = NULL;
    
    __asm__ volatile("" ::: "memory");
    wq->head = next_head;
    wq->enqueued++;
    work_pending[device_id] = 1;
    
    return 0;
}

/**
 * Dequeue work item (called from worker)
 * Returns 0 if no work available, 1 if work item returned
 */
int workqueue_dequeue(uint8_t device_id, work_item_t *item)
{
    if (device_id >= num_queues || !item) {
        return 0;
    }
    
    struct work_queue *wq = &work_queues[device_id];
    uint16_t tail = wq->tail;
    
    /* Check if queue is empty */
    if (tail == wq->head) {
        work_pending[device_id] = 0;  /* Clear pending flag */
        wq->spurious++;
        return 0;  /* No work */
    }
    
    /* Copy work item */
    *item = wq->items[tail];
    
    /* Memory barrier: ensure item is read before updating tail */
    __asm__ volatile("" ::: "memory");
    
    /* Update tail pointer */
    wq->tail = (tail + 1) & wq->mask;
    wq->dequeued++;
    
    return 1;  /* Work item returned */
}

/**
 * Check if any work is pending (fast poll)
 */
bool workqueue_has_work(uint8_t device_id)
{
    if (device_id >= num_queues) {
        return false;
    }
    
    return work_pending[device_id] != 0;
}

/**
 * Check if any device has work pending
 */
bool workqueue_has_any_work(void)
{
    for (uint8_t i = 0; i < num_queues; i++) {
        if (work_pending[i]) {
            return true;
        }
    }
    return false;
}

/**
 * Get work queue statistics
 */
void workqueue_get_stats(uint8_t device_id, struct workqueue_stats *stats)
{
    if (device_id >= num_queues || !stats) {
        return;
    }
    
    struct work_queue *wq = &work_queues[device_id];
    
    stats->enqueued = wq->enqueued;
    stats->dequeued = wq->dequeued;
    stats->overruns = wq->overruns;
    stats->spurious = wq->spurious;
    stats->pending = (wq->head - wq->tail) & wq->mask;
    stats->queue_size = wq->size;
}

/**
 * Reset work queue statistics
 */
void workqueue_reset_stats(uint8_t device_id)
{
    if (device_id >= num_queues) {
        return;
    }
    
    struct work_queue *wq = &work_queues[device_id];
    
    wq->enqueued = 0;
    wq->dequeued = 0;
    wq->overruns = 0;
    wq->spurious = 0;
}

/**
 * Get address of work pending flag for ISR
 * This allows the ISR to directly set the flag via SMC
 */
volatile uint8_t *workqueue_get_pending_flag(uint8_t device_id)
{
    if (device_id >= MAX_DEVICES) {
        return NULL;
    }
    
    return &work_pending[device_id];
}

/**
 * Advanced work queue operations
 */

/**
 * Drain all work from a queue (for shutdown)
 */
int workqueue_drain(uint8_t device_id)
{
    work_item_t item;
    int drained = 0;
    
    while (workqueue_dequeue(device_id, &item)) {
        drained++;
        /* Just discard items */
    }
    
    return drained;
}

/**
 * Get queue utilization percentage (0-100)
 */
uint8_t workqueue_utilization(uint8_t device_id)
{
    if (device_id >= num_queues) {
        return 0;
    }
    
    struct work_queue *wq = &work_queues[device_id];
    uint16_t used = (wq->head - wq->tail) & wq->mask;
    
    return (used * 100) / wq->size;
}

/**
 * Check queue health
 * Returns 0 if healthy, negative if problems detected
 */
int workqueue_health_check(uint8_t device_id)
{
    if (device_id >= num_queues) {
        return -1;
    }
    
    struct work_queue *wq = &work_queues[device_id];
    
    /* Check for excessive overruns */
    if (wq->overruns > wq->enqueued / 4) {  /* >25% overruns */
        return -2;  /* Queue too small */
    }
    
    /* Check for queue stagnation */
    uint16_t pending = (wq->head - wq->tail) & wq->mask;
    if (pending == wq->size - 1) {  /* Nearly full */
        return -3;  /* Consumer too slow */
    }
    
    return 0;  /* Healthy */
}
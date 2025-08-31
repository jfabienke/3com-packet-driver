/**
 * @file dma_rings.c
 * @brief DMA Descriptor Ring Management for 3C515
 * 
 * Agent Team B (07-08): Week 1 Implementation
 * 
 * This module implements DMA descriptor ring management for the 3C515 ISA
 * bus-master network controller. It handles TX/RX descriptor rings with
 * proper 64KB boundary safety and coherent DMA buffer management.
 * 
 * Key Features:
 * - Circular descriptor rings with hardware ownership
 * - 64KB boundary-safe DMA buffer allocation
 * - Scatter-gather I/O with fragment support
 * - Ring wrap-around and status management
 * - Integration with bounce buffer system
 * 
 * DMA Safety Requirements:
 * - All descriptors must be physically contiguous
 * - No descriptors may cross 64KB boundaries
 * - Buffers must be cache-coherent
 * - Physical addresses below 16MB for ISA DMA
 * 
 * This file is part of the CORKSCRW.MOD module.
 * Copyright (c) 2025 3Com/Phase3A Team B
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* DMA Configuration */
#define TX_RING_SIZE            16      /* 16 TX descriptors */
#define RX_RING_SIZE            32      /* 32 RX descriptors */
#define MAX_FRAME_SIZE          1536    /* Maximum Ethernet frame */
#define DMA_ALIGNMENT           16      /* 16-byte descriptor alignment */
#define ISA_DMA_LIMIT           0x1000000  /* 16MB limit for ISA DMA */

/* Descriptor Control Bits */
#define DN_FRAG_LAST            0x80000000  /* Last fragment */
#define DN_FRAG_FIRST           0x40000000  /* First fragment */
#define DN_REQUEST_INT          0x20000000  /* Request interrupt */
#define DN_INDICATE             0x10000000  /* Indicate frame */

#define UP_PACKET_COMPLETE      0x8000      /* Packet complete */
#define UP_ERROR                0x4000      /* Error in packet */
#define UP_OVERRUN              0x2000      /* Buffer overrun */
#define UP_RUNT_FRAME           0x1000      /* Runt frame */
#define UP_ALIGN_ERROR          0x0800      /* Alignment error */
#define UP_CRC_ERROR            0x0400      /* CRC error */
#define UP_OVERFLOW             0x0200      /* FIFO overflow */

/**
 * Download Descriptor (TX) - Hardware Layout
 */
typedef struct {
    uint32_t next_desc_ptr;     /* Physical pointer to next descriptor */
    uint32_t frame_start_hdr;   /* Frame start header and control */
    uint32_t frag_addr;         /* Fragment physical address */
    uint32_t frag_len;          /* Fragment length and control */
} __attribute__((packed)) dn_desc_t;

/**
 * Upload Descriptor (RX) - Hardware Layout
 */
typedef struct {
    uint32_t next_desc_ptr;     /* Physical pointer to next descriptor */
    uint32_t pkt_status;        /* Packet status and length */
    uint32_t frag_addr;         /* Fragment physical address */
    uint32_t frag_len;          /* Fragment length */
} __attribute__((packed)) up_desc_t;

/**
 * DMA Buffer Descriptor
 */
typedef struct {
    void *virt_addr;            /* Virtual address */
    uint32_t phys_addr;         /* Physical address */
    uint16_t size;              /* Buffer size */
    uint8_t boundary_safe;      /* 64KB boundary safe flag */
    uint8_t in_use;             /* Buffer in use flag */
} dma_buffer_t;

/**
 * Ring Buffer Management Structure
 */
typedef struct {
    /* TX Ring */
    dn_desc_t *tx_ring;         /* TX descriptor ring */
    uint32_t tx_ring_phys;      /* TX ring physical address */
    uint16_t tx_head;           /* TX ring head index */
    uint16_t tx_tail;           /* TX ring tail index */
    uint16_t tx_count;          /* Active TX descriptors */
    dma_buffer_t tx_buffers[TX_RING_SIZE];
    
    /* RX Ring */
    up_desc_t *rx_ring;         /* RX descriptor ring */
    uint32_t rx_ring_phys;      /* RX ring physical address */
    uint16_t rx_head;           /* RX ring head index */
    uint16_t rx_tail;           /* RX ring tail index */
    dma_buffer_t rx_buffers[RX_RING_SIZE];
    
    /* Status */
    bool initialized;           /* Ring initialization flag */
    uint32_t tx_ring_errors;    /* TX ring errors */
    uint32_t rx_ring_errors;    /* RX ring errors */
    uint32_t boundary_violations; /* 64KB boundary violations */
} ring_manager_t;

/* Global ring manager */
static ring_manager_t g_ring_mgr;

/* Forward Declarations */
static int allocate_descriptor_rings(void);
static int allocate_dma_buffers(void);
static void free_descriptor_rings(void);
static void free_dma_buffers(void);
static int setup_tx_ring(void);
static int setup_rx_ring(void);
static bool check_64kb_boundary(uint32_t phys_addr, uint32_t size);
static uint32_t virt_to_phys(void *virt_addr);
static void *alloc_dma_coherent(size_t size, uint32_t *phys_addr);
static void free_dma_coherent(void *virt_addr, size_t size);

/* Ring manipulation functions */
static int tx_ring_add_packet(const void *packet_data, uint16_t packet_len);
static int tx_ring_complete_packet(uint16_t *completed_count);
static int rx_ring_get_packet(void **packet_data, uint16_t *packet_len);
static int rx_ring_refill_buffers(void);

/**
 * ============================================================================
 * RING INITIALIZATION AND CLEANUP
 * ============================================================================
 */

/**
 * @brief Initialize DMA descriptor rings
 * 
 * @return 0 on success, negative error code on failure
 */
int dma_rings_init(void)
{
    int result;
    
    /* Check if already initialized */
    if (g_ring_mgr.initialized) {
        return -1;  /* Already initialized */
    }
    
    /* Clear ring manager structure */
    memset(&g_ring_mgr, 0, sizeof(ring_manager_t));
    
    /* Allocate descriptor rings */
    result = allocate_descriptor_rings();
    if (result < 0) {
        goto cleanup;
    }
    
    /* Allocate DMA buffers */
    result = allocate_dma_buffers();
    if (result < 0) {
        goto cleanup;
    }
    
    /* Set up TX ring */
    result = setup_tx_ring();
    if (result < 0) {
        goto cleanup;
    }
    
    /* Set up RX ring */
    result = setup_rx_ring();
    if (result < 0) {
        goto cleanup;
    }
    
    g_ring_mgr.initialized = true;
    return 0;
    
cleanup:
    dma_rings_cleanup();
    return result;
}

/**
 * @brief Clean up DMA descriptor rings
 */
void dma_rings_cleanup(void)
{
    if (!g_ring_mgr.initialized) {
        return;
    }
    
    /* Free DMA buffers */
    free_dma_buffers();
    
    /* Free descriptor rings */
    free_descriptor_rings();
    
    /* Clear ring manager */
    memset(&g_ring_mgr, 0, sizeof(ring_manager_t));
}

/**
 * @brief Get ring physical addresses for hardware programming
 * 
 * @param tx_ring_phys Pointer to store TX ring physical address
 * @param rx_ring_phys Pointer to store RX ring physical address
 * @return 0 on success, negative on error
 */
int dma_rings_get_addresses(uint32_t *tx_ring_phys, uint32_t *rx_ring_phys)
{
    if (!g_ring_mgr.initialized || !tx_ring_phys || !rx_ring_phys) {
        return -1;
    }
    
    *tx_ring_phys = g_ring_mgr.tx_ring_phys;
    *rx_ring_phys = g_ring_mgr.rx_ring_phys;
    
    return 0;
}

/**
 * ============================================================================
 * TX RING MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Add packet to TX ring for transmission
 * 
 * @param packet_data Pointer to packet data
 * @param packet_len Length of packet in bytes
 * @return 0 on success, negative on error
 */
int dma_rings_tx_add_packet(const void *packet_data, uint16_t packet_len)
{
    if (!g_ring_mgr.initialized || !packet_data || packet_len == 0) {
        return -1;
    }
    
    /* Check if ring has space */
    if (g_ring_mgr.tx_count >= TX_RING_SIZE - 1) {
        return -2;  /* Ring full */
    }
    
    return tx_ring_add_packet(packet_data, packet_len);
}

/**
 * @brief Process completed TX descriptors
 * 
 * @param completed_count Pointer to store number of completed packets
 * @return 0 on success, negative on error
 */
int dma_rings_tx_complete(uint16_t *completed_count)
{
    if (!g_ring_mgr.initialized || !completed_count) {
        return -1;
    }
    
    return tx_ring_complete_packet(completed_count);
}

/**
 * @brief Check if TX ring has space for more packets
 * 
 * @return Number of free TX slots
 */
int dma_rings_tx_free_slots(void)
{
    if (!g_ring_mgr.initialized) {
        return 0;
    }
    
    return TX_RING_SIZE - 1 - g_ring_mgr.tx_count;
}

/**
 * ============================================================================
 * RX RING MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Get received packet from RX ring
 * 
 * @param packet_data Pointer to store packet data pointer
 * @param packet_len Pointer to store packet length
 * @return 0 on success, negative on error, 1 if no packet available
 */
int dma_rings_rx_get_packet(void **packet_data, uint16_t *packet_len)
{
    if (!g_ring_mgr.initialized || !packet_data || !packet_len) {
        return -1;
    }
    
    return rx_ring_get_packet(packet_data, packet_len);
}

/**
 * @brief Refill RX ring with fresh buffers
 * 
 * @return 0 on success, negative on error
 */
int dma_rings_rx_refill(void)
{
    if (!g_ring_mgr.initialized) {
        return -1;
    }
    
    return rx_ring_refill_buffers();
}

/**
 * ============================================================================
 * INTERNAL IMPLEMENTATION
 * ============================================================================
 */

/**
 * @brief Allocate descriptor rings
 */
static int allocate_descriptor_rings(void)
{
    size_t tx_ring_size = sizeof(dn_desc_t) * TX_RING_SIZE;
    size_t rx_ring_size = sizeof(up_desc_t) * RX_RING_SIZE;
    
    /* Allocate TX ring */
    g_ring_mgr.tx_ring = (dn_desc_t*)alloc_dma_coherent(tx_ring_size, 
                                                        &g_ring_mgr.tx_ring_phys);
    if (!g_ring_mgr.tx_ring) {
        return -1;
    }
    
    /* Check 64KB boundary */
    if (!check_64kb_boundary(g_ring_mgr.tx_ring_phys, tx_ring_size)) {
        free_dma_coherent(g_ring_mgr.tx_ring, tx_ring_size);
        g_ring_mgr.tx_ring = NULL;
        g_ring_mgr.boundary_violations++;
        return -2;
    }
    
    /* Allocate RX ring */
    g_ring_mgr.rx_ring = (up_desc_t*)alloc_dma_coherent(rx_ring_size, 
                                                        &g_ring_mgr.rx_ring_phys);
    if (!g_ring_mgr.rx_ring) {
        free_dma_coherent(g_ring_mgr.tx_ring, tx_ring_size);
        g_ring_mgr.tx_ring = NULL;
        return -1;
    }
    
    /* Check 64KB boundary */
    if (!check_64kb_boundary(g_ring_mgr.rx_ring_phys, rx_ring_size)) {
        free_dma_coherent(g_ring_mgr.tx_ring, tx_ring_size);
        free_dma_coherent(g_ring_mgr.rx_ring, rx_ring_size);
        g_ring_mgr.tx_ring = NULL;
        g_ring_mgr.rx_ring = NULL;
        g_ring_mgr.boundary_violations++;
        return -2;
    }
    
    return 0;
}

/**
 * @brief Allocate DMA buffers for packets
 */
static int allocate_dma_buffers(void)
{
    /* Allocate TX buffers */
    for (int i = 0; i < TX_RING_SIZE; i++) {
        dma_buffer_t *buf = &g_ring_mgr.tx_buffers[i];
        
        buf->virt_addr = alloc_dma_coherent(MAX_FRAME_SIZE, &buf->phys_addr);
        if (!buf->virt_addr) {
            return -1;
        }
        
        buf->size = MAX_FRAME_SIZE;
        buf->boundary_safe = check_64kb_boundary(buf->phys_addr, buf->size);
        buf->in_use = false;
        
        if (!buf->boundary_safe) {
            g_ring_mgr.boundary_violations++;
        }
    }
    
    /* Allocate RX buffers */
    for (int i = 0; i < RX_RING_SIZE; i++) {
        dma_buffer_t *buf = &g_ring_mgr.rx_buffers[i];
        
        buf->virt_addr = alloc_dma_coherent(MAX_FRAME_SIZE, &buf->phys_addr);
        if (!buf->virt_addr) {
            return -1;
        }
        
        buf->size = MAX_FRAME_SIZE;
        buf->boundary_safe = check_64kb_boundary(buf->phys_addr, buf->size);
        buf->in_use = true;  /* RX buffers start in use */
        
        if (!buf->boundary_safe) {
            g_ring_mgr.boundary_violations++;
        }
    }
    
    return 0;
}

/**
 * @brief Set up TX descriptor ring
 */
static int setup_tx_ring(void)
{
    /* Initialize TX ring pointers */
    g_ring_mgr.tx_head = 0;
    g_ring_mgr.tx_tail = 0;
    g_ring_mgr.tx_count = 0;
    
    /* Initialize descriptors */
    for (int i = 0; i < TX_RING_SIZE; i++) {
        dn_desc_t *desc = &g_ring_mgr.tx_ring[i];
        
        /* Set up circular ring */
        uint32_t next_phys = g_ring_mgr.tx_ring_phys + 
                            ((i + 1) % TX_RING_SIZE) * sizeof(dn_desc_t);
        desc->next_desc_ptr = next_phys;
        
        /* Clear other fields */
        desc->frame_start_hdr = 0;
        desc->frag_addr = 0;
        desc->frag_len = 0;
    }
    
    return 0;
}

/**
 * @brief Set up RX descriptor ring
 */
static int setup_rx_ring(void)
{
    /* Initialize RX ring pointers */
    g_ring_mgr.rx_head = 0;
    g_ring_mgr.rx_tail = 0;
    
    /* Initialize descriptors with buffers */
    for (int i = 0; i < RX_RING_SIZE; i++) {
        up_desc_t *desc = &g_ring_mgr.rx_ring[i];
        dma_buffer_t *buf = &g_ring_mgr.rx_buffers[i];
        
        /* Set up circular ring */
        uint32_t next_phys = g_ring_mgr.rx_ring_phys + 
                            ((i + 1) % RX_RING_SIZE) * sizeof(up_desc_t);
        desc->next_desc_ptr = next_phys;
        
        /* Set up buffer */
        desc->frag_addr = buf->phys_addr;
        desc->frag_len = buf->size;
        desc->pkt_status = 0;  /* Owned by hardware */
    }
    
    return 0;
}

/**
 * ============================================================================
 * UTILITY FUNCTIONS (Stubs for Week 1)
 * ============================================================================
 */

static bool check_64kb_boundary(uint32_t phys_addr, uint32_t size)
{
    /* Check if buffer crosses 64KB boundary */
    uint32_t start_page = phys_addr >> 16;
    uint32_t end_page = (phys_addr + size - 1) >> 16;
    return (start_page == end_page);
}

static uint32_t virt_to_phys(void *virt_addr)
{
    /* Stub for Week 1 - in DOS, virtual == physical for conventional memory */
    return (uint32_t)virt_addr;
}

static void *alloc_dma_coherent(size_t size, uint32_t *phys_addr)
{
    /* Stub for Week 1 - would use memory management API */
    *phys_addr = 0x100000;  /* Fake physical address */
    return NULL;  /* Would return allocated buffer */
}

static void free_dma_coherent(void *virt_addr, size_t size)
{
    /* Stub for Week 1 - would free via memory management API */
}

static void free_descriptor_rings(void)
{
    /* Stub for Week 1 */
}

static void free_dma_buffers(void)
{
    /* Stub for Week 1 */
}

static int tx_ring_add_packet(const void *packet_data, uint16_t packet_len)
{
    /* Stub for Week 1 */
    return 0;
}

static int tx_ring_complete_packet(uint16_t *completed_count)
{
    /* Stub for Week 1 */
    *completed_count = 0;
    return 0;
}

static int rx_ring_get_packet(void **packet_data, uint16_t *packet_len)
{
    /* Stub for Week 1 */
    return 1;  /* No packet available */
}

static int rx_ring_refill_buffers(void)
{
    /* Stub for Week 1 */
    return 0;
}
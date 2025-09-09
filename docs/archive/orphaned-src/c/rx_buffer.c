/**
 * @file rx_buffer.c
 * @brief RX buffer management with proper physical/virtual addressing
 * 
 * Implements buffer pools with both physical addresses (for NIC DMA) and
 * far pointers (for CPU access) in DOS real mode.
 */

#include <stdint.h>
#include <string.h>
#include <dos.h>
#include "rx_buffer.h"
#include "logging.h"

/* Per-NIC buffer pools */
static rx_buffer_pool_t rx_pools[4] = {0};

/**
 * @brief Check if buffer crosses 64KB boundary
 * 
 * ISA DMA controllers cannot cross 64KB boundaries in a single transfer.
 * @param phys_addr Physical start address
 * @param size Buffer size
 * @return 1 if crosses boundary, 0 if safe
 */
static int crosses_64k_boundary(uint32_t phys_addr, uint16_t size) {
    uint32_t end_addr = phys_addr + size - 1;
    /* Check if start and end are in different 64KB pages */
    return (phys_addr & 0xFFFF0000) != (end_addr & 0xFFFF0000);
}

/**
 * @brief Allocate conventional memory block
 * 
 * Uses DOS INT 21h AH=48h to allocate paragraphs.
 * Returns both segment and physical address.
 * 
 * NOTE: Physical address calculation assumes real mode without EMM386/QEMM.
 * Systems using V86 mode memory managers require XMS/DPMI for DMA-safe buffers.
 */
static int alloc_conventional_block(uint16_t size, uint16_t *seg, uint32_t *phys) {
    uint16_t paragraphs;
    uint16_t alloc_seg = 0;
    int result = 0;
    int retry_count = 0;
    const int MAX_RETRIES = 3;
    
    /* Calculate paragraphs needed (round up) */
    paragraphs = (size + 15) >> 4;
    
retry_alloc:
    __asm {
        mov     ah, 48h         ; Allocate memory
        mov     bx, paragraphs
        int     21h
        jnc     alloc_success   ; Jump if no carry (success)
        mov     result, -1      ; Set error result
        jmp     alloc_done
    alloc_success:
        mov     alloc_seg, ax   ; Save allocated segment
    alloc_done:
    }
    
    if (result != 0 || alloc_seg == 0) {
        LOG_ERROR("DOS memory allocation failed for %u paragraphs", paragraphs);
        return -1;
    }
    
    *seg = alloc_seg;
    *phys = ((uint32_t)alloc_seg) << 4;  /* Physical = segment * 16 (real mode only) */
    
    /* Check for 64KB boundary crossing */
    if (crosses_64k_boundary(*phys, size)) {
        LOG_WARNING("Buffer crosses 64KB boundary at %lX, retrying", *phys);
        
        /* Free this block */
        __asm {
            mov     es, alloc_seg
            mov     ah, 49h     ; Free memory
            int     21h
        }
        
        if (++retry_count < MAX_RETRIES) {
            /* Try allocating slightly larger to get different alignment */
            paragraphs += 1;
            goto retry_alloc;
        } else {
            LOG_ERROR("Cannot allocate DMA-safe buffer after %d tries", MAX_RETRIES);
            return -1;
        }
    }
    
    return 0;
}

/**
 * @brief Initialize RX buffer pool for a NIC
 */
int rx_buffer_init(uint8_t nic_index) {
    rx_buffer_pool_t *pool;
    uint16_t large_seg, small_seg;
    uint32_t large_phys, small_phys;
    int i;
    
    if (nic_index >= 4) {
        return -1;
    }
    
    pool = &rx_pools[nic_index];
    memset(pool, 0, sizeof(rx_buffer_pool_t));
    
    /* Allocate large buffer block (32 * 1536 = 48KB) */
    if (alloc_conventional_block(RX_BUF_COUNT * RX_BUF_SIZE,
                                 &large_seg, &large_phys) != 0) {
        LOG_ERROR("Failed to allocate large RX buffers");
        return -1;
    }
    
    pool->large_mem_base = MK_FP(large_seg, 0);
    
    /* Allocate small buffer block (16 * 256 = 4KB) */
    if (alloc_conventional_block(RX_SMALL_BUF_COUNT * RX_SMALL_BUF_SIZE,
                                 &small_seg, &small_phys) != 0) {
        LOG_ERROR("Failed to allocate small RX buffers");
        /* TODO: Free large block */
        return -1;
    }
    
    pool->small_mem_base = MK_FP(small_seg, 0);
    
    /* Initialize large buffer descriptors */
    for (i = 0; i < RX_BUF_COUNT; i++) {
        pool->large_bufs[i].phys_addr = large_phys + (i * RX_BUF_SIZE);
        pool->large_bufs[i].virt_ptr = MK_FP(large_seg, i * RX_BUF_SIZE);
        pool->large_bufs[i].size = RX_BUF_SIZE;
        pool->large_bufs[i].in_use = 0;
    }
    
    /* Initialize small buffer descriptors */
    for (i = 0; i < RX_SMALL_BUF_COUNT; i++) {
        pool->small_bufs[i].phys_addr = small_phys + (i * RX_SMALL_BUF_SIZE);
        pool->small_bufs[i].virt_ptr = MK_FP(small_seg, i * RX_SMALL_BUF_SIZE);
        pool->small_bufs[i].size = RX_SMALL_BUF_SIZE;
        pool->small_bufs[i].in_use = 0;
    }
    
    pool->large_head = 0;
    pool->large_tail = 0;
    pool->small_head = 0;
    pool->small_tail = 0;
    
    LOG_INFO("RX buffers initialized for NIC %d: %d large, %d small",
             nic_index, RX_BUF_COUNT, RX_SMALL_BUF_COUNT);
    
    return 0;
}

/**
 * @brief Allocate an RX buffer
 */
int rx_buffer_alloc(uint8_t nic_index, uint16_t size,
                    uint32_t *phys_addr, void __far **virt_ptr) {
    rx_buffer_pool_t *pool;
    rx_buffer_t *buf = NULL;
    
    if (nic_index >= 4 || !phys_addr || !virt_ptr) {
        return -1;
    }
    
    pool = &rx_pools[nic_index];
    
    if (size <= RX_SMALL_BUF_SIZE) {
        /* Allocate from small pool */
        if (pool->small_bufs[pool->small_head].in_use == 0) {
            buf = &pool->small_bufs[pool->small_head];
            pool->small_head = (pool->small_head + 1) % RX_SMALL_BUF_COUNT;
        }
    } else {
        /* Allocate from large pool */
        if (pool->large_bufs[pool->large_head].in_use == 0) {
            buf = &pool->large_bufs[pool->large_head];
            pool->large_head = (pool->large_head + 1) % RX_BUF_COUNT;
        }
    }
    
    if (!buf) {
        pool->alloc_failures++;
        return -1;  /* No buffers available */
    }
    
    buf->in_use = 1;
    *phys_addr = buf->phys_addr;
    *virt_ptr = buf->virt_ptr;
    
    pool->alloc_count++;
    
    return 0;
}

/**
 * @brief Free an RX buffer
 */
void rx_buffer_free(uint8_t nic_index, uint32_t phys_addr) {
    rx_buffer_pool_t *pool;
    int i;
    
    if (nic_index >= 4) {
        return;
    }
    
    pool = &rx_pools[nic_index];
    
    /* Search in large pool */
    for (i = 0; i < RX_BUF_COUNT; i++) {
        if (pool->large_bufs[i].phys_addr == phys_addr) {
            pool->large_bufs[i].in_use = 0;
            pool->free_count++;
            return;
        }
    }
    
    /* Search in small pool */
    for (i = 0; i < RX_SMALL_BUF_COUNT; i++) {
        if (pool->small_bufs[i].phys_addr == phys_addr) {
            pool->small_bufs[i].in_use = 0;
            pool->free_count++;
            return;
        }
    }
}

/**
 * @brief Convert physical address to far pointer
 */
void __far *rx_buffer_phys_to_virt(uint8_t nic_index, uint32_t phys_addr) {
    rx_buffer_pool_t *pool;
    int i;
    
    if (nic_index >= 4) {
        return NULL;
    }
    
    pool = &rx_pools[nic_index];
    
    /* Search in large pool */
    for (i = 0; i < RX_BUF_COUNT; i++) {
        if (pool->large_bufs[i].phys_addr == phys_addr) {
            return pool->large_bufs[i].virt_ptr;
        }
    }
    
    /* Search in small pool */
    for (i = 0; i < RX_SMALL_BUF_COUNT; i++) {
        if (pool->small_bufs[i].phys_addr == phys_addr) {
            return pool->small_bufs[i].virt_ptr;
        }
    }
    
    return NULL;
}
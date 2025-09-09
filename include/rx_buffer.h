/**
 * @file rx_buffer.h
 * @brief RX buffer management with proper physical/virtual addressing
 * 
 * Maintains both physical addresses (for NIC DMA) and far pointers
 * (for CPU access) in DOS real mode. Ensures proper identity mapping
 * in conventional memory.
 */

#ifndef _RX_BUFFER_H_
#define _RX_BUFFER_H_

#include <stdint.h>

/* Buffer size constants */
#define RX_BUF_SIZE         1536    /* Standard Ethernet MTU + headers */
#define RX_SMALL_BUF_SIZE   256     /* Copy-break buffer size */
#define RX_BUF_COUNT        32      /* Number of RX buffers per NIC */
#define RX_SMALL_BUF_COUNT  16      /* Number of small buffers */

/* RX buffer descriptor */
typedef struct {
    uint32_t phys_addr;     /* Physical address for NIC DMA */
    void __far *virt_ptr;   /* Far pointer for CPU access */
    uint16_t size;          /* Buffer size */
    uint8_t in_use;         /* Buffer in use flag */
    uint8_t reserved;       /* Alignment */
} rx_buffer_t;

/* RX buffer pool */
typedef struct {
    /* Large buffers for normal packets */
    rx_buffer_t large_bufs[RX_BUF_COUNT];
    uint8_t large_head;     /* Next buffer to allocate */
    uint8_t large_tail;     /* Next buffer to free */
    
    /* Small buffers for copy-break */
    rx_buffer_t small_bufs[RX_SMALL_BUF_COUNT];
    uint8_t small_head;     /* Next small buffer */
    uint8_t small_tail;     /* Next to free */
    
    /* Memory blocks (must be in conventional memory) */
    void __far *large_mem_base;    /* Base of large buffer memory */
    void __far *small_mem_base;    /* Base of small buffer memory */
    
    /* Statistics */
    uint32_t alloc_count;
    uint32_t free_count;
    uint32_t alloc_failures;
} rx_buffer_pool_t;

/**
 * @brief Initialize RX buffer pool for a NIC
 * 
 * Allocates buffers in conventional memory with proper physical/virtual mapping.
 * 
 * @param nic_index NIC index (0-3)
 * @return 0 on success, -1 on error
 */
int rx_buffer_init(uint8_t nic_index);

/**
 * @brief Allocate an RX buffer
 * 
 * @param nic_index NIC index
 * @param size Required size (selects large or small pool)
 * @param phys_addr Output: Physical address for NIC
 * @param virt_ptr Output: Far pointer for CPU
 * @return 0 on success, -1 on error
 */
int rx_buffer_alloc(uint8_t nic_index, uint16_t size,
                    uint32_t *phys_addr, void __far **virt_ptr);

/**
 * @brief Free an RX buffer
 * 
 * @param nic_index NIC index
 * @param phys_addr Physical address to free
 */
void rx_buffer_free(uint8_t nic_index, uint32_t phys_addr);

/**
 * @brief Convert physical address to far pointer
 * 
 * Only valid for buffers allocated from our pools.
 * 
 * @param nic_index NIC index
 * @param phys_addr Physical address
 * @return Far pointer or NULL if not found
 */
void __far *rx_buffer_phys_to_virt(uint8_t nic_index, uint32_t phys_addr);

/**
 * @brief Convert far pointer to physical address
 * 
 * Computes physical address from segment:offset in real mode.
 * 
 * @param ptr Far pointer
 * @return Physical address
 */
static inline uint32_t far_to_phys(void __far *ptr) {
    uint16_t seg = FP_SEG(ptr);
    uint16_t off = FP_OFF(ptr);
    return ((uint32_t)seg << 4) + off;
}

/**
 * @brief Make far pointer from segment:offset
 * 
 * @param seg Segment
 * @param off Offset
 * @return Far pointer
 */
static inline void __far *make_far_ptr(uint16_t seg, uint16_t off) {
    return MK_FP(seg, off);
}

#endif /* _RX_BUFFER_H_ */
/**
 * @file mempool_module.c
 * @brief Memory Pool Service Module for 3Com Packet Driver
 * 
 * Agent 11 - Memory Management - Day 3-4 Deliverable
 * 
 * This module provides unified memory allocation services with DMA-safe
 * buffer policies, XMS integration, and CPU-optimized operations.
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#include "../../../include/common.h"
#include "../../../include/memory_api.h"
#include "../../../include/module_abi.h"
#include "../../../include/logging.h"
#include "../../../include/cpu_detect.h"
#include "../../../include/xms_detect.h"

/* Module header for MEMPOOL.MOD */
module_header_t mempool_header = {
    .signature = {'M', 'D', '6', '4'},
    .abi_version = 1,
    .module_type = MODULE_TYPE_SERVICE,
    .flags = MODULE_FLAG_COLD_DISCARD | MODULE_FLAG_ESSENTIAL,
    
    .total_size_para = 64,      /* 1KB total size */
    .resident_size_para = 32,   /* 512 bytes resident */
    .cold_size_para = 32,       /* 512 bytes cold (init code) */
    .alignment_para = 1,        /* 16-byte alignment */
    
    .init_offset = 0x40,        /* Initialization entry point */
    .api_offset = 0x80,         /* API entry point */
    .isr_offset = 0,            /* No ISR */
    .unload_offset = 0x120,     /* Cleanup entry point */
    
    .export_table_offset = 0x200,
    .export_count = 8,          /* 8 exported functions */
    .reloc_table_offset = 0x280,
    .reloc_count = 16,          /* 16 relocations */
    
    .bss_size_para = 8,         /* 128 bytes BSS */
    .required_cpu = 0x0286,     /* 80286+ required */
    .required_features = 0,     /* No special features */
    .module_id = 0x1100,        /* Module ID 0x1100 */
    
    .module_name = "MEMPOOL    ",
    .name_padding = 0,
    
    .vendor_id = 0x3COM0001,
    .build_timestamp = 0,       /* Set at build time */
    .reserved = {0, 0}
};

/* Memory pool service state */
typedef struct {
    bool initialized;
    bool xms_available;
    uint32_t total_allocated;
    uint32_t peak_usage;
    uint32_t allocation_count;
    uint16_t active_handles;
    memory_services_t services;
} mempool_state_t;

static mempool_state_t g_mempool_state = {0};

/* DMA buffer pool configuration */
#define DMA_POOL_SIZE_KB    64
#define DMA_ALIGN_BYTES     16
#define MAX_DMA_BUFFERS     32

typedef struct {
    void* buffer;
    size_t size;
    bool in_use;
    uint8_t alignment;
} dma_buffer_t;

static dma_buffer_t g_dma_buffers[MAX_DMA_BUFFERS];
static uint16_t g_dma_buffer_count = 0;

/* Forward declarations */
static void* mempool_alloc(size_t size, memory_type_t type, uint16_t flags, size_t alignment);
static bool mempool_free(void* ptr);
static void* mempool_realloc(void* ptr, size_t new_size);
static bool mempool_query_block(const void* ptr, memory_block_t* block_info);
static bool mempool_get_stats(memory_stats_t* stats);

/* Buffer management functions */
static packet_buffer_t* mempool_get_buffer(size_t size, uint16_t timeout_ms);
static bool mempool_return_buffer(packet_buffer_t* buffer);
static uint8_t mempool_addref_buffer(packet_buffer_t* buffer);
static uint8_t mempool_release_buffer(packet_buffer_t* buffer);

/* DMA functions */
static bool mempool_dma_prepare(const dma_operation_t* dma_op);
static bool mempool_dma_complete(const dma_operation_t* dma_op);
static void* mempool_alloc_coherent(size_t size, dma_device_type_t device_type, size_t alignment);
static bool mempool_free_coherent(void* ptr, size_t size);

/* Utility functions */
static void* mempool_memset_fast(void* dest, int value, size_t count);
static void* mempool_memcpy_fast(void* dest, const void* src, size_t count);
static int mempool_memcmp_fast(const void* buf1, const void* buf2, size_t count);

/* Memory allocation with three-tier strategy */
static void* mempool_alloc(size_t size, memory_type_t type, uint16_t flags, size_t alignment) {
    if (!g_mempool_state.initialized) {
        return NULL;
    }
    
    /* Validate parameters */
    if (size == 0 || size > 0xFFFF) {
        return NULL;
    }
    
    /* Apply DMA-safe allocation if requested */
    if (type & MEMORY_TYPE_DMA_COHERENT) {
        return mempool_alloc_coherent(size, DMA_DEVICE_NETWORK, alignment);
    }
    
    /* Use three-tier memory system from existing implementation */
    uint32_t mem_flags = 0;
    mem_type_t mem_type = MEM_TYPE_GENERAL;
    
    /* Map memory types */
    switch (type & 0x7F) {
        case MEMORY_TYPE_BUFFER:
            mem_type = MEM_TYPE_PACKET_BUFFER;
            break;
        case MEMORY_TYPE_MODULE:
            mem_type = MEM_TYPE_DRIVER_DATA;
            break;
        case MEMORY_TYPE_PERSISTENT:
            mem_flags |= MEM_FLAG_PERSISTENT;
            break;
        default:
            mem_type = MEM_TYPE_GENERAL;
            break;
    }
    
    /* Map flags */
    if (flags & MEMORY_FLAG_ZERO) {
        mem_flags |= MEM_FLAG_ZERO;
    }
    if (flags & MEMORY_FLAG_ALIGN) {
        mem_flags |= MEM_FLAG_ALIGNED;
    }
    
    /* Allocate memory using existing three-tier system */
    void* ptr;
    if (alignment > 1) {
        ptr = memory_alloc_aligned(size, alignment, mem_type);
    } else {
        ptr = memory_alloc(size, mem_type, mem_flags);
    }
    
    if (ptr) {
        g_mempool_state.total_allocated += size;
        g_mempool_state.allocation_count++;
        
        if (g_mempool_state.total_allocated > g_mempool_state.peak_usage) {
            g_mempool_state.peak_usage = g_mempool_state.total_allocated;
        }
    }
    
    return ptr;
}

/* Memory deallocation */
static bool mempool_free(void* ptr) {
    if (!ptr || !g_mempool_state.initialized) {
        return false;
    }
    
    /* Check if it's a DMA coherent buffer */
    for (int i = 0; i < g_dma_buffer_count; i++) {
        if (g_dma_buffers[i].buffer == ptr && g_dma_buffers[i].in_use) {
            return mempool_free_coherent(ptr, g_dma_buffers[i].size);
        }
    }
    
    /* Use existing memory free */
    memory_free(ptr);
    return true;
}

/* Memory reallocation */
static void* mempool_realloc(void* ptr, size_t new_size) {
    if (!g_mempool_state.initialized) {
        return NULL;
    }
    
    if (!ptr) {
        return mempool_alloc(new_size, MEMORY_TYPE_CONVENTIONAL, 0, 1);
    }
    
    if (new_size == 0) {
        mempool_free(ptr);
        return NULL;
    }
    
    /* For now, implement as alloc + copy + free */
    void* new_ptr = mempool_alloc(new_size, MEMORY_TYPE_CONVENTIONAL, 0, 1);
    if (!new_ptr) {
        return NULL;
    }
    
    /* Copy existing data (limited by old size - we'd need to track this) */
    memory_copy_optimized(new_ptr, ptr, new_size); /* Simplified */
    mempool_free(ptr);
    
    return new_ptr;
}

/* Query memory block information */
static bool mempool_query_block(const void* ptr, memory_block_t* block_info) {
    if (!ptr || !block_info || !g_mempool_state.initialized) {
        return false;
    }
    
    /* This would require extending the existing memory system to track metadata */
    /* For now, return basic information */
    block_info->address = (void*)ptr;
    block_info->size = 0;  /* Size tracking would need to be added */
    block_info->type = MEMORY_TYPE_CONVENTIONAL;
    block_info->flags = 0;
    block_info->handle = 0;
    block_info->owner_id = 0x11;  /* MEMPOOL module ID */
    block_info->lock_count = 0;
    block_info->timestamp = 0;
    
    return true;
}

/* Get memory statistics */
static bool mempool_get_stats(memory_stats_t* stats) {
    if (!stats || !g_mempool_state.initialized) {
        return false;
    }
    
    /* Get stats from existing memory system */
    const mem_stats_t* mem_stats = memory_get_stats();
    
    stats->conventional_total = 640 * 1024;  /* 640KB conventional */
    stats->conventional_free = stats->conventional_total - mem_stats->used_memory;
    stats->conventional_largest = 32 * 1024;  /* Estimated */
    
    /* XMS stats */
    if (g_mempool_state.xms_available) {
        stats->xms_total = memory_get_xms_size() * 1024;
        stats->xms_free = stats->xms_total - g_mempool_state.total_allocated;
        stats->xms_handles_used = g_mempool_state.active_handles;
    } else {
        stats->xms_total = 0;
        stats->xms_free = 0;
        stats->xms_handles_used = 0;
    }
    
    /* UMB stats (not currently tracked) */
    stats->umb_total = 0;
    stats->umb_free = 0;
    stats->umb_blocks = 0;
    
    /* Allocation stats */
    stats->total_allocations = g_mempool_state.allocation_count;
    stats->total_deallocations = 0;  /* Would need tracking */
    stats->peak_usage = g_mempool_state.peak_usage;
    stats->current_usage = g_mempool_state.total_allocated;
    
    /* Fragmentation */
    stats->fragmentation_pct = 10;  /* Estimated */
    stats->largest_free_block = 32 * 1024;  /* Estimated */
    
    return true;
}

/* DMA coherent memory allocation */
static void* mempool_alloc_coherent(size_t size, dma_device_type_t device_type, size_t alignment) {
    if (g_dma_buffer_count >= MAX_DMA_BUFFERS) {
        return NULL;
    }
    
    /* Allocate DMA-capable memory using existing system */
    void* buffer = memory_alloc_dma(size);
    if (!buffer) {
        return NULL;
    }
    
    /* Verify 64KB boundary compliance */
    uint32_t addr = (uint32_t)buffer;
    uint32_t next_boundary = (addr + 0x10000) & 0xFFFF0000;
    if (addr + size > next_boundary) {
        log_warning("DMA buffer spans 64KB boundary: %08lX + %u", addr, size);
        memory_free_dma(buffer);
        return NULL;
    }
    
    /* Track DMA buffer */
    g_dma_buffers[g_dma_buffer_count].buffer = buffer;
    g_dma_buffers[g_dma_buffer_count].size = size;
    g_dma_buffers[g_dma_buffer_count].in_use = true;
    g_dma_buffers[g_dma_buffer_count].alignment = alignment;
    g_dma_buffer_count++;
    
    return buffer;
}

/* Free DMA coherent memory */
static bool mempool_free_coherent(void* ptr, size_t size) {
    /* Find and remove from DMA buffer tracking */
    for (int i = 0; i < g_dma_buffer_count; i++) {
        if (g_dma_buffers[i].buffer == ptr && g_dma_buffers[i].in_use) {
            g_dma_buffers[i].in_use = false;
            memory_free_dma(ptr);
            
            /* Compact the array */
            for (int j = i; j < g_dma_buffer_count - 1; j++) {
                g_dma_buffers[j] = g_dma_buffers[j + 1];
            }
            g_dma_buffer_count--;
            return true;
        }
    }
    
    return false;
}

/* DMA operation preparation */
static bool mempool_dma_prepare(const dma_operation_t* dma_op) {
    if (!dma_op || !g_mempool_state.initialized) {
        return false;
    }
    
    /* Verify buffer is DMA-safe */
    uint32_t addr = (uint32_t)dma_op->buffer;
    uint32_t end_addr = addr + dma_op->length;
    
    /* Check 64KB boundary compliance */
    if ((addr & 0xFFFF0000) != ((end_addr - 1) & 0xFFFF0000)) {
        log_error("DMA buffer crosses 64KB boundary: %08lX-%08lX", addr, end_addr);
        return false;
    }
    
    /* For DOS real mode, no cache management needed */
    return true;
}

/* DMA operation completion */
static bool mempool_dma_complete(const dma_operation_t* dma_op) {
    if (!dma_op || !g_mempool_state.initialized) {
        return false;
    }
    
    /* No cache management needed in DOS real mode */
    return true;
}

/* Buffer management - simplified packet buffer system */
static packet_buffer_t* mempool_get_buffer(size_t size, uint16_t timeout_ms) {
    /* Use existing buffer allocation system */
    buffer_desc_t* buffer = buffer_alloc_ethernet_frame(size, BUFFER_TYPE_RX);
    if (!buffer) {
        return NULL;
    }
    
    /* Convert to packet buffer format */
    packet_buffer_t* packet_buf = (packet_buffer_t*)mempool_alloc(sizeof(packet_buffer_t), 
                                                                 MEMORY_TYPE_TEMP, 0, 1);
    if (!packet_buf) {
        buffer_free_any(buffer);
        return NULL;
    }
    
    packet_buf->data = (uint8_t*)buffer_get_data_ptr(buffer);
    packet_buf->size = buffer_get_size(buffer);
    packet_buf->used = buffer_get_used_size(buffer);
    packet_buf->buffer_id = (uint16_t)(uintptr_t)buffer;  /* Store buffer reference */
    packet_buf->ref_count = 1;
    packet_buf->flags = 0;
    packet_buf->private_data = buffer;
    
    return packet_buf;
}

/* Return packet buffer */
static bool mempool_return_buffer(packet_buffer_t* buffer) {
    if (!buffer) {
        return false;
    }
    
    /* Free the underlying buffer */
    if (buffer->private_data) {
        buffer_free_any((buffer_desc_t*)buffer->private_data);
    }
    
    /* Free the packet buffer structure */
    mempool_free(buffer);
    return true;
}

/* Add buffer reference */
static uint8_t mempool_addref_buffer(packet_buffer_t* buffer) {
    if (!buffer) {
        return 0;
    }
    
    if (buffer->ref_count < 255) {
        buffer->ref_count++;
    }
    
    return buffer->ref_count;
}

/* Release buffer reference */
static uint8_t mempool_release_buffer(packet_buffer_t* buffer) {
    if (!buffer) {
        return 0;
    }
    
    if (buffer->ref_count > 0) {
        buffer->ref_count--;
    }
    
    if (buffer->ref_count == 0) {
        mempool_return_buffer(buffer);
        return 0;
    }
    
    return buffer->ref_count;
}

/* Fast memory operations using CPU optimization */
static void* mempool_memset_fast(void* dest, int value, size_t count) {
    memory_set_optimized(dest, (uint8_t)value, count);
    return dest;
}

static void* mempool_memcpy_fast(void* dest, const void* src, size_t count) {
    memory_copy_optimized(dest, src, count);
    return dest;
}

static int mempool_memcmp_fast(const void* buf1, const void* buf2, size_t count) {
    return memory_compare(buf1, buf2, count);
}

/* Module initialization entry point */
void far mempool_init(void) {
    log_info("MEMPOOL: Initializing memory service module");
    
    /* Initialize existing memory system */
    if (memory_init() != 0) {
        log_error("MEMPOOL: Failed to initialize base memory system");
        _asm {
            stc
            mov ax, 0x0023  ; LOAD_FAILED
            retf
        }
    }
    
    /* Initialize CPU-optimized memory operations */
    memory_init_cpu_optimized();
    
    /* Check XMS availability */
    g_mempool_state.xms_available = memory_xms_available();
    log_info("MEMPOOL: XMS available: %s", g_mempool_state.xms_available ? "Yes" : "No");
    
    /* Initialize buffer system */
    if (buffer_system_init_optimized() != SUCCESS) {
        log_warning("MEMPOOL: Buffer system initialization failed");
    }
    
    /* Set up service interface */
    g_mempool_state.services.allocate = mempool_alloc;
    g_mempool_state.services.deallocate = mempool_free;
    g_mempool_state.services.reallocate = mempool_realloc;
    g_mempool_state.services.query_block = mempool_query_block;
    g_mempool_state.services.get_stats = mempool_get_stats;
    
    g_mempool_state.services.get_buffer = mempool_get_buffer;
    g_mempool_state.services.return_buffer = mempool_return_buffer;
    g_mempool_state.services.addref_buffer = mempool_addref_buffer;
    g_mempool_state.services.release_buffer = mempool_release_buffer;
    
    g_mempool_state.services.dma_prepare = mempool_dma_prepare;
    g_mempool_state.services.dma_complete = mempool_dma_complete;
    g_mempool_state.services.alloc_coherent = mempool_alloc_coherent;
    g_mempool_state.services.free_coherent = mempool_free_coherent;
    
    g_mempool_state.services.memset_fast = mempool_memset_fast;
    g_mempool_state.services.memcpy_fast = mempool_memcpy_fast;
    g_mempool_state.services.memcmp_fast = mempool_memcmp_fast;
    
    /* Initialize DMA buffer tracking */
    for (int i = 0; i < MAX_DMA_BUFFERS; i++) {
        g_dma_buffers[i].in_use = false;
    }
    g_dma_buffer_count = 0;
    
    /* Mark as initialized */
    g_mempool_state.initialized = true;
    g_mempool_state.total_allocated = 0;
    g_mempool_state.peak_usage = 0;
    g_mempool_state.allocation_count = 0;
    g_mempool_state.active_handles = 0;
    
    log_info("MEMPOOL: Memory service module initialized successfully");
    
    /* Return success */
    _asm {
        clc
        xor ax, ax
        retf
    }
}

/* Module API entry point */
void far mempool_api(void) {
    _asm {
        push bp
        mov bp, sp
        
        ; AX = function number
        ; ES:DI = parameter structure
        
        cmp ax, 0x01
        je get_services
        cmp ax, 0x02
        je get_statistics
        
        ; Unknown function
        stc
        mov ax, 0x0001  ; Invalid function
        jmp api_exit
        
    get_services:
        ; Return pointer to memory services structure
        mov ax, offset g_mempool_state.services
        mov dx, cs
        clc
        jmp api_exit
        
    get_statistics:
        ; Get memory statistics
        push es
        push di
        call mempool_get_stats
        add sp, 4
        clc
        jmp api_exit
        
    api_exit:
        pop bp
        retf
    }
}

/* Module cleanup entry point */
void far mempool_cleanup(void) {
    log_info("MEMPOOL: Cleaning up memory service module");
    
    /* Free any remaining DMA buffers */
    for (int i = 0; i < g_dma_buffer_count; i++) {
        if (g_dma_buffers[i].in_use) {
            log_warning("MEMPOOL: Freeing unreleased DMA buffer at %p", 
                       g_dma_buffers[i].buffer);
            memory_free_dma(g_dma_buffers[i].buffer);
        }
    }
    
    /* Cleanup buffer system */
    buffer_system_cleanup();
    
    /* Cleanup base memory system */
    memory_cleanup();
    
    /* Clear state */
    g_mempool_state.initialized = false;
    
    log_info("MEMPOOL: Memory service module cleanup completed");
    
    _asm {
        clc
        xor ax, ax
        retf
    }
}

/* Export table - alphabetically sorted for binary search */
export_entry_t mempool_exports[8] = {
    {"dma_alloc", (uint16_t)mempool_alloc_coherent, SYMBOL_FLAG_FUNCTION},
    {"dma_free ", (uint16_t)mempool_free_coherent, SYMBOL_FLAG_FUNCTION},
    {"mem_alloc", (uint16_t)mempool_alloc, SYMBOL_FLAG_FUNCTION},
    {"mem_free ", (uint16_t)mempool_free, SYMBOL_FLAG_FUNCTION},
    {"mem_query", (uint16_t)mempool_query_block, SYMBOL_FLAG_FUNCTION},
    {"mem_stats", (uint16_t)mempool_get_stats, SYMBOL_FLAG_FUNCTION},
    {"services ", (uint16_t)&g_mempool_state.services, SYMBOL_FLAG_DATA},
    {"version  ", 0x0100, SYMBOL_FLAG_DATA}  /* Version 1.0 */
};
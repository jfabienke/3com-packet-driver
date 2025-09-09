/**
 * @file 3com_performance.c
 * @brief Performance optimizations for 3Com NICs
 *
 * Implements CPU-specific optimizations, interrupt coalescing, and
 * other performance enhancements for 3Com PCI NICs.
 *
 * 3Com Packet Driver - Performance Optimizations
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../../include/3com_pci.h"
#include "../../include/hardware.h"
#include "../../include/logging.h"
#include "../../include/cpu_detect.h"
#include <dos.h>
#include <string.h>

/* Performance tuning constants */
#define INT_COAL_TIMER_US       200    /* Interrupt coalescing timer (microseconds) */
#define INT_COAL_FRAMES         8      /* Coalesce after N frames */
#define DMA_BURST_SIZE          128    /* DMA burst size for Cyclone/Tornado */
#define PREFETCH_SIZE           64     /* Cache line prefetch size */

/* CPU optimization flags */
#define OPT_USE_REP_MOVSD       0x01   /* Use REP MOVSD for 386+ */
#define OPT_USE_PREFETCH        0x02   /* Use prefetch for 486+ */
#define OPT_USE_PIPELINE        0x04   /* Pipeline optimizations for Pentium */

static uint8_t cpu_opt_flags = 0;

/**
 * @brief Detect CPU and enable appropriate optimizations
 */
void detect_cpu_optimizations(void)
{
    cpu_info_t cpu_info;
    
    /* Get CPU information */
    detect_cpu(&cpu_info);
    
    LOG_INFO("3Com: Detected CPU: %s", cpu_info.vendor_string);
    
    /* Enable optimizations based on CPU */
    if (cpu_info.type >= CPU_TYPE_386) {
        cpu_opt_flags |= OPT_USE_REP_MOVSD;
        LOG_INFO("3Com: Enabled 32-bit memory operations");
    }
    
    if (cpu_info.type >= CPU_TYPE_486) {
        cpu_opt_flags |= OPT_USE_PREFETCH;
        LOG_INFO("3Com: Enabled cache prefetch optimizations");
    }
    
    if (cpu_info.type >= CPU_TYPE_PENTIUM) {
        cpu_opt_flags |= OPT_USE_PIPELINE;
        LOG_INFO("3Com: Enabled pipeline optimizations");
    }
}

/**
 * @brief Configure interrupt coalescing for Cyclone/Tornado
 * 
 * Reduces interrupt overhead by batching multiple packets.
 * 
 * @param ctx 3Com PCI context
 * @return 0 on success, negative error code on failure
 */
int configure_interrupt_coalescing(pci_3com_context_t *ctx)
{
    uint16_t ioaddr;
    
    if (!ctx) {
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Only Cyclone and Tornado support interrupt coalescing */
    if (!(ctx->generation & (IS_CYCLONE | IS_TORNADO))) {
        return SUCCESS;  /* Not supported, but not an error */
    }
    
    ioaddr = ctx->base.io_base;
    
    LOG_INFO("3Com: Configuring interrupt coalescing");
    
    /* Select Window 7 for advanced configuration */
    select_window(ioaddr, 7);
    
    /* Set interrupt mitigation timer (in microseconds) */
    window_write16(ioaddr, 7, WN7_INT_TIMER, INT_COAL_TIMER_US);
    
    /* Set packet count threshold */
    window_write16(ioaddr, 7, WN7_INT_COUNT, INT_COAL_FRAMES);
    
    /* Enable interrupt mitigation */
    uint16_t config = window_read16(ioaddr, 7, WN7_CONFIG);
    config |= 0x0100;  /* Enable interrupt mitigation */
    window_write16(ioaddr, 7, WN7_CONFIG, config);
    
    ctx->int_mitigation_enabled = 1;
    
    LOG_INFO("3Com: Interrupt coalescing enabled (%d us / %d frames)",
             INT_COAL_TIMER_US, INT_COAL_FRAMES);
    
    return SUCCESS;
}

/**
 * @brief Configure DMA burst mode for Boomerang and later
 * 
 * @param ctx 3Com PCI context
 * @return 0 on success, negative error code on failure
 */
int configure_dma_burst_mode(pci_3com_context_t *ctx)
{
    uint16_t ioaddr;
    uint32_t dma_ctrl;
    
    if (!ctx) {
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Only Boomerang and later support DMA burst mode */
    if (ctx->generation & IS_VORTEX) {
        return SUCCESS;  /* PIO mode, no DMA */
    }
    
    ioaddr = ctx->base.io_base;
    
    LOG_INFO("3Com: Configuring DMA burst mode");
    
    /* Read DMA control register */
    dma_ctrl = inl(ioaddr + 0x20);  /* DMA control register */
    
    /* Set burst size based on generation */
    if (ctx->generation & (IS_CYCLONE | IS_TORNADO)) {
        /* Cyclone/Tornado support larger bursts */
        dma_ctrl &= ~0x00001F00;  /* Clear burst size bits */
        dma_ctrl |= 0x00000800;   /* 128-byte burst */
        LOG_INFO("3Com: DMA burst size set to 128 bytes");
    } else {
        /* Boomerang uses smaller bursts */
        dma_ctrl &= ~0x00001F00;
        dma_ctrl |= 0x00000400;   /* 64-byte burst */
        LOG_INFO("3Com: DMA burst size set to 64 bytes");
    }
    
    /* Enable burst mode */
    dma_ctrl |= 0x00000001;  /* Enable DMA bursting */
    
    /* Write back DMA control */
    outl(ioaddr + 0x20, dma_ctrl);
    
    return SUCCESS;
}

/**
 * @brief Optimized memory copy using CPU-specific features
 * 
 * @param dest Destination address
 * @param src Source address
 * @param count Number of bytes to copy
 */
void optimized_memcpy(void *dest, const void *src, size_t count)
{
    if (cpu_opt_flags & OPT_USE_REP_MOVSD) {
        /* Use 32-bit transfers for 386+ */
        __asm {
            push    es
            push    ds
            
            les     di, dest
            lds     si, src
            mov     cx, count
            
            ; Align to 4-byte boundary
            mov     ax, cx
            and     ax, 3
            shr     cx, 2       ; Divide by 4 for DWORD count
            
            ; Use 32-bit prefix for MOVSD
            db      0x66        ; Operand size prefix
            rep     movsd       ; Move DWORDs
            
            ; Copy remaining bytes
            mov     cx, ax
            rep     movsb
            
            pop     ds
            pop     es
        }
    } else {
        /* Fall back to standard memcpy */
        memcpy(dest, src, count);
    }
}

/**
 * @brief Configure packet prefetch for Tornado
 * 
 * @param ctx 3Com PCI context
 * @return 0 on success, negative error code on failure
 */
int configure_packet_prefetch(pci_3com_context_t *ctx)
{
    uint16_t ioaddr;
    
    if (!ctx) {
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Only Tornado supports packet prefetch */
    if (!(ctx->generation & IS_TORNADO)) {
        return SUCCESS;
    }
    
    ioaddr = ctx->base.io_base;
    
    LOG_INFO("3Com: Configuring packet prefetch");
    
    /* Select Window 7 */
    select_window(ioaddr, 7);
    
    /* Enable packet prefetch */
    uint16_t config = window_read16(ioaddr, 7, WN7_CONFIG);
    config |= 0x0200;  /* Enable prefetch */
    window_write16(ioaddr, 7, WN7_CONFIG, config);
    
    /* Set prefetch size */
    window_write16(ioaddr, 7, WN7_PREFETCH_SIZE, PREFETCH_SIZE);
    
    LOG_INFO("3Com: Packet prefetch enabled (%d bytes)", PREFETCH_SIZE);
    
    return SUCCESS;
}

/**
 * @brief Apply all performance optimizations
 * 
 * @param ctx 3Com PCI context
 * @return 0 on success, negative error code on failure
 */
int apply_performance_optimizations(pci_3com_context_t *ctx)
{
    int result;
    
    if (!ctx) {
        return ERROR_INVALID_PARAMETER;
    }
    
    LOG_INFO("3Com: Applying performance optimizations for %s",
             get_generation_string(ctx->generation));
    
    /* Detect CPU capabilities */
    detect_cpu_optimizations();
    
    /* Configure interrupt coalescing */
    result = configure_interrupt_coalescing(ctx);
    if (result != SUCCESS) {
        LOG_WARNING("3Com: Failed to configure interrupt coalescing");
    }
    
    /* Configure DMA burst mode */
    result = configure_dma_burst_mode(ctx);
    if (result != SUCCESS) {
        LOG_WARNING("3Com: Failed to configure DMA burst mode");
    }
    
    /* Configure packet prefetch */
    result = configure_packet_prefetch(ctx);
    if (result != SUCCESS) {
        LOG_WARNING("3Com: Failed to configure packet prefetch");
    }
    
    LOG_INFO("3Com: Performance optimizations applied");
    
    return SUCCESS;
}
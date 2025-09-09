/**
 * @file 3com_power.c
 * @brief Power management for 3Com NICs
 *
 * Implements Wake-on-LAN, ACPI power states, and CardBus power management
 * for 3Com PCI/CardBus network controllers.
 *
 * 3Com Packet Driver - Power Management Implementation
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../../include/3com_pci.h"
#include "../../include/hardware.h"
#include "../../include/logging.h"
#include <dos.h>

/* Power management register offsets */
#define PM_CTRL             0xE0    /* Power management control */
#define PM_STATUS           0xE4    /* Power management status */
#define WOL_CTRL            0xF0    /* Wake-on-LAN control */
#define WOL_PATTERN         0xF4    /* Wake-on-LAN pattern */

/* Power management control bits */
#define PM_STATE_D0         0x00    /* Full power */
#define PM_STATE_D1         0x01    /* Light sleep */
#define PM_STATE_D2         0x02    /* Deep sleep */
#define PM_STATE_D3         0x03    /* Power off */
#define PM_PME_ENABLE       0x0100  /* Enable PME# signal */
#define PM_PME_STATUS       0x8000  /* PME# status */

/* Wake-on-LAN control bits */
#define WOL_MAGIC_ENABLE    0x0001  /* Enable magic packet wake */
#define WOL_PATTERN_ENABLE  0x0002  /* Enable pattern match wake */
#define WOL_LINK_ENABLE     0x0004  /* Enable link change wake */
#define WOL_BROADCAST_EN    0x0008  /* Enable broadcast wake */

/**
 * @brief Configure Wake-on-LAN for supported NICs
 * 
 * @param ctx 3Com PCI context
 * @param wol_modes Wake-on-LAN modes to enable
 * @return 0 on success, negative error code on failure
 */
int configure_wake_on_lan(pci_3com_context_t *ctx, uint16_t wol_modes)
{
    uint16_t ioaddr;
    uint16_t wol_ctrl;
    
    if (!ctx) {
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Only Cyclone, Tornado, and CardBus support WOL */
    if (!(ctx->generation & (IS_CYCLONE | IS_TORNADO)) && 
        !(ctx->capabilities & HAS_CB_FNS)) {
        LOG_INFO("3Com: Wake-on-LAN not supported on %s",
                 get_generation_string(ctx->generation));
        return ERROR_NOT_SUPPORTED;
    }
    
    ioaddr = ctx->base.io_base;
    
    LOG_INFO("3Com: Configuring Wake-on-LAN");
    
    /* Select Window 7 for power management */
    select_window(ioaddr, 7);
    
    /* Read current WOL control */
    wol_ctrl = window_read16(ioaddr, 7, WN7_WOL_CTRL);
    
    /* Clear all WOL modes first */
    wol_ctrl &= ~(WOL_MAGIC_ENABLE | WOL_PATTERN_ENABLE | 
                  WOL_LINK_ENABLE | WOL_BROADCAST_EN);
    
    /* Enable requested WOL modes */
    if (wol_modes & WOL_MODE_MAGIC) {
        wol_ctrl |= WOL_MAGIC_ENABLE;
        LOG_INFO("3Com: Enabled magic packet wake");
    }
    
    if (wol_modes & WOL_MODE_PATTERN) {
        wol_ctrl |= WOL_PATTERN_ENABLE;
        LOG_INFO("3Com: Enabled pattern match wake");
    }
    
    if (wol_modes & WOL_MODE_LINK) {
        wol_ctrl |= WOL_LINK_ENABLE;
        LOG_INFO("3Com: Enabled link change wake");
    }
    
    if (wol_modes & WOL_MODE_BROADCAST) {
        wol_ctrl |= WOL_BROADCAST_EN;
        LOG_INFO("3Com: Enabled broadcast wake");
    }
    
    /* Write WOL control */
    window_write16(ioaddr, 7, WN7_WOL_CTRL, wol_ctrl);
    
    /* Enable PME# signal for wake events */
    uint16_t pm_ctrl = inpw(ioaddr + PM_CTRL);
    pm_ctrl |= PM_PME_ENABLE;
    outpw(ioaddr + PM_CTRL, pm_ctrl);
    
    ctx->wol_enabled = (wol_modes != 0);
    
    return SUCCESS;
}

/**
 * @brief Set WOL pattern for pattern match wake
 * 
 * @param ctx 3Com PCI context
 * @param pattern Pattern bytes to match
 * @param mask Mask bytes (1 = compare, 0 = ignore)
 * @param length Pattern length (max 128 bytes)
 * @return 0 on success, negative error code on failure
 */
int set_wol_pattern(pci_3com_context_t *ctx, const uint8_t *pattern,
                    const uint8_t *mask, uint16_t length)
{
    uint16_t ioaddr;
    int i;
    
    if (!ctx || !pattern || !mask || length > 128) {
        return ERROR_INVALID_PARAMETER;
    }
    
    if (!(ctx->wol_enabled)) {
        LOG_ERROR("3Com: WOL not enabled");
        return ERROR_NOT_INITIALIZED;
    }
    
    ioaddr = ctx->base.io_base;
    
    LOG_INFO("3Com: Setting WOL pattern (%d bytes)", length);
    
    /* Select Window 7 */
    select_window(ioaddr, 7);
    
    /* Write pattern to pattern registers */
    for (i = 0; i < length; i++) {
        outpb(ioaddr + WOL_PATTERN + i, pattern[i]);
    }
    
    /* Write mask to mask registers */
    for (i = 0; i < (length + 7) / 8; i++) {
        outpb(ioaddr + WOL_PATTERN + 128 + i, mask[i]);
    }
    
    /* Set pattern length */
    window_write16(ioaddr, 7, WN7_WOL_PATTERN_LEN, length);
    
    return SUCCESS;
}

/**
 * @brief Set ACPI power state
 * 
 * @param ctx 3Com PCI context
 * @param power_state Power state (D0-D3)
 * @return 0 on success, negative error code on failure
 */
int set_power_state(pci_3com_context_t *ctx, uint8_t power_state)
{
    uint16_t ioaddr;
    uint16_t pm_ctrl;
    
    if (!ctx) {
        return ERROR_INVALID_PARAMETER;
    }
    
    if (power_state > PM_STATE_D3) {
        LOG_ERROR("3Com: Invalid power state %d", power_state);
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Only newer NICs support ACPI power states */
    if (!(ctx->generation & (IS_CYCLONE | IS_TORNADO))) {
        return ERROR_NOT_SUPPORTED;
    }
    
    ioaddr = ctx->base.io_base;
    
    LOG_INFO("3Com: Setting power state to D%d", power_state);
    
    /* Read current power management control */
    pm_ctrl = inpw(ioaddr + PM_CTRL);
    
    /* Clear current power state */
    pm_ctrl &= ~0x0003;
    
    /* Set new power state */
    pm_ctrl |= power_state;
    
    /* Handle state-specific configuration */
    switch (power_state) {
        case PM_STATE_D0:
            /* Full power - enable all functions */
            outw(ioaddr + EL3_CMD, PowerUp);
            break;
            
        case PM_STATE_D1:
        case PM_STATE_D2:
            /* Sleep states - maintain WOL if enabled */
            if (ctx->wol_enabled) {
                pm_ctrl |= PM_PME_ENABLE;
            }
            break;
            
        case PM_STATE_D3:
            /* Power off - only WOL active */
            outw(ioaddr + EL3_CMD, PowerDown);
            if (ctx->wol_enabled) {
                pm_ctrl |= PM_PME_ENABLE;
            }
            break;
    }
    
    /* Write power management control */
    outpw(ioaddr + PM_CTRL, pm_ctrl);
    
    ctx->power_state = power_state;
    
    return SUCCESS;
}

/**
 * @brief Handle CardBus power management events
 * 
 * @param ctx 3Com PCI context
 * @param event CardBus power event
 * @return 0 on success, negative error code on failure
 */
int handle_cardbus_power_event(pci_3com_context_t *ctx, uint8_t event)
{
    if (!ctx) {
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Only CardBus NICs need this */
    if (!(ctx->capabilities & HAS_CB_FNS)) {
        return SUCCESS;
    }
    
    LOG_INFO("3Com: Handling CardBus power event 0x%02X", event);
    
    switch (event) {
        case CB_EVENT_SUSPEND:
            /* Prepare for suspend */
            LOG_INFO("3Com: CardBus suspend requested");
            
            /* Save current state */
            ctx->saved_power_state = ctx->power_state;
            
            /* Enter D2 state for suspend */
            set_power_state(ctx, PM_STATE_D2);
            break;
            
        case CB_EVENT_RESUME:
            /* Resume from suspend */
            LOG_INFO("3Com: CardBus resume requested");
            
            /* Restore previous power state */
            set_power_state(ctx, ctx->saved_power_state);
            
            /* Re-initialize if needed */
            if (ctx->saved_power_state == PM_STATE_D0) {
                /* Restore operational settings */
                outw(ctx->base.io_base + EL3_CMD, TxEnable);
                outw(ctx->base.io_base + EL3_CMD, RxEnable);
            }
            break;
            
        case CB_EVENT_REMOVE:
            /* Card removal */
            LOG_INFO("3Com: CardBus removal detected");
            
            /* Power down completely */
            set_power_state(ctx, PM_STATE_D3);
            break;
            
        default:
            LOG_WARNING("3Com: Unknown CardBus event 0x%02X", event);
            break;
    }
    
    return SUCCESS;
}

/**
 * @brief Get current power management status
 * 
 * @param ctx 3Com PCI context
 * @param status Pointer to store power status
 * @return 0 on success, negative error code on failure
 */
int get_power_status(pci_3com_context_t *ctx, power_status_t *status)
{
    uint16_t ioaddr;
    uint16_t pm_status;
    
    if (!ctx || !status) {
        return ERROR_INVALID_PARAMETER;
    }
    
    ioaddr = ctx->base.io_base;
    
    /* Read power management status */
    pm_status = inpw(ioaddr + PM_STATUS);
    
    /* Fill status structure */
    status->power_state = ctx->power_state;
    status->wol_enabled = ctx->wol_enabled;
    status->pme_status = (pm_status & PM_PME_STATUS) ? 1 : 0;
    
    /* Check for wake event */
    if (status->pme_status) {
        LOG_INFO("3Com: PME# wake event detected");
        
        /* Clear PME# status */
        outpw(ioaddr + PM_STATUS, PM_PME_STATUS);
    }
    
    return SUCCESS;
}
/**
 * @file app_callback.c
 * @brief Application Callback System Implementation
 * 
 * Provides safe far call mechanisms for invoking client application
 * callbacks with proper DS fixup and alternate stack support for TSR context.
 */

#include "app_callback.h"
#include "tsr_memory.h"
#include "../include/logging.h"
#include <dos.h>
#include <string.h>

/* Global alternate stack for IRQ context (1KB stack) */
#define ALT_STACK_SIZE 1024
static uint8_t g_alt_stack[ALT_STACK_SIZE];
uint16_t g_irq_alt_ss = 0;
uint16_t g_irq_alt_sp = 0;

/* Callback system state */
static int g_callback_system_initialized = 0;
static volatile int g_callback_in_progress = 0;

/**
 * @brief Initialize callback system
 */
int callback_system_init(void)
{
    if (g_callback_system_initialized) {
        return CB_SUCCESS;
    }
    
    /* Set up alternate stack for IRQ context */
    g_irq_alt_ss = FP_SEG(g_alt_stack);
    g_irq_alt_sp = FP_OFF(g_alt_stack) + ALT_STACK_SIZE - 2; /* Top of stack */
    
    g_callback_system_initialized = 1;
    
    LOG_INFO("Callback system initialized with alternate stack at %04X:%04X", 
             g_irq_alt_ss, g_irq_alt_sp);
    
    return CB_SUCCESS;
}

/**
 * @brief Register an application callback
 */
int callback_register(APP_CB *cb, void far *entry, uint16_t client_ds, callback_type_t type)
{
    if (!cb || !entry) {
        return CB_ERROR_INVALID_CB;
    }
    
    /* Basic DS validation - should be a reasonable segment */
    if (client_ds == 0 || client_ds == 0xFFFF) {
        return CB_ERROR_INVALID_DS;
    }
    
    /* Initialize callback record */
    memset(cb, 0, sizeof(APP_CB));
    cb->entry = entry;
    cb->client_ds = client_ds;
    cb->alt_ss = 0;  /* Will use global alternate stack */
    cb->alt_sp = 0;
    
    LOG_DEBUG("Registered %s callback at %04X:%04X with DS=%04X",
              (type == CB_TYPE_REGISTER) ? "register" : "cdecl",
              FP_SEG(entry), FP_OFF(entry), client_ds);
    
    return CB_SUCCESS;
}

/**
 * @brief Check if callback is safe to invoke
 */
int callback_is_safe(APP_CB *cb)
{
    if (!cb) return 0;
    if (!cb->entry) return 0;
    if (cb->client_ds == 0 || cb->client_ds == 0xFFFF) return 0;
    
    /* Check if we're already in a callback (prevent reentrancy) */
    if (g_callback_in_progress) return 0;
    
    return 1;
}

/**
 * @brief Invoke packet receiver callback (register-based)
 */
int callback_invoke_receiver(APP_CB *cb, void far *packet_data,
                           uint16_t length, uint16_t linktype, uint16_t handle)
{
    int use_alt_stack = 0;
    
    if (!callback_is_safe(cb)) {
        return CB_ERROR_NOT_SAFE;
    }
    
    /* Use alternate stack if we're in interrupt context */
    /* Simple heuristic: if current SS is not a typical DOS data segment */
    use_alt_stack = (g_irq_alt_ss != 0);
    
    /* Set reentrancy guard */
    _disable();
    g_callback_in_progress = 1;
    _enable();
    
    LOG_DEBUG("Invoking receiver callback: len=%u, type=%04X, handle=%04X, alt_stack=%d",
              length, linktype, handle, use_alt_stack);
    
    /* Call the assembly trampoline */
    call_recv_reg_tramp(cb, linktype, handle, length, packet_data, use_alt_stack);
    
    /* Clear reentrancy guard */
    _disable();
    g_callback_in_progress = 0;
    _enable();
    
    return CB_SUCCESS;
}

/**
 * @brief Invoke generic callback (C calling convention)
 */
int callback_invoke_cdecl(APP_CB *cb, void far *arg0, uint16_t arg1, uint16_t arg2)
{
    int use_alt_stack = 0;
    
    if (!callback_is_safe(cb)) {
        return CB_ERROR_NOT_SAFE;
    }
    
    /* Use alternate stack if available */
    use_alt_stack = (g_irq_alt_ss != 0);
    
    /* Set reentrancy guard */
    _disable();
    g_callback_in_progress = 1;
    _enable();
    
    LOG_DEBUG("Invoking cdecl callback: arg1=%u, arg2=%u, alt_stack=%d",
              arg1, arg2, use_alt_stack);
    
    /* Call the assembly trampoline */
    call_cdecl_tramp(cb, arg0, arg1, arg2, use_alt_stack);
    
    /* Clear reentrancy guard */
    _disable();
    g_callback_in_progress = 0;
    _enable();
    
    return CB_SUCCESS;
}

/**
 * @brief Set alternate stack for IRQ context callbacks
 */
int callback_set_alt_stack(APP_CB *cb, uint16_t stack_seg, uint16_t stack_ptr)
{
    if (!cb) {
        return CB_ERROR_INVALID_CB;
    }
    
    cb->alt_ss = stack_seg;
    cb->alt_sp = stack_ptr;
    
    LOG_DEBUG("Set alternate stack for callback: %04X:%04X", stack_seg, stack_ptr);
    
    return CB_SUCCESS;
}

/**
 * @brief Cleanup callback system
 */
void callback_system_cleanup(void)
{
    if (!g_callback_system_initialized) {
        return;
    }
    
    /* Wait for any in-progress callbacks to complete */
    while (g_callback_in_progress) {
        /* Spin wait - should be very brief */
    }
    
    g_irq_alt_ss = 0;
    g_irq_alt_sp = 0;
    g_callback_system_initialized = 0;
    
    LOG_DEBUG("Callback system cleanup completed");
}

/**
 * @brief Safe packet receiver callback with error handling
 * 
 * High-level wrapper that includes comprehensive error handling
 * and logging for packet delivery to applications.
 */
int callback_deliver_packet(APP_CB *cb, void far *packet_data,
                          uint16_t length, uint16_t linktype, uint16_t handle)
{
    int result;
    
    if (!g_callback_system_initialized) {
        LOG_ERROR("Callback system not initialized");
        return CB_ERROR_NOT_SAFE;
    }
    
    if (!packet_data || length == 0) {
        LOG_ERROR("Invalid packet data: ptr=%p, len=%u", packet_data, length);
        return CB_ERROR_INVALID_CB;
    }
    
    /* Validate packet length is reasonable */
    if (length > 1514) {  /* Standard Ethernet MTU */
        LOG_WARNING("Large packet length: %u bytes", length);
    }
    
    result = callback_invoke_receiver(cb, packet_data, length, linktype, handle);
    
    if (result != CB_SUCCESS) {
        LOG_ERROR("Callback invocation failed: error=%d", result);
    } else {
        LOG_DEBUG("Packet delivered successfully to application");
    }
    
    return result;
}

/**
 * @brief Check if callback system is ready
 */
int callback_system_ready(void)
{
    return g_callback_system_initialized;
}

/**
 * @brief Get callback system status
 */
void callback_get_status(int *initialized, int *in_progress, uint16_t *alt_ss, uint16_t *alt_sp)
{
    if (initialized) *initialized = g_callback_system_initialized;
    if (in_progress) *in_progress = g_callback_in_progress;
    if (alt_ss) *alt_ss = g_irq_alt_ss;
    if (alt_sp) *alt_sp = g_irq_alt_sp;
}
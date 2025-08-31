/**
 * @file app_callback.h
 * @brief Application Callback System Interface
 * 
 * Provides safe far call mechanisms for invoking client application
 * callbacks with proper DS fixup and alternate stack support for TSR context.
 */

#ifndef APP_CALLBACK_H
#define APP_CALLBACK_H

#include <i86.h>
#include <stdint.h>

/**
 * @brief Application callback record
 * 
 * Stores all information needed to safely invoke an application
 * callback from TSR context.
 */
typedef struct APP_CB {
    void (far *entry)(void);   /* 00h: far entry CS:IP */
    uint16_t client_ds;        /* 04h: client's DGROUP */
    uint16_t alt_ss;           /* 06h: optional alt stack SS */
    uint16_t alt_sp;           /* 08h: optional alt stack SP */
} APP_CB;

/**
 * @brief Callback types for different calling conventions
 */
typedef enum {
    CB_TYPE_REGISTER,  /* Register-based (Crynwr/packet driver style) */
    CB_TYPE_CDECL,     /* C calling convention (__far __cdecl) */
    CB_TYPE_PASCAL     /* Pascal calling convention (reserved) */
} callback_type_t;

/* Assembly trampolines implemented in cbtramp.asm */

/**
 * @brief Register-based callback trampoline
 * 
 * Calls application with register-based convention:
 * AX = linktype, BX = handle, CX = length, ES:DI = packet data
 * 
 * @param cb Callback record
 * @param axv AX register value (linktype)
 * @param bxv BX register value (handle)
 * @param cxv CX register value (length)
 * @param pkt_esdi Far pointer to packet data
 * @param use_alt_stack Use alternate stack if available
 */
void call_recv_reg_tramp(APP_CB *cb,
                         uint16_t axv,
                         uint16_t bxv,
                         uint16_t cxv,
                         void far *pkt_esdi,
                         int use_alt_stack);

/**
 * @brief C calling convention trampoline
 * 
 * @param cb Callback record
 * @param arg0 First argument (far pointer)
 * @param arg1 Second argument
 * @param arg2 Third argument
 * @param use_alt_stack Use alternate stack if available
 */
void call_cdecl_tramp(APP_CB *cb,
                      void far *arg0,
                      uint16_t arg1,
                      uint16_t arg2,
                      int use_alt_stack);

/* High-level callback management */

/**
 * @brief Initialize callback system
 * 
 * Sets up callback infrastructure including alternate stacks.
 * 
 * @return 0 on success, negative on error
 */
int callback_system_init(void);

/**
 * @brief Register an application callback
 * 
 * @param cb Callback record to initialize
 * @param entry Far pointer to callback function
 * @param client_ds Client's data segment
 * @param type Callback type (register/cdecl)
 * @return 0 on success, negative on error
 */
int callback_register(APP_CB *cb, void far *entry, uint16_t client_ds, callback_type_t type);

/**
 * @brief Invoke packet receiver callback (register-based)
 * 
 * @param cb Callback record
 * @param packet_data Far pointer to packet
 * @param length Packet length
 * @param linktype Link layer type
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int callback_invoke_receiver(APP_CB *cb, void far *packet_data, 
                           uint16_t length, uint16_t linktype, uint16_t handle);

/**
 * @brief Invoke generic callback (C calling convention)
 * 
 * @param cb Callback record
 * @param arg0 First argument
 * @param arg1 Second argument
 * @param arg2 Third argument
 * @return 0 on success, negative on error
 */
int callback_invoke_cdecl(APP_CB *cb, void far *arg0, uint16_t arg1, uint16_t arg2);

/**
 * @brief Set alternate stack for IRQ context callbacks
 * 
 * @param cb Callback record
 * @param stack_seg Stack segment
 * @param stack_ptr Stack pointer (top of stack)
 * @return 0 on success, negative on error
 */
int callback_set_alt_stack(APP_CB *cb, uint16_t stack_seg, uint16_t stack_ptr);

/**
 * @brief Check if callback is safe to invoke
 * 
 * Performs basic validation of callback record.
 * 
 * @param cb Callback record
 * @return 1 if safe, 0 if not safe
 */
int callback_is_safe(APP_CB *cb);

/**
 * @brief Cleanup callback system
 */
void callback_system_cleanup(void);

/* Global alternate stack for IRQ context */
extern uint16_t g_irq_alt_ss;
extern uint16_t g_irq_alt_sp;

/* Error codes */
#define CB_SUCCESS            0
#define CB_ERROR_INVALID_CB  -1
#define CB_ERROR_INVALID_DS  -2
#define CB_ERROR_NO_MEMORY   -3
#define CB_ERROR_NOT_SAFE    -4

#endif /* APP_CALLBACK_H */
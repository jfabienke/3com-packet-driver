# Vtable Integration Architecture Specification

Last Updated: 2025-09-04
Status: supplemental
Purpose: Define the vtable-based HAL interfaces for the unified .EXE driver.

## Overview

This document defines the authoritative vtable integration pattern for the 3Com packet driver, establishing the foundation for Phase 5 modular architecture while enabling immediate functionality through existing implementations.

## Architectural Decision

**Date**: 2025-08-19
**Decision**: Adopt**Vtable Pattern with Module Interface** as the core architectural foundation
**Status**: ✅ **APPROVED** and documented

## Design Rationale

### Requirements Analysis
1. Unified Driver Support: Support ISA + PCI families via a vtable-based HAL
2. **DOS Memory Constraints**: <6KB resident core in 640KB conventional memory
3. **Existing Codebase**: Leverage 22+ existing hardware implementations
4. **Runtime Polymorphism**: Dynamic dispatch based on detected hardware

### Alternative Patterns Rejected
- **Direct Implementation**: Can't support modular loading, too large resident size
- **Pure HAL**: Too complex for DOS environment, adds unnecessary abstraction
- **Hybrid Approach**: Current mess with parallel patterns, no clear integration

## Vtable Structure Specification

### Core NIC Operations Table
```c
// include/hardware.h - Already defined!
typedef struct nic_ops {
    /* Core lifecycle */
    int (*init)(struct nic_info *nic);
    int (*cleanup)(struct nic_info *nic);
    int (*reset)(struct nic_info *nic);
    int (*self_test)(struct nic_info *nic);

    /* Packet operations - CRITICAL PATH */
    int (*send_packet)(struct nic_info *nic, const uint8_t *packet, size_t len);
    int (*receive_packet)(struct nic_info *nic, uint8_t *buffer, size_t *len);
    int (*check_tx_complete)(struct nic_info *nic);
    int (*check_rx_available)(struct nic_info *nic);

    /* Interrupt handling */
    void (*handle_interrupt)(struct nic_info *nic);
    int (*check_interrupt)(struct nic_info *nic);
    int (*enable_interrupts)(struct nic_info *nic);
    int (*disable_interrupts)(struct nic_info *nic);

    /* Configuration */
    int (*set_mac_address)(struct nic_info *nic, const uint8_t *mac);
    int (*get_mac_address)(struct nic_info *nic, uint8_t *mac);
    int (*set_receive_mode)(struct nic_info *nic, uint16_t mode);
} nic_ops_t;
```

### Module Interface Extension
```c
// include/module_api.h - For Phase 5
typedef struct {
    uint16_t magic;           // 'MD' (0x4D44)
    uint16_t version;         // Module version
    uint16_t vtable_offset;   // Offset to nic_ops_t structure
    uint16_t init_offset;     // Module initialization function
    uint16_t family_id;       // FAMILY_ETHERLINK3, FAMILY_CORKSCREW, etc.
    char name[32];           // Module name
} module_header_t;
```

## Integration Patterns

### 1. Vtable Wiring Pattern
```c
// src/c/hardware.c - CRITICAL FIX
static void init_3c509b_ops(void) {
    // CURRENT (BROKEN):
    // g_3c509b_ops.send_packet = NULL; /* TODO: Implement */

    // REQUIRED (WORKING):
    extern int _3c509b_send_packet(nic_info_t*, const uint8_t*, size_t);
    extern int _3c509b_receive_packet(nic_info_t*, uint8_t*, size_t*);
    extern int _3c509b_init(nic_info_t*);
    extern void _3c509b_handle_interrupt(nic_info_t*);

    g_3c509b_ops.init = _3c509b_init;
    g_3c509b_ops.send_packet = _3c509b_send_packet;
    g_3c509b_ops.receive_packet = _3c509b_receive_packet;
    g_3c509b_ops.handle_interrupt = _3c509b_handle_interrupt;
    // ... connect all function pointers
}
```

### 2. Assembly-C Bridge Pattern
```asm
; src/asm/packet_api.asm - INT 60h Entry Point
api_send_packet PROC
    ; Convert DOS calling convention to C
    push    bp
    mov     bp, sp

    ; Parameters from DOS registers -> C stack
    push    si          ; packet buffer (DS:SI)
    push    cx          ; packet length
    push    ax          ; handle

    ; Call C dispatcher
    extern _pd_send_packet
    call    _pd_send_packet
    add     sp, 6

    ; Convert C return to DOS registers
    ; AX = return code, DH = error code
    pop     bp
    ret
api_send_packet ENDP
```

### 3. Hardware Dispatch Pattern
```c
// src/c/api.c - C Dispatcher
int pd_send_packet(uint16_t handle, void *packet, uint16_t length) {
    // Find NIC by handle
    nic_info_t *nic = find_nic_by_handle(handle);
    if (!nic || !nic->ops || !nic->ops->send_packet) {
        return ERROR_NOT_SUPPORTED;
    }

    // Dispatch through vtable - POLYMORPHIC!
    return nic->ops->send_packet(nic, packet, length);
}
```

### 4. Module Loading Pattern (Phase 5)
```c
// Future: src/c/module_loader.c
int load_hardware_module(const char* module_name) {
    // No .MOD loading in unified packaging; vtable provided at init
    module_header_t *header = load_module_file(module_name);

    // Get vtable from module
    nic_ops_t *ops = (nic_ops_t*)((char*)header + header->vtable_offset);

    // Register vtable for detected NICs
    register_nic_ops(header->family_id, ops);

    return SUCCESS;
}
```

## Critical Implementation Points

### 1. PnP Activation (BLOCKING)
```asm
; src/asm/pnp.asm - MUST IMPLEMENT
pnp_configure_device PROC
    ; Write I/O base address to PnP registers
    mov     dx, PNP_IO_BASE_HIGH
    mov     al, bh          ; High byte of I/O base
    out     dx, al
    mov     dx, PNP_IO_BASE_LOW
    mov     al, bl          ; Low byte of I/O base
    out     dx, al

    ; Configure IRQ
    mov     dx, PNP_IRQ_SELECT
    mov     al, cl          ; IRQ number
    out     dx, al

    ; Activate device - CRITICAL!
    mov     dx, PNP_ACTIVATE
    mov     al, 1           ; Enable device
    out     dx, al

    ; Device is now ready for I/O operations
    mov     ax, 0           ; Success
    ret
pnp_configure_device ENDP
```

### 2. Calling Convention Standards
```c
// All vtable functions use consistent signature:
// - Return: int (0=success, negative=error)
// - First parameter: nic_info_t *nic (context)
// - Additional parameters as needed
// - Must preserve DOS registers if called from ASM

// Assembly functions use near calls with stack cleanup:
// - Parameters pushed right-to-left
// - Caller cleans stack
// - Return value in AX
```

## Integration Timeline

### Phase 1: Core Wiring (4 hours)
1. **Connect vtable pointers** in hardware.c init functions
2. **Implement PnP activation** in pnp.asm
3. **Bridge INT 60h handlers** in packet_api.asm
4. **Test basic packet flow** end-to-end

### Phase 2: API Completion (2 hours)
1. **Implement remaining stubs** in api.c using vtable dispatch
2. **Complete handle management** and receive callbacks
3. **Test with DOS applications** (mTCP, etc.)

### Result: Complete packet driver functionality through vtable pattern!

## Module Compatibility (Phase 5)

### PTASK.MOD Structure
```c
// Module exports nic_ops_t at vtable_offset
static nic_ops_t etherlink3_ops = {
    .init = etherlink3_init,
    .send_packet = etherlink3_send_packet,
    .receive_packet = etherlink3_receive_packet,
    // ... all operations
};

module_header_t ptask_header = {
    .magic = 0x4D44,                    // 'MD'
    .version = 0x0100,                  // Version 1.0
    .vtable_offset = offsetof(ptask_module_t, ops),
    .family_id = FAMILY_ETHERLINK3,
    .name = "PTASK - EtherLink III Family"
};
```

## Validation Criteria

### Phase 1 Success
- ✅ NIC detection works (already passing)
- ✅ Vtable pointers connected to implementations
- ✅ PnP activation enables hardware I/O
- ✅ Basic packet transmission/reception functional

### Phase 2 Success
- ✅ Full Packet Driver API compliance
- ✅ Multiple applications can use driver simultaneously
- ✅ Handle management working correctly
- ✅ Production-ready DOS packet driver

This vtable integration pattern provides the foundation for immediate functionality while preparing for Phase 5 modular architecture expansion.

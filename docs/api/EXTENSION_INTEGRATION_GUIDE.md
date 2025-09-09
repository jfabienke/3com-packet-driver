# Extension API Integration Guide

Last Updated: 2025-09-04
Status: supplemental
Purpose: How to consume vendor extension APIs safely from tools.

## Integration Points

### 1. packet_driver_isr Modification

The extension check must be placed BEFORE any existing extended dispatch (0x20-0x29) to avoid conflicts:

```assembly
; In packet_api_smc.asm, modify packet_driver_isr:

packet_driver_isr:
        ; Save initial state
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    bp
        push    ds
        push    es
        
        ; Get our data segment
        mov     ax, cs
        mov     ds, ax
        
        ; === ADD EXTENSION CHECK HERE (BEFORE 0x20 range) ===
        cmp     ah, 80h                 ; Vendor extension range?
        jae     extension_dispatch      ; Handle 80h-FFh
        
        ; Check for extended functions (0x20-0x29)
        cmp     ah, 20h
        jb      .standard_function
        cmp     ah, 29h
        ja      .bad_command
        
        ; ... rest of ISR continues
```

### 2. Makefile Addition

```makefile
# Add to HOT_ASM_OBJS (resident)
HOT_ASM_OBJS = $(BUILDDIR)/packet_api_smc.obj \
               $(BUILDDIR)/nic_irq_smc.obj \
               $(BUILDDIR)/hardware_smc.obj \
               $(BUILDDIR)/extension_api_final.obj \  # ADD THIS
               $(BUILDDIR)/flow_routing.obj \
               ...
```

### 3. API Ready Guard

Ensure api_ready flag is set after initialization:

```c
// In api.c or init.c
uint8_t api_ready = 0;  // Global flag

// After successful initialization
void api_init_complete(void) {
    // ... other init ...
    
    // Initialize extension snapshots
    init_extension_snapshots();
    
    // Mark API ready for requests
    api_ready = 1;
}
```

## Snapshot Update Paths

### Safe Update Mechanism

The seqlock ensures atomic snapshot updates without long CLI sections:

```c
// In C code for deferred updates
void update_safety_state_deferred(void) {
    safety_snapshot_t new_state;
    
    // Build new state
    new_state.flags = 0;
    if (global_force_pio_mode) new_state.flags |= SAFETY_PIO_FORCED;
    if (patches_verified) new_state.flags |= SAFETY_PATCHES_OK;
    if (dma_boundary_check) new_state.flags |= SAFETY_BOUNDARY_CHECK;
    if (dma_validated) new_state.flags |= SAFETY_DMA_VALIDATED;
    
    new_state.stack_free = calculate_stack_free();
    new_state.patch_count = total_patches_applied;
    
    // Update via assembly routine
    update_snapshot_safe(&new_state, 
                        offsetof(snapshots, safety_snapshot),
                        sizeof(safety_snapshot_t));
}
```

### Update Triggers

| Event | Snapshot Updated | Fields Changed |
|-------|-----------------|----------------|
| Driver init | All | Initial values |
| Bus master test pass | Safety, Version | DMA enabled flags |
| DMA failure | Safety | Kill switch, PIO forced |
| Stack overflow detected | Safety | Stack guard flag |
| Patch verification | Safety | Patches OK flag |

## Error Code Reference

Standardized error codes across all vendor functions:

```c
#define EXT_SUCCESS           0x0000  /* Operation successful */
#define EXT_ERR_NOT_READY    0x7000  /* API not initialized */
#define EXT_ERR_TOO_SMALL    0x7001  /* Buffer too small */
#define EXT_ERR_BAD_FUNCTION 0x7002  /* Invalid function code */
#define EXT_ERR_NO_BUFFER    0x7003  /* Buffer required but missing */
#define EXT_ERR_TIMEOUT      0x7004  /* Operation timed out */
```

## Capability Discovery

The capability bitmask (AH=80h, DX register) allows future expansion:

```c
// Current capabilities (v1.0)
#define CAP_DISCOVERY  0x0001  /* AH=80h implemented */
#define CAP_SAFETY     0x0002  /* AH=81h implemented */
#define CAP_PATCHES    0x0004  /* AH=82h implemented */
#define CAP_MEMORY     0x0008  /* AH=83h implemented */
#define CAP_VERSION    0x0010  /* AH=84h implemented */
#define CAP_CURRENT    0x001F  /* All current capabilities */

// Future capabilities (reserved)
#define CAP_PERF       0x0020  /* AH=85h performance counters */
#define CAP_CONFIG     0x0040  /* AH=86h runtime config */
#define CAP_STATS      0x0080  /* AH=87h statistics */
```

## Verification Checklist

### Pre-Integration
- [ ] api_ready flag defined and accessible
- [ ] Seqlock initialized to 0 (even)
- [ ] Snapshots initialized with valid data
- [ ] Error codes defined in header

### Integration
- [ ] Extension check added before 0x20 range
- [ ] extension_api_final.obj in HOT_ASM_OBJS
- [ ] No cold symbols referenced from hot code
- [ ] Register preservation verified

### Post-Integration
- [ ] Run EXTTEST.EXE - all tests pass
- [ ] Measure dispatch overhead (<5 cycles added)
- [ ] Verify memory map shows +85 bytes
- [ ] Test with existing packet driver clients

## Timing Verification

Measure the dispatch overhead:

```assembly
; Test dispatch timing
        rdtsc
        mov     [start_tsc], eax
        
        ; Non-vendor call (should bypass)
        mov     ah, 01h
        int     60h
        
        rdtsc
        sub     eax, [start_tsc]
        ; Should be within 5 cycles of baseline
```

## Stage 1 Integration

When Stage 1 (bus master test) completes:

```c
// On successful validation
if (bus_master_test_passed()) {
    // Update via safe mechanism
    extern void update_dma_enabled(void);
    update_dma_enabled();  // Sets DMA flags in snapshots
    
    // Log the change
    LOG_INFO("DMA enabled, snapshots updated");
}

// On failure or safety trigger
if (dma_error_detected()) {
    extern void force_pio_mode(void);
    force_pio_mode();  // Sets kill switch + PIO forced
    
    LOG_ERROR("DMA failed, PIO forced via extension API");
}
```

## Memory Impact Summary

```
Component           Size    Location
-----------------   ----    --------
Extension check     3       In existing ISR
Extension handler   38      HOT_ASM_OBJS
Seqlock            2       HOT data
Timestamp          2       HOT data  
Snapshots          40      HOT data
-----------------   ----
Total              85 bytes

Original budget:    45 bytes code + reasonable data
Actual:            41 bytes code + 44 bytes data
Status:            ✓ Within reasonable limits
```

## Testing Script

```bash
# Build with extension
make clean
make ENABLE_EXTENSION=1

# Verify size
size build/3cpd.exe | grep -E "(text|data)"
# Should show ~85 byte increase

# Run tests
./test/exttest.com

# Verify with packet driver test
./test/pkttest.com
# Should work unchanged
```

---
**Status**: READY FOR INTEGRATION
**Risk**: LOW
**Next**: Proceed to Stage 1 after verification

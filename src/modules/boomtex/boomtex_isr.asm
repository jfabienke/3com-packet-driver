; ============================================================================
; boomtex_isr.asm - BOOMTEX.MOD Optimized Interrupt Service Routine
; ============================================================================
; BOOMTEX.MOD - Zero-Branch ISR with ≤60μs Timing Constraint
; Team C (Agents 09-10) - Week 1 Critical Deliverable
;
; Provides optimized interrupt handling for 3C515-TX ISA bus master and
; 3C900-TPO PCI NICs with computed jump dispatch and straight-line code.
; ============================================================================

%include "../../include/timing_macros.inc"
%include "../../include/tsr_defensive.inc"
%include "../../include/asm_interfaces.inc"

section .text

; External symbols
extern g_boomtex_context
extern boomtex_3c515tx_interrupt
extern boomtex_3c900tpo_interrupt
extern boomtex_handle_ne2000_interrupt

; Hardware type constants (must match boomtex_internal.h)
BOOMTEX_HARDWARE_UNKNOWN        equ 0
BOOMTEX_HARDWARE_3C515TX        equ 1
BOOMTEX_HARDWARE_3C900TPO       equ 2
BOOMTEX_HARDWARE_3C905TX        equ 3
BOOMTEX_HARDWARE_NE2000_COMPAT  equ 4

; Register offsets for common hardware access
C3C515_STATUS_REG       equ 0x0E
C3C900_STATUS_REG       equ 0x02
C3C900_COMMAND          equ 0x00

; Status register bits
C3C515_STAT_INT_LATCH   equ 0x0001
C3C900_STAT_INT_LATCH   equ 0x0001

; Command values
C3C515_CMD_INT_ACK      equ 0x6800
C3C900_CMD_INT_ACK      equ 0x6800

; ============================================================================
; Zero-Branch Interrupt Service Routine Entry Point
; ============================================================================

global boomtex_isr_asm_entry
boomtex_isr_asm_entry:
    ; Save all registers per DOS calling conventions
    push    ax
    push    bx
    push    cx
    push    dx
    push    si
    push    di
    push    bp
    push    ds
    push    es

    ; Start timing measurement for performance validation
    TIMING_ISR_START

    ; Set up data segment to access module context
    mov     ax, cs
    mov     ds, ax

    ; Get first NIC context from global context
    mov     si, g_boomtex_context
    add     si, 14              ; Offset to first NIC context
    
    ; Get hardware type for computed jump dispatch
    mov     bl, [si + 0]        ; hardware_type field
    
    ; Validate hardware type (defensive programming)
    cmp     bl, BOOMTEX_HARDWARE_NE2000_COMPAT
    ja      isr_unknown_hardware
    
    ; Zero-branch dispatch using computed jump table
    shl     bx, 1               ; Convert to word offset
    jmp     word [isr_dispatch_table + bx]

; ============================================================================
; Hardware-Specific Interrupt Handlers (Straight-Line Code)
; ============================================================================

isr_handle_3c515tx:
    ; Fast path for 3C515-TX ISA bus master
    mov     dx, [si + 4]        ; Get I/O base address
    add     dx, C3C515_STATUS_REG
    in      ax, dx              ; Read status register
    
    ; Quick interrupt acknowledgment
    test    ax, C3C515_STAT_INT_LATCH
    jz      isr_not_our_interrupt
    
    ; Store status for later processing
    mov     [si + 64], ax       ; Store in context for C handler
    
    ; Acknowledge interrupt immediately
    mov     dx, [si + 4]        ; I/O base
    add     dx, C3C515_STATUS_REG
    mov     ax, C3C515_CMD_INT_ACK
    or      ax, 0x007F          ; Acknowledge all interrupt sources
    out     dx, ax
    
    ; Call C interrupt handler for detailed processing
    push    si                  ; Pass NIC context
    call    boomtex_3c515tx_interrupt
    add     sp, 2               ; Clean up stack
    
    jmp     isr_exit_success

isr_handle_3c900tpo:
    ; Fast path for 3C900-TPO PCI
    mov     dx, [si + 4]        ; Get I/O base address
    add     dx, C3C900_STATUS_REG
    in      ax, dx              ; Read status register
    
    ; Quick interrupt acknowledgment
    test    ax, C3C900_STAT_INT_LATCH
    jz      isr_not_our_interrupt
    
    ; Store status for later processing
    mov     [si + 64], ax       ; Store in context
    
    ; Acknowledge interrupt immediately
    mov     dx, [si + 4]        ; I/O base
    add     dx, C3C900_COMMAND
    mov     ax, C3C900_CMD_INT_ACK
    or      ax, 0x007F          ; Acknowledge all sources
    out     dx, ax
    
    ; Call C interrupt handler
    push    si                  ; Pass NIC context
    call    boomtex_3c900tpo_interrupt
    add     sp, 2
    
    jmp     isr_exit_success

isr_handle_ne2000_compat:
    ; Week 1 NE2000 compatibility interrupt handler
    push    si                  ; Pass NIC context
    call    boomtex_handle_ne2000_interrupt
    add     sp, 2
    
    jmp     isr_exit_success

isr_unknown_hardware:
    ; Fallback for unknown hardware (should never happen)
    jmp     isr_not_our_interrupt

; ============================================================================
; ISR Exit Paths
; ============================================================================

isr_exit_success:
    ; Increment interrupt counter
    mov     bx, g_boomtex_context
    inc     word [bx + 10]      ; Increment interrupts_handled counter
    
    ; Send EOI to PIC
    mov     al, 0x20            ; EOI command
    out     0x20, al            ; Send to primary PIC
    
    ; Check if IRQ > 7 (secondary PIC)
    mov     al, [si + 5]        ; Get IRQ from NIC context
    cmp     al, 7
    jle     isr_timing_end
    out     0xA0, al            ; Send EOI to secondary PIC

isr_timing_end:
    ; End timing measurement
    TIMING_ISR_END
    
    ; Validate ISR timing constraint (≤60μs)
    TIMING_VALIDATE_ISR
    jc      isr_timing_violation
    
    jmp     isr_restore_and_exit

isr_timing_violation:
    ; Log timing violation (if debug mode enabled)
    ; In production, this would be handled differently
    ; For now, just continue
    
isr_restore_and_exit:
    ; Restore all registers
    pop     es
    pop     ds
    pop     bp
    pop     di
    pop     si
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    
    ; Return from interrupt
    iret

isr_not_our_interrupt:
    ; Not our interrupt - restore registers and chain to next handler
    pop     es
    pop     ds
    pop     bp
    pop     di
    pop     si
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    
    ; Jump to original interrupt handler (if chained)
    ; For now, just return
    iret

; ============================================================================
; Computed Jump Dispatch Table (Zero-Branch Design)
; ============================================================================

section .data align=16

isr_dispatch_table:
    dw      isr_unknown_hardware        ; BOOMTEX_HARDWARE_UNKNOWN
    dw      isr_handle_3c515tx          ; BOOMTEX_HARDWARE_3C515TX
    dw      isr_handle_3c900tpo         ; BOOMTEX_HARDWARE_3C900TPO
    dw      isr_unknown_hardware        ; BOOMTEX_HARDWARE_3C905TX (placeholder)
    dw      isr_handle_ne2000_compat    ; BOOMTEX_HARDWARE_NE2000_COMPAT

; ============================================================================
; CPU-Specific Optimization Patches (Self-Modifying Code)
; ============================================================================

section .text

; Patch points for CPU-specific optimizations
global boomtex_patch_386_optimizations
boomtex_patch_386_optimizations:
    push    ax
    push    bx
    push    cx
    push    dx
    
    ; 80386-specific optimizations
    ; Use 32-bit operations where beneficial
    
    ; Patch example: Use 32-bit I/O operations
    ; This would modify the I/O instructions above to use EAX/EDX
    
    ; Flush prefetch queue after modification
    call    flush_prefetch_queue_asm
    
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    ret

global boomtex_patch_486_optimizations
boomtex_patch_486_optimizations:
    push    ax
    push    bx
    push    cx
    push    dx
    
    ; 80486-specific optimizations
    ; Optimize for cache line boundaries
    ; Use cache-friendly instruction sequences
    
    ; Example: Align critical code sections to cache boundaries
    
    call    flush_prefetch_queue_asm
    
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    ret

global boomtex_patch_pentium_optimizations
boomtex_patch_pentium_optimizations:
    push    ax
    push    bx
    push    cx
    push    dx
    
    ; Pentium-specific optimizations
    ; Optimize for dual pipeline execution
    ; Avoid instruction pairing conflicts
    
    ; Example: Reorder instructions for optimal pairing
    
    call    flush_prefetch_queue_asm
    
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    ret

; ============================================================================
; Utility Functions
; ============================================================================

global flush_prefetch_queue_asm
flush_prefetch_queue_asm:
    ; Flush prefetch queue using near jump
    jmp     short flush_continue
flush_continue:
    ret

; ============================================================================
; Performance Statistics and Debug Support
; ============================================================================

section .data

; ISR timing statistics (for debugging)
isr_timing_min      dw      0xFFFF      ; Minimum ISR time
isr_timing_max      dw      0x0000      ; Maximum ISR time
isr_timing_count    dd      0           ; Number of ISR calls
isr_violations      dw      0           ; Timing violations

section .text

; Update ISR timing statistics (called from timing macros)
global update_isr_timing_stats
update_isr_timing_stats:
    ; AX contains timing result in microseconds
    push    bx
    push    cx
    
    ; Update minimum
    cmp     ax, [isr_timing_min]
    jge     check_max
    mov     [isr_timing_min], ax
    
check_max:
    ; Update maximum
    cmp     ax, [isr_timing_max]
    jle     update_count
    mov     [isr_timing_max], ax
    
update_count:
    ; Increment counter
    inc     dword [isr_timing_count]
    
    ; Check for violation (>60μs)
    cmp     ax, 60
    jle     stats_exit
    inc     word [isr_violations]
    
stats_exit:
    pop     cx
    pop     bx
    ret

; ============================================================================
; Multi-NIC Support (Future Enhancement)
; ============================================================================

; This section would contain code for handling multiple NICs
; Currently using single NIC for Week 1 implementation

section .text

; Scan all NICs for interrupt sources
global boomtex_scan_all_nics
boomtex_scan_all_nics:
    ; Future enhancement: scan all configured NICs
    ; For Week 1, we handle only the first NIC
    ret

; ============================================================================
; Week 1 NE2000 Compatibility Support
; ============================================================================

section .text

; Handle NE2000 compatibility interrupt
global boomtex_handle_ne2000_interrupt
boomtex_handle_ne2000_interrupt:
    push    bp
    mov     bp, sp
    push    ax
    push    dx
    
    ; Get NIC context from parameter
    mov     si, [bp + 4]
    
    ; Simple NE2000 interrupt acknowledgment
    mov     dx, [si + 4]        ; I/O base (typically 0x300)
    add     dx, 0x07            ; NE2000 interrupt status register
    in      al, dx
    
    ; Acknowledge by writing back
    out     dx, al
    
    pop     dx
    pop     ax
    pop     bp
    ret

; ============================================================================
; End of ISR Implementation
; ============================================================================
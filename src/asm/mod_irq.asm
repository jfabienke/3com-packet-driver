;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_irq.asm
;; @brief Hardware IRQ handler with EOI (JIT-assembled)
;;
;; Handles the hardware interrupt from the NIC. Saves all registers, calls
;; the NIC-specific ISR via a patchable CALL, sends EOI to the PIC, and
;; returns via IRET. Supports both master (IRQ 0-7) and slave (IRQ 8-15).
;;
;; Hot path size target: ~0.8KB
;;
;; Last Updated: 2026-02-01 11:37:22 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16
cpu 286

%include "patch_macros.inc"

;; ============================================================================
;; Patch type constants (local aliases)
;; ============================================================================
PATCH_TYPE_IMM16        equ 6
PATCH_TYPE_IMM8         equ 7
PATCH_TYPE_RELOC_NEAR   equ 8

;; ============================================================================
;; PIC ports
;; ============================================================================
PIC_MASTER_CMD          equ 20h
PIC_SLAVE_CMD           equ 0A0h
EOI_CMD                 equ 20h

;; ============================================================================
;; IRQ mitigation
;; ============================================================================
IRQ_MITIGATION_THRESH   equ 64          ; Max IRQs before yielding

;; ############################################################################
;; MODULE SEGMENT
;; ############################################################################
segment MODULE class=MODULE align=16

;; ============================================================================
;; 64-byte Module Header
;; ============================================================================
global _mod_irq_header
_mod_irq_header:
    db  'PKTDRV', 0             ; +00  7 bytes: module signature
    db  1                       ; +07  1 byte:  major version
    db  0                       ; +08  1 byte:  minor version
    db  0                       ; +09  1 byte:  cpu_req
    db  0                       ; +0A  1 byte:  nic_type
    db  0                       ; +0B  1 byte:  cap_flags
    dw  hot_end - hot_start     ; +0C  2 bytes: hot code size
    dw  patch_table             ; +0E  2 bytes: offset to patch table
    dw  patch_table_end - patch_table  ; +10  2 bytes: patch table size
    dw  hot_start               ; +12  2 bytes: offset to hot_start
    dw  hot_end                 ; +14  2 bytes: offset to hot_end
    times 64 - ($ - _mod_irq_header) db 0  ; Pad to 64 bytes

;; ============================================================================
;; HOT PATH START
;; ============================================================================
hot_start:

;; ----------------------------------------------------------------------------
;; irq_entry - Hardware interrupt handler entry point
;; ----------------------------------------------------------------------------
global irq_entry
irq_entry:
    push    ax
    push    bx
    push    cx
    push    dx
    push    si
    push    di
    push    ds
    push    es
    push    bp

    ;; Set DS to our data segment
    push    cs
    pop     ds

    ;; Increment IRQ mitigation counter
    inc     word [irq_count]
    cmp     word [irq_count], IRQ_MITIGATION_THRESH
    jb      irq_below_thresh
    mov     word [irq_count], 0             ; Reset counter
irq_below_thresh:

    ;; Read the IRQ number (patched by SMC with actual value)
    ;; This patch point stores the IRQ number as an immediate byte
    PATCH_POINT pp_irq_number
    ;; After patching, the above becomes: mov al, <irq#>
    ;; For now, default to 0 (nop sled is safe)

    ;; Call the NIC-specific interrupt service routine
    ;; Patched to the correct NIC ISR at load time
    PATCH_POINT_CALL pp_nic_isr, irq_nic_isr_fallback

    ;; Send EOI to the PIC
    ;; Determine if slave PIC needs EOI (IRQ >= 8)
    cmp     byte [irq_is_slave], 0
    je      irq_master_only

    ;; Send EOI to slave PIC (port A0h)
    PATCH_POINT_IO pp_eoi_slave, {mov al, EOI_CMD}
    out     PIC_SLAVE_CMD, al

irq_master_only:
    ;; Send EOI to master PIC (port 20h) - always required
    mov     al, EOI_CMD
    out     PIC_MASTER_CMD, al

    ;; Restore registers and return
    pop     bp
    pop     es
    pop     ds
    pop     di
    pop     si
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    iret

;; ----------------------------------------------------------------------------
;; Fallback NIC ISR - does nothing if not patched
;; ----------------------------------------------------------------------------
irq_nic_isr_fallback:
    ret

;; ============================================================================
;; Local data (within hot path)
;; ============================================================================
irq_count:
    dw  0                                   ; IRQ mitigation counter

irq_is_slave:
    db  0                                   ; 0 = master (IRQ 0-7), 1 = slave (IRQ 8-15)

hot_end:

;; ============================================================================
;; PATCH TABLE
;; ============================================================================
patch_table:
    PATCH_TABLE_ENTRY  pp_irq_number, PATCH_TYPE_IMM8
    PATCH_TABLE_ENTRY  pp_nic_isr,    PATCH_TYPE_RELOC_NEAR
    PATCH_TABLE_ENTRY  pp_eoi_slave,  PATCH_TYPE_IO
patch_table_end:

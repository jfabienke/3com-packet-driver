;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file pci_shim_isr.asm
;; @brief INT 1Ah ISR wrapper for PCI BIOS shim
;;
;; Provides the interrupt service routine wrapper for intercepting PCI BIOS
;; calls. Filters for PCI config read/write functions (AH=B1h, AL=08h-0Dh)
;; and calls C handler for broken functions, otherwise chains to original BIOS.
;;
;; Properly preserves all registers including ECX high 16 bits for dword ops.
;; Sets carry flag by modifying saved FLAGS on stack, not current flags.
;;
;; 3Com Packet Driver - PCI BIOS Shim ISR
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16
cpu 386                         ; Enable 386 instructions

; C symbol naming bridge (maps C symbols to symbol_)
%include "csym.inc"

; =============================================================================
; JIT MODULE HEADER
; =============================================================================
segment MODULE class=MODULE align=16

global _mod_pciisr_header
_mod_pciisr_header:
    db  'PKTDRV', 0             ; +00  7 bytes: module signature
    db  1                       ; +07  1 byte:  major version
    db  0                       ; +08  1 byte:  minor version
    db  0                       ; +09  1 byte:  cpu_req
    db  0                       ; +0A  1 byte:  nic_type (0 = generic)
    db  1                       ; +0B  1 byte:  cap_flags (1 = MOD_CAP_CORE)
    dw  hot_end - hot_start     ; +0C  2 bytes: hot code size
    dw  patch_table             ; +0E  2 bytes: offset to patch table
    dw  patch_table_end - patch_table  ; +10  2 bytes: patch table size
    dw  hot_start               ; +12  2 bytes: offset to hot_start
    dw  hot_end                 ; +14  2 bytes: offset to hot_end
    times 64 - ($ - _mod_pciisr_header) db 0  ; Pad to 64 bytes

; Data segment (will be merged with C data segment at link time)
segment _DATA class=DATA
        ; Empty - just for group declaration

; Code segment
segment _TEXT class=CODE
hot_start:

; External symbols
; pci_shim_handler_c is defined in linkasm.asm (ASM-to-ASM, no underscore needed)
extern pci_shim_handler_c       ; C handler function (ASM stub)
; Note: old_int1a_offset/segment are set via set_chain_vector(), not extern symbols

; Data segment group
group DGROUP _DATA

; Register context structure passed to C handler
; PCI_REGS structure offsets
%define pr_ax       0           ; AX register (function in AL)
%define pr_bx       2           ; BX register (bus/dev/func)
%define pr_cx_low   4           ; CX low 16 bits
%define pr_cx_high  6           ; CX high 16 bits (for ECX)
%define pr_dx_low   8           ; DX low 16 bits
%define pr_dx_high  10          ; DX high 16 bits (for EDX)
%define pr_di       12          ; DI register (offset)
%define pr_si       14          ; SI register
%define PCI_REGS_SIZE 16        ; Size of structure

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; PCI BIOS shim interrupt service routine
;;
;; Intercepts INT 1Ah, filters for PCI config functions, and either handles
;; via C shim or chains to original BIOS.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _pci_shim_isr
_pci_shim_isr:
    ; CPU has already pushed FLAGS, CS, IP
    ; Stack layout: [FLAGS][CS][IP]

    ; Save all registers we might modify
    push    ax
    push    bx
    push    cx
    push    dx
    push    si
    push    di
    push    bp
    push    ds
    push    es

    ; Quick filter - is this a PCI BIOS call?
    cmp     ah, 0B1h
    jne     chain_to_bios

    ; Is it a config read/write function we handle?
    cmp     al, 08h             ; Read Config Byte
    jb      chain_to_bios
    cmp     al, 0Dh             ; Write Config Dword
    ja      chain_to_bios

    ; Set up DS for our data segment
    push    ax                  ; Save AX
    mov     ax, DGROUP
    mov     ds, ax
    pop     ax                  ; Restore AX

    ; Build register context structure on stack
    sub     sp, PCI_REGS_SIZE
    mov     bp, sp              ; BP points to structure

    ; Fill in the structure
    mov     [bp+pr_ax], ax
    mov     [bp+pr_bx], bx
    mov     [bp+pr_cx_low], cx
    mov     [bp+pr_di], di
    mov     [bp+pr_si], si

    ; Get DX low
    mov     [bp+pr_dx_low], dx

    ; Get ECX high 16 bits without modifying it
    ; Use EAX as temporary (we already saved AX)
    db      66h                 ; Operand size prefix
    mov     ax, cx              ; MOV EAX, ECX
    db      66h
    shr     ax, 16              ; SHR EAX, 16 (now AX = ECX high)
    mov     [bp+pr_cx_high], ax

    ; Get EDX high 16 bits
    db      66h
    mov     ax, dx              ; MOV EAX, EDX
    db      66h
    shr     ax, 16              ; SHR EAX, 16 (now AX = EDX high)
    mov     [bp+pr_dx_high], ax

    ; Call C handler with pointer to structure
    push    ss
    push    bp                  ; Far pointer to structure
    call    pci_shim_handler_c
    add     sp, 4               ; Clean up parameters

    ; AX contains return status:
    ; AX = 0FFFFh: Not handled, chain to BIOS
    ; AX = 00xxh: Success (AH=0, AL=subfunction)
    ; AX = 8xxxh: Error (AH=error code)

    cmp     ax, 0FFFFh
    je      not_handled

    ; Function was handled by shim
    ; Copy any returned values back to registers

    ; For read operations (AL=08h-0Ah), update return registers
    mov     bx, [bp+pr_ax]
    and     bl, 0Fh             ; Get function code

    cmp     bl, 08h             ; Read Config Byte
    jne     check_read_word
    mov     cl, byte [bp+pr_cx_low]  ; Return byte in CL
    jmp     set_status

check_read_word:
    cmp     bl, 09h             ; Read Config Word
    jne     check_read_dword
    mov     cx, [bp+pr_cx_low]  ; Return word in CX
    jmp     set_status

check_read_dword:
    cmp     bl, 0Ah             ; Read Config Dword
    jne     set_status          ; Not a read, skip

    ; Return dword in ECX
    mov     cx, [bp+pr_cx_low]
    mov     bx, [bp+pr_cx_high]
    ; Set ECX high 16 bits
    db      66h
    shl     bx, 16              ; SHL EBX, 16
    db      66h
    or      cx, bx              ; OR ECX, EBX

set_status:
    ; AH contains PCI BIOS status code
    mov     ah, ah              ; Status already in AH from return

    ; Clean up structure
    add     sp, PCI_REGS_SIZE

    ; Now modify the FLAGS on stack to set/clear carry
    ; Stack: [ES][DS][BP][DI][SI][DX][CX][BX][AX][FLAGS][CS][IP]
    ; We need to reach FLAGS at [SP+18]

    push    bp
    mov     bp, sp
    test    ah, ah              ; Check if error (non-zero)
    jz      clear_carry

    ; Set carry flag for error
    or      word [bp+20], 0001h
    jmp     finish_return

clear_carry:
    ; Clear carry flag for success
    and     word [bp+20], 0FFFEh

finish_return:
    pop     bp

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

    iret                        ; Return from interrupt

not_handled:
    ; Clean up structure
    add     sp, PCI_REGS_SIZE

chain_to_bios:
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

    ; Tail-chain to original handler
    ; Use far jump to avoid recursion
    db      0EAh                ; Far jump opcode
_chain_offset: dw 0             ; Will be patched with actual offset
_chain_segment: dw 0            ; Will be patched with actual segment

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; void cdecl set_chain_vector(unsigned short segment, unsigned short offset);
;;
;; Set the chain vector for tail-chaining to original INT 1Ah
;; Called from C during initialization
;;
;; @param segment Original handler segment (stack [bp+4])
;; @param offset Original handler offset (stack [bp+6])
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _set_chain_vector
_set_chain_vector:
    push    bp
    mov     bp, sp

    mov     ax, [bp+4]          ; Get segment
    mov     [cs:_chain_segment], ax

    mov     ax, [bp+6]          ; Get offset
    mov     [cs:_chain_offset], ax

    pop     bp
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Helper function to get ECX high word
;; unsigned short cdecl get_ecx_high(void);
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _get_ecx_high
_get_ecx_high:
    db      66h
    mov     ax, cx              ; MOV EAX, ECX
    db      66h
    shr     ax, 16              ; SHR EAX, 16
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Helper function to set ECX high word
;; void cdecl set_ecx_high(unsigned short value);
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _set_ecx_high
_set_ecx_high:
    push    bp
    mov     bp, sp
    push    bx

    mov     bx, [bp+4]          ; Get value
    db      66h
    shl     bx, 16              ; SHL EBX, 16
    db      66h
    and     cx, 0FFFFh          ; Clear ECX high
    db      66h
    or      cx, bx              ; OR ECX, EBX

    pop     bx
    pop     bp
    ret

hot_end:

patch_table:
patch_table_end:

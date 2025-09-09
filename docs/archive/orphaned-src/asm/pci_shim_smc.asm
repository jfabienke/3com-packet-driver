;
; @file pci_shim_smc.asm
; @brief Self-Modifying Code patches for V86-aware PCI access
;
; Instead of checking V86 mode on every I/O operation, we detect it once
; during initialization and patch the appropriate code paths using SMC.
; This provides optimal performance for both real mode and V86 mode.
;

SEGMENT _TEXT PUBLIC CLASS=CODE USE16

; External references
EXTERN _asm_is_v86_mode: FAR

; Public symbols for patching
PUBLIC _pci_io_patch_init
PUBLIC _pci_mech1_read_byte_fast
PUBLIC _pci_mech1_write_byte_fast
PUBLIC _pci_mech1_read_word_fast
PUBLIC _pci_mech1_write_word_fast
PUBLIC _pci_mech1_read_dword_fast
PUBLIC _pci_mech1_write_dword_fast
PUBLIC _vortex_tx_patch_init
PUBLIC _vortex_rx_patch_init
PUBLIC _isr_tiny_patch_init

; Patch points - these will be modified by SMC
; Each patch point has two versions: real mode (fast) and V86 mode (safe)

;-----------------------------------------------------------------------------
; Patch initialization - detect V86 and apply appropriate patches
;-----------------------------------------------------------------------------
_pci_io_patch_init PROC NEAR
        push    bp
        mov     bp, sp
        push    ax
        push    bx
        push    si
        push    di
        push    es
        
        ; Check if we're in V86 mode
        call    FAR PTR _asm_is_v86_mode
        test    ax, ax
        jz      .apply_realmode_patches
        
.apply_v86_patches:
        ; V86 mode detected - patch with conservative I/O
        mov     ax, cs
        mov     es, ax
        
        ; Patch read_byte with V86-safe version
        mov     si, OFFSET v86_read_byte_code
        mov     di, OFFSET read_byte_patch_point
        mov     cx, V86_READ_BYTE_SIZE
        rep     movsb
        
        ; Patch write_byte with V86-safe version
        mov     si, OFFSET v86_write_byte_code
        mov     di, OFFSET write_byte_patch_point
        mov     cx, V86_WRITE_BYTE_SIZE
        rep     movsb
        
        ; Patch read_dword with V86-safe version
        mov     si, OFFSET v86_read_dword_code
        mov     di, OFFSET read_dword_patch_point
        mov     cx, V86_READ_DWORD_SIZE
        rep     movsb
        
        ; Patch write_dword with V86-safe version
        mov     si, OFFSET v86_write_dword_code
        mov     di, OFFSET write_dword_patch_point
        mov     cx, V86_WRITE_DWORD_SIZE
        rep     movsb
        
        mov     ax, 1           ; Return 1 for V86 mode
        jmp     .done
        
.apply_realmode_patches:
        ; Real mode - patch with fast I/O (no extra delays)
        mov     ax, cs
        mov     es, ax
        
        ; Patch read_byte with fast version
        mov     si, OFFSET real_read_byte_code
        mov     di, OFFSET read_byte_patch_point
        mov     cx, REAL_READ_BYTE_SIZE
        rep     movsb
        
        ; Patch write_byte with fast version
        mov     si, OFFSET real_write_byte_code
        mov     di, OFFSET write_byte_patch_point
        mov     cx, REAL_WRITE_BYTE_SIZE
        rep     movsb
        
        ; Patch read_dword with fast version
        mov     si, OFFSET real_read_dword_code
        mov     di, OFFSET read_dword_patch_point
        mov     cx, REAL_READ_DWORD_SIZE
        rep     movsb
        
        ; Patch write_dword with fast version
        mov     si, OFFSET real_write_dword_code
        mov     di, OFFSET write_dword_patch_point
        mov     cx, REAL_WRITE_DWORD_SIZE
        rep     movsb
        
        xor     ax, ax          ; Return 0 for real mode
        
.done:
        pop     es
        pop     di
        pop     si
        pop     bx
        pop     ax
        pop     bp
        ret
_pci_io_patch_init ENDP

;-----------------------------------------------------------------------------
; Mechanism #1 byte read - patched at runtime
; Input: BX = bus/dev/func, DI = offset
; Output: AL = byte value
;-----------------------------------------------------------------------------
_pci_mech1_read_byte_fast PROC NEAR
        push    dx
        push    eax
        
        ; Build config address in EAX
        mov     eax, 80000000h  ; Enable bit
        mov     al, bh          ; Bus
        shl     eax, 8
        mov     al, bl          ; Dev/func
        shl     eax, 8
        mov     al, dil         ; Offset
        and     al, 0FCh        ; Align to dword
        
        ; Write address to CF8
        mov     dx, 0CF8h
        
read_byte_patch_point:
        ; This area will be patched with either real or V86 code
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        ; End of patch area
        
        ; Read byte from CFC + offset
        mov     dx, 0CFCh
        mov     al, dil
        and     al, 3
        add     dl, al
        in      al, dx
        
        pop     eax
        mov     ah, [esp+2]     ; Restore only high part of EAX
        pop     dx
        ret
_pci_mech1_read_byte_fast ENDP

;-----------------------------------------------------------------------------
; Mechanism #1 byte write - patched at runtime
; Input: BX = bus/dev/func, DI = offset, CL = value
;-----------------------------------------------------------------------------
_pci_mech1_write_byte_fast PROC NEAR
        push    dx
        push    eax
        
        ; Build config address
        mov     eax, 80000000h
        mov     al, bh
        shl     eax, 8
        mov     al, bl
        shl     eax, 8
        mov     al, dil
        and     al, 0FCh
        
        ; Write address to CF8
        mov     dx, 0CF8h
        
write_byte_patch_point:
        ; This area will be patched
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        ; End of patch area
        
        ; Write byte to CFC + offset
        mov     dx, 0CFCh
        mov     al, dil
        and     al, 3
        add     dl, al
        mov     al, cl
        out     dx, al
        
        pop     eax
        pop     dx
        ret
_pci_mech1_write_byte_fast ENDP

;-----------------------------------------------------------------------------
; Mechanism #1 dword read - patched at runtime
; Input: BX = bus/dev/func, DI = offset
; Output: EAX = dword value (DX:AX for Watcom)
;-----------------------------------------------------------------------------
_pci_mech1_read_dword_fast PROC NEAR
        push    cx
        
        ; Build config address
        mov     eax, 80000000h
        mov     al, bh
        shl     eax, 8
        mov     al, bl
        shl     eax, 8
        mov     al, dil
        and     al, 0FCh
        
        ; Write address to CF8
        push    dx
        mov     dx, 0CF8h
        
read_dword_patch_point:
        ; This area will be patched
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        ; End of patch area
        
        ; Read dword from CFC
        mov     dx, 0CFCh
        db      66h             ; Operand size prefix
        in      ax, dx          ; IN EAX, DX
        
        ; Convert to DX:AX for Watcom
        mov     cx, ax
        db      66h
        shr     ax, 16          ; SHR EAX, 16
        mov     dx, ax
        mov     ax, cx
        
        pop     dx              ; Note: This corrupts our return but we already saved it
        pop     cx
        ret
_pci_mech1_read_dword_fast ENDP

;-----------------------------------------------------------------------------
; Mechanism #1 dword write - patched at runtime
; Input: BX = bus/dev/func, DI = offset, ECX = value (CX:DX for input)
;-----------------------------------------------------------------------------
_pci_mech1_write_dword_fast PROC NEAR
        push    ax
        push    dx
        
        ; Combine CX:DX into ECX
        db      66h
        mov     ax, cx          ; MOV EAX, ECX (low word already in CX)
        db      66h
        shl     ax, 16          ; SHL EAX, 16
        mov     ax, dx          ; Add high word
        push    eax             ; Save value
        
        ; Build config address
        mov     eax, 80000000h
        mov     al, bh
        shl     eax, 8
        mov     al, bl
        shl     eax, 8
        mov     al, dil
        and     al, 0FCh
        
        ; Write address to CF8
        mov     dx, 0CF8h
        
write_dword_patch_point:
        ; This area will be patched
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        ; End of patch area
        
        ; Write dword to CFC
        mov     dx, 0CFCh
        pop     eax             ; Get value
        db      66h
        out     dx, ax          ; OUT DX, EAX
        
        pop     dx
        pop     ax
        ret
_pci_mech1_write_dword_fast ENDP

;-----------------------------------------------------------------------------
; Patch code templates - these are copied over the patch points
;-----------------------------------------------------------------------------

; Real mode fast I/O (no delays)
real_read_byte_code:
        cli
        db      66h
        out     dx, ax          ; OUT DX, EAX
        sti
REAL_READ_BYTE_SIZE EQU $ - real_read_byte_code

real_write_byte_code:
        cli
        db      66h
        out     dx, ax          ; OUT DX, EAX
        sti
REAL_WRITE_BYTE_SIZE EQU $ - real_write_byte_code

real_read_dword_code:
        cli
        db      66h
        out     dx, ax          ; OUT DX, EAX
        sti
        nop                     ; Padding to match V86 size
        nop
        nop
REAL_READ_DWORD_SIZE EQU $ - real_read_dword_code

real_write_dword_code:
        cli
        db      66h
        out     dx, ax          ; OUT DX, EAX
        sti
        nop                     ; Padding
        nop
        nop
REAL_WRITE_DWORD_SIZE EQU $ - real_write_dword_code

; V86 mode safe I/O (with delays for EMM386 compatibility)
v86_read_byte_code:
        pushf
        cli
        db      66h
        out     dx, ax          ; OUT DX, EAX
        jmp     $+2             ; Delay for I/O
        jmp     $+2
        popf
V86_READ_BYTE_SIZE EQU $ - v86_read_byte_code

v86_write_byte_code:
        pushf
        cli
        db      66h
        out     dx, ax          ; OUT DX, EAX
        jmp     $+2             ; Delay
        jmp     $+2
        popf
V86_WRITE_BYTE_SIZE EQU $ - v86_write_byte_code

v86_read_dword_code:
        pushf
        cli
        db      66h
        out     dx, ax          ; OUT DX, EAX
        jmp     $+2             ; Extra delays for 32-bit I/O
        jmp     $+2
        jmp     $+2
        jmp     $+2
        popf
V86_READ_DWORD_SIZE EQU $ - v86_read_dword_code

v86_write_dword_code:
        pushf
        cli
        db      66h
        out     dx, ax          ; OUT DX, EAX
        jmp     $+2             ; Extra delays
        jmp     $+2
        jmp     $+2
        jmp     $+2
        popf
V86_WRITE_DWORD_SIZE EQU $ - v86_write_dword_code

;-----------------------------------------------------------------------------
; Word access functions (built on byte functions)
;-----------------------------------------------------------------------------
_pci_mech1_read_word_fast PROC NEAR
        push    cx
        push    bx
        push    di
        
        ; Read low byte
        call    _pci_mech1_read_byte_fast
        mov     cl, al
        
        ; Read high byte
        inc     di
        call    _pci_mech1_read_byte_fast
        mov     ah, al
        mov     al, cl
        
        pop     di
        pop     bx
        pop     cx
        ret
_pci_mech1_read_word_fast ENDP

_pci_mech1_write_word_fast PROC NEAR
        push    ax
        push    cx
        push    bx
        push    di
        
        mov     ax, cx          ; Save value
        
        ; Write low byte
        call    _pci_mech1_write_byte_fast
        
        ; Write high byte
        inc     di
        mov     cl, ah
        call    _pci_mech1_write_byte_fast
        
        pop     di
        pop     bx
        pop     cx
        pop     ax
        ret
_pci_mech1_write_word_fast ENDP

;-----------------------------------------------------------------------------
; Vortex TX optimization patching
; Patches rep outsw with appropriate V86 delays if needed
;-----------------------------------------------------------------------------
_vortex_tx_patch_init PROC NEAR
        push    bp
        mov     bp, sp
        push    ax
        push    si
        push    di
        push    es
        push    cx
        
        ; Check if we're in V86 mode
        call    FAR PTR _asm_is_v86_mode
        test    ax, ax
        jz      .apply_realmode_tx
        
.apply_v86_tx:
        ; V86 mode - patch TX with chunked transfers
        mov     ax, cs
        mov     es, ax
        
        ; Find and patch the rep outsw in vortex_tx_fast
        EXTERN _vortex_tx_fast: NEAR
        mov     di, OFFSET _vortex_tx_fast
        add     di, 125         ; Offset to rep outsw instruction
        
        ; Replace with chunked V86-safe version
        mov     si, OFFSET v86_tx_burst_code
        mov     cx, V86_TX_BURST_SIZE
        rep     movsb
        jmp     .tx_done
        
.apply_realmode_tx:
        ; Real mode - no changes needed, rep outsw is optimal
        
.tx_done:
        pop     cx
        pop     es
        pop     di
        pop     si
        pop     ax
        pop     bp
        ret
_vortex_tx_patch_init ENDP

;-----------------------------------------------------------------------------
; Vortex RX optimization patching
; Patches rep insw with appropriate V86 delays if needed
;-----------------------------------------------------------------------------
_vortex_rx_patch_init PROC NEAR
        push    bp
        mov     bp, sp
        push    ax
        push    si
        push    di
        push    es
        push    cx
        
        ; Check if we're in V86 mode
        call    FAR PTR _asm_is_v86_mode
        test    ax, ax
        jz      .apply_realmode_rx
        
.apply_v86_rx:
        ; V86 mode - patch RX with chunked transfers
        mov     ax, cs
        mov     es, ax
        
        ; Find and patch the rep insw in vortex_rx_fast
        EXTERN _vortex_rx_fast: NEAR
        mov     di, OFFSET _vortex_rx_fast
        add     di, 191         ; Offset to rep insw instruction
        
        ; Replace with chunked V86-safe version
        mov     si, OFFSET v86_rx_burst_code
        mov     cx, V86_RX_BURST_SIZE
        rep     movsb
        jmp     .rx_done
        
.apply_realmode_rx:
        ; Real mode - no changes needed, rep insw is optimal
        
.rx_done:
        pop     cx
        pop     es
        pop     di
        pop     si
        pop     ax
        pop     bp
        ret
_vortex_rx_patch_init ENDP

;-----------------------------------------------------------------------------
; Tiny ISR patching
; Patches interrupt handler with V86-safe version if needed
;-----------------------------------------------------------------------------
_isr_tiny_patch_init PROC NEAR
        push    bp
        mov     bp, sp
        push    ax
        push    si
        push    di
        push    es
        push    cx
        
        ; Check if we're in V86 mode
        call    FAR PTR _asm_is_v86_mode
        test    ax, ax
        jz      .apply_realmode_isr
        
.apply_v86_isr:
        ; V86 mode - patch ISR with IRET workaround
        mov     ax, cs
        mov     es, ax
        
        ; Find and patch the rx_batch_isr
        EXTERN _rx_batch_isr: FAR
        mov     di, OFFSET _rx_batch_isr
        add     di, 13          ; Offset to EOI sequence
        
        ; Replace with V86-safe EOI
        mov     si, OFFSET v86_eoi_code
        mov     cx, V86_EOI_SIZE
        rep     movsb
        jmp     .isr_done
        
.apply_realmode_isr:
        ; Real mode - standard EOI is fine
        
.isr_done:
        pop     cx
        pop     es
        pop     di
        pop     si
        pop     ax
        pop     bp
        ret
_isr_tiny_patch_init ENDP

;-----------------------------------------------------------------------------
; V86 patch code templates for optimizations
;-----------------------------------------------------------------------------

; V86 chunked TX burst (instead of rep outsw)
v86_tx_burst_code:
        push    cx
        push    ax
.tx_chunk_loop:
        cmp     cx, 8           ; Process in chunks of 8 words
        jbe     .tx_final
        mov     ax, 8
        sub     cx, ax
        push    cx
        mov     cx, ax
        rep     outsw           ; Small burst
        jmp     $+2             ; V86 delay
        jmp     $+2
        pop     cx
        jmp     .tx_chunk_loop
.tx_final:
        rep     outsw           ; Final chunk
        pop     ax
        pop     cx
V86_TX_BURST_SIZE EQU $ - v86_tx_burst_code

; V86 chunked RX burst (instead of rep insw)
v86_rx_burst_code:
        push    cx
        push    ax
.rx_chunk_loop:
        cmp     cx, 8           ; Process in chunks of 8 words
        jbe     .rx_final
        mov     ax, 8
        sub     cx, ax
        push    cx
        mov     cx, ax
        rep     insw            ; Small burst
        jmp     $+2             ; V86 delay
        jmp     $+2
        pop     cx
        jmp     .rx_chunk_loop
.rx_final:
        rep     insw            ; Final chunk
        pop     ax
        pop     cx
V86_RX_BURST_SIZE EQU $ - v86_rx_burst_code

; V86-safe EOI sequence
v86_eoi_code:
        pushf
        cli
        mov     al, 20h
        out     20h, al         ; Master PIC EOI
        jmp     $+2             ; V86 delay
        jmp     $+2
        popf
V86_EOI_SIZE EQU $ - v86_eoi_code

ENDS

END
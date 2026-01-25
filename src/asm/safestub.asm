;
; @file safety_stubs.asm
; @brief Minimal safety stubs for SMC patching (TSR resident)
;
; These tiny stubs are patched into the hot path based on runtime detection.
; All stubs preserve registers according to C calling convention and are
; 286-compatible unless specifically gated for 386+.
;
; GPT-5 validated: Realistic TSR overhead with proper register preservation
;
; Converted to NASM syntax: 2026-01-23

bits 16
cpu 286                             ; Base compatibility level

; External data references
extern _vds_pool                    ; VDS buffer pool
extern _bounce_pool                 ; Bounce buffer pool
extern _cpu_type                    ; CPU type for gating
extern _nic_io_base                 ; NIC I/O base address
extern _saved_int_mask              ; Saved interrupt mask
extern _mask_method                 ; How interrupts were masked

; Public exports
global vds_lock_stub
global vds_unlock_stub
global cache_flush_486
global bounce_tx_stub
global bounce_rx_stub
global check_64kb_stub
global pio_fallback_stub
global safe_disable_interrupts
global safe_enable_interrupts
global serialize_after_smc

; CPU type constants
CPU_286         equ 2
CPU_386         equ 3
CPU_486         equ 4

; NIC interrupt mask register offset
INT_MASK_REG    equ 0Eh

segment _TEXT class=CODE

;-----------------------------------------------------------------------------
; VDS lock stub - Lock buffer for DMA in V86 mode
; Preserves: All registers
; Size: ~45 bytes
;-----------------------------------------------------------------------------
vds_lock_stub:
        push    ax
        push    bx
        push    cx
        push    dx
        push    es
        push    di

        ; Get next available VDS buffer from pool
        call    get_vds_buffer      ; Returns buffer index in AX
        test    ax, ax
        js      .no_buffer          ; Negative = no buffer available

        ; Buffer already locked at init, just mark as in-use
        mov     bx, ax
        shl     bx, 4               ; Multiply by struct size (16)
        mov     byte [_vds_pool + bx + 14], 1  ; Set in_use flag

        clc                         ; Success
        jmp     .done

.no_buffer:
        stc                         ; Failure

.done:
        pop     di
        pop     es
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

;-----------------------------------------------------------------------------
; VDS unlock stub - Unlock buffer after DMA complete
; Preserves: All registers
; Size: ~35 bytes
;-----------------------------------------------------------------------------
vds_unlock_stub:
        push    ax
        push    bx

        ; Find and release VDS buffer
        call    release_vds_buffer

        pop     bx
        pop     ax
        ret

;-----------------------------------------------------------------------------
; Cache flush for 486+ - WBINVD instruction
; Only called on 486+ in real mode (gated by detection)
; Preserves: All registers
; Size: 5 bytes
;-----------------------------------------------------------------------------
cache_flush_486:
        cpu 486                     ; Enable 486 instructions
        wbinvd                      ; Write-back and invalidate cache
        cpu 286                     ; Back to 286 mode
        ret

;-----------------------------------------------------------------------------
; Bounce buffer TX stub - Copy data to bounce buffer for TX
; Preserves: All registers except AX (returns bounce address)
; Size: ~55 bytes
;-----------------------------------------------------------------------------
bounce_tx_stub:
        push    cx
        push    si
        push    di
        push    es
        push    ds
        pushf

        cld                         ; Clear direction flag

        ; Get bounce buffer
        call    get_bounce_buffer   ; Returns segment in AX
        mov     es, ax              ; ES = bounce buffer segment
        xor     di, di              ; ES:DI = destination

        ; DS:SI already points to source data
        mov     cx, 768             ; 1536 bytes / 2
        rep     movsw               ; Copy data

        ; Return bounce buffer physical address in DX:AX
        mov     ax, es
        xor     dx, dx
        shl     ax, 4               ; Convert segment to physical
        adc     dx, 0               ; Handle overflow

        popf
        pop     ds
        pop     es
        pop     di
        pop     si
        pop     cx
        ret

;-----------------------------------------------------------------------------
; Bounce buffer RX stub - Copy data from bounce buffer after RX
; Preserves: All registers
; Size: ~55 bytes
;-----------------------------------------------------------------------------
bounce_rx_stub:
        push    ax
        push    cx
        push    si
        push    di
        push    es
        push    ds
        pushf

        cld                         ; Clear direction flag

        ; Get bounce buffer that was used for RX
        call    get_rx_bounce_buffer ; Returns segment in AX
        push    ds
        mov     ds, ax              ; DS = bounce buffer segment
        xor     si, si              ; DS:SI = source

        ; ES:DI already points to destination
        mov     cx, 768             ; 1536 bytes / 2
        rep     movsw               ; Copy data

        pop     ds

        ; Release bounce buffer
        call    release_bounce_buffer

        popf
        pop     ds
        pop     es
        pop     di
        pop     si
        pop     cx
        pop     ax
        ret

;-----------------------------------------------------------------------------
; 64KB boundary check stub - Pure detector for DMA boundary crossing
;
; This is a simplified implementation per SAFESTUB_COMPLETION.md:
; - Pure detector: returns CF=1 if boundary crossed, CF=0 if safe
; - Caller handles fallback (typically pio_fallback_stub)
; - No transparent bounce buffer in ISR hot path to keep it simple
;
; Input:  DX:AX = physical address (24-bit for ISA)
;         CX = transfer length in bytes
; Output: CF = 0 if DMA is safe (no boundary crossing)
;         CF = 1 if 64KB boundary crossed (force PIO fallback)
; Preserves: AX, BX, CX, DX
; Size: ~20 bytes
;-----------------------------------------------------------------------------
check_64kb_stub:
        push    ax
        push    bx

        ; Check if buffer + length crosses 64KB boundary
        ; 64KB boundary is when low 16 bits wrap around (carry out of AX)
        mov     bx, ax              ; Save start low word
        add     ax, cx              ; Add length to low word
        jc      .crossed            ; Carry = crossed 64KB boundary

        ; Additional check: did we wrap from high address to low?
        ; If (start + len) < start, we wrapped
        cmp     ax, bx
        jb      .crossed            ; Wrapped around = crossed

        ; No crossing - DMA is safe
        pop     bx
        pop     ax
        clc                         ; CF=0: safe for DMA
        ret

.crossed:
        ; Boundary crossed - caller should use PIO fallback
        pop     bx
        pop     ax
        stc                         ; CF=1: force PIO fallback
        ret

;-----------------------------------------------------------------------------
; PIO fallback stub - Redirect to PIO when DMA is disabled
; Preserves: All registers
; Size: ~20 bytes
;-----------------------------------------------------------------------------
pio_fallback_stub:
        push    ax
        push    dx

        ; Redirect to PIO implementation
        call    pio_transfer        ; Implement PIO transfer

        pop     dx
        pop     ax
        ret

;-----------------------------------------------------------------------------
; Safe interrupt disable - Handles V86/IOPL correctly
; Size: ~60 bytes (286-compatible with 386+ enhancements)
;-----------------------------------------------------------------------------
safe_disable_interrupts:
        push    ax
        push    dx

        ; Check CPU type
        cmp     byte [_cpu_type], CPU_386
        jb      .use_cli            ; 286 or below - simple CLI

        ; 386+ - Need to check V86 mode
        cpu 386
        pushfd
        pop     eax
        test    eax, 00020000h      ; Check VM bit (17)
        jz      .use_cli_386        ; Not in V86

        ; Check IOPL
        mov     edx, eax
        shr     edx, 12
        and     edx, 3              ; Extract IOPL bits
        cmp     dl, 3
        je      .use_cli_386        ; IOPL=3, can use CLI

        ; V86 with IOPL<3 - mask at device
        cpu 286
        mov     dx, [_nic_io_base]
        add     dx, INT_MASK_REG
        in      al, dx
        mov     [_saved_int_mask], al
        or      al, 0FFh            ; Mask all interrupts
        out     dx, al
        mov     byte [_mask_method], 1
        jmp     .done

.use_cli_386:
        cpu 286
.use_cli:
        cli
        mov     byte [_mask_method], 0

.done:
        pop     dx
        pop     ax
        ret

;-----------------------------------------------------------------------------
; Safe interrupt enable - Restores previous state
; Size: ~35 bytes
;-----------------------------------------------------------------------------
safe_enable_interrupts:
        push    ax
        push    dx

        cmp     byte [_mask_method], 0
        je      .use_sti

        ; Restore device mask
        mov     dx, [_nic_io_base]
        add     dx, INT_MASK_REG
        mov     al, [_saved_int_mask]
        out     dx, al
        jmp     .done

.use_sti:
        sti

.done:
        pop     dx
        pop     ax
        ret

;-----------------------------------------------------------------------------
; Serialize after SMC - Proper serialization for 286-Pentium
; Size: ~45 bytes
;-----------------------------------------------------------------------------
serialize_after_smc:
        ; Flush prefetch queue
        jmp     short $+2

        ; Check CPU type for serialization method
        cmp     byte [_cpu_type], CPU_486
        jb      .use_far_ret        ; 286/386 - use far return

        ; 486+ - Check for CPUID
        call    check_cpuid_available
        test    ax, ax
        jz      .use_far_ret

        ; CPUID available - use it for serialization
        cpu 486
        xor     eax, eax
        cpuid
        cpu 286
        ret

.use_far_ret:
        ; Far return to serialize on 286/386/486-without-CPUID
        push    cs
        push    word .serialized
        retf
.serialized:
        ret

;-----------------------------------------------------------------------------
; Check CPUID availability (286-safe)
; Returns: AX = 1 if CPUID available, 0 if not
; Size: ~50 bytes
;-----------------------------------------------------------------------------
check_cpuid_available:
        ; First check if we're on 386+
        cmp     byte [_cpu_type], CPU_386
        jb      .no_cpuid           ; 286 - no CPUID

        ; 386+ - Check EFLAGS.ID bit
        cpu 386
        pushfd
        pop     eax
        mov     ecx, eax            ; Save original

        xor     eax, 00200000h      ; Toggle ID bit (21)
        push    eax
        popfd                       ; Try to set it

        pushfd
        pop     eax                 ; Read back

        push    ecx
        popfd                       ; Restore original

        xor     eax, ecx            ; Check if bit toggled
        and     eax, 00200000h
        jz      .no_cpuid_386

        mov     ax, 1               ; CPUID available
        jmp     short .done_386

.no_cpuid_386:
        xor     ax, ax              ; No CPUID

.done_386:
        cpu 286
        ret

.no_cpuid:
        xor     ax, ax
        ret

;-----------------------------------------------------------------------------
; Helper: Get available VDS buffer
; Returns: AX = buffer index, or -1 if none available
;-----------------------------------------------------------------------------
get_vds_buffer:
        push    bx
        push    cx

        xor     ax, ax
        mov     cx, 32              ; VDS_POOL_SIZE

.search:
        mov     bx, ax
        shl     bx, 4               ; Multiply by struct size
        cmp     byte [_vds_pool + bx + 14], 0  ; Check in_use flag
        je      .found

        inc     ax
        loop    .search

        mov     ax, -1              ; No buffer available
        jmp     short .done

.found:
        ; AX contains buffer index

.done:
        pop     cx
        pop     bx
        ret

;-----------------------------------------------------------------------------
; Helper: Release VDS buffer
;
; Releases a VDS buffer back to the pool by matching physical address
; and clearing the in_use flag.
;
; Input:  DX:AX = physical address of buffer to release
; Output: CF = 0 if buffer found and released
;         CF = 1 if buffer not found in pool
; Preserves: All registers except flags
;
; VDS pool entry structure (16 bytes per entry):
;   offset 0:  physical_addr (4 bytes)
;   offset 14: flags         (1 byte) - Bit 0: in_use
;-----------------------------------------------------------------------------
release_vds_buffer:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si

        ; Search through VDS pool for matching physical address
        xor     cx, cx              ; Entry counter
        mov     si, _vds_pool       ; SI = pool base address

.search_loop:
        cmp     cx, 32              ; VDS_POOL_SIZE = 32
        jae     .not_found

        ; Check if this entry matches our physical address
        ; Compare low word first (offset 0)
        cmp     ax, [si]
        jne     .next_entry

        ; Compare high word (offset 2)
        cmp     dx, [si+2]
        jne     .next_entry

        ; Match found - clear in_use flag (offset 14)
        and     byte [si+14], 0FEh  ; Clear bit 0 (in_use)

        ; Success
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        clc                         ; CF=0: success
        ret

.next_entry:
        add     si, 16              ; Move to next entry (16 bytes per struct)
        inc     cx
        jmp     .search_loop

.not_found:
        ; Buffer not found in pool - this is an error condition
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        stc                         ; CF=1: not found
        ret

;-----------------------------------------------------------------------------
; Helper: Get bounce buffer
; Returns: AX = segment of bounce buffer
;-----------------------------------------------------------------------------
get_bounce_buffer:
        ; For now, return first bounce buffer
        ; TODO: Implement proper pool management
        mov     ax, _bounce_pool
        shr     ax, 4               ; Convert to segment (assuming aligned)
        ret

;-----------------------------------------------------------------------------
; Helper: Get RX bounce buffer
; Returns: AX = segment of bounce buffer used for RX
;-----------------------------------------------------------------------------
get_rx_bounce_buffer:
        ; For now, return first bounce buffer
        ; TODO: Track which buffer was used for RX
        mov     ax, _bounce_pool
        shr     ax, 4               ; Convert to segment (assuming aligned)
        ret

;-----------------------------------------------------------------------------
; Helper: Release bounce buffer
;-----------------------------------------------------------------------------
release_bounce_buffer:
        ; TODO: Implement proper pool management
        ret

;-----------------------------------------------------------------------------
; Helper: Use bounce buffer for 64KB crossing
;-----------------------------------------------------------------------------
use_bounce_for_64kb:
        ; TODO: Switch to bounce buffer
        ret

;-----------------------------------------------------------------------------
; Helper: PIO transfer implementation
; Uses pre-set I/O handler from dispatch table for CPU-adaptive transfers.
;
; Input:  ES:DI = destination buffer
;         CX = word count
;         [_nic_io_base] = NIC I/O base address
; Output: ES:DI advanced by bytes transferred
; Uses:   AX, CX, DX, DI
;-----------------------------------------------------------------------------
pio_transfer:
        push    ax
        push    dx

        ; Get NIC I/O base address
        mov     dx, [_nic_io_base]
        ; Note: For 3C509B/3C515, RX FIFO is at base+0 (offset 0)

        ; Clear direction flag for forward string ops
        cld

        ; Call pre-set I/O handler from dispatch table
        ; The handler expects: ES:DI = dest, DX = port, CX = word count
        ; insw_handler is set by init_io_dispatch() in nicirq.asm
        extern  insw_handler
        call    [insw_handler]

        pop     dx
        pop     ax
        ret

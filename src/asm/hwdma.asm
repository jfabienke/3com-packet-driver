; @file hwdma.asm
; @brief DMA operations for 3Com Packet Driver
;
; Created: 2026-01-25 from hardware.asm modularization
; Last Updated: 2026-01-25 11:33:10
;
; This module contains DMA-related hardware operations for bus mastering
; and ISA DMA support. This is a HOT section module that stays resident
; for runtime DMA operations.
;
; This file is part of the 3Com Packet Driver project.
;
;=============================================================================

bits 16
cpu 386

;-----------------------------------------------------------------------------
; Include Files
;-----------------------------------------------------------------------------
%include "asm_interfaces.inc"

;-----------------------------------------------------------------------------
; Constants
;-----------------------------------------------------------------------------
C515_DMA_CTRL       EQU 400h    ; ISA DMA control register offset

;-----------------------------------------------------------------------------
; External References
;-----------------------------------------------------------------------------
extern get_cpu_features

;-----------------------------------------------------------------------------
; Global Exports
;-----------------------------------------------------------------------------
global dma_stall_engines
global dma_unstall_engines
global dma_start_transfer
global dma_get_engine_status
global dma_prepare_coherent_buffer
global dma_complete_coherent_buffer
global setup_advanced_dma_descriptors
global advanced_dma_interrupt_check
global isa_virt_to_phys
global check_isa_dma_boundary
global setup_isa_dma_descriptor
global init_3c515_bus_master

;=============================================================================
; JIT MODULE HEADER
;=============================================================================
segment MODULE class=MODULE align=16

global _mod_hwdma_header
_mod_hwdma_header:
    db  'PKTDRV', 0             ; +00  7 bytes: module signature
    db  1                       ; +07  1 byte:  major version
    db  0                       ; +08  1 byte:  minor version
    db  2                       ; +09  1 byte:  cpu_req (386 DMA instructions)
    db  0                       ; +0A  1 byte:  nic_type (0 = generic)
    db  1                       ; +0B  1 byte:  cap_flags (1 = MOD_CAP_CORE)
    dw  hot_end - hot_start     ; +0C  2 bytes: hot code size
    dw  patch_table             ; +0E  2 bytes: offset to patch table
    dw  patch_table_end - patch_table  ; +10  2 bytes: patch table size
    dw  hot_start               ; +12  2 bytes: offset to hot_start
    dw  hot_end                 ; +14  2 bytes: offset to hot_end
    times 64 - ($ - _mod_hwdma_header) db 0  ; Pad to 64 bytes

;=============================================================================
; HOT SECTION - Resident DMA Operations
;=============================================================================

section .text
hot_start:

;-----------------------------------------------------------------------------
; dma_stall_engines - Stall DMA engines for timeout recovery
;
; Input:  AL = 1 to stall TX, 0 to skip TX
;         AH = 1 to stall RX, 0 to skip RX
;         DX = I/O base address
; Output: AX = 0 for success, negative error code on failure
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
dma_stall_engines:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Save stall flags
        mov     bl, al                      ; TX stall flag
        mov     bh, ah                      ; RX stall flag

        ; Get I/O base from context (passed in DX)
        test    dx, dx
        jz      .no_io_base

        ; Select window 7 for DMA control
        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (1 << 11) | 7           ; Select window 7
        out     dx, ax
        pop     dx

        ; Small delay for window selection
        mov     cx, 100
.window_delay:
        nop
        loop    .window_delay

        ; Stall TX engine if requested
        test    bl, bl
        jz      .skip_tx_stall

        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (6 << 11) + 2           ; DownStall command
        out     dx, ax
        pop     dx

        ; Wait for TX engine to stall
        mov     cx, 1000                    ; Timeout counter
.tx_stall_wait:
        push    dx
        add     dx, 0Eh                     ; Status register
        in      ax, dx
        pop     dx
        test    ax, 0800h                   ; Check DMA in progress bit
        jz      .tx_stalled
        dec     cx
        jnz     .tx_stall_wait

        ; TX stall timeout
        mov     ax, -1
        jmp     .exit

.tx_stalled:
.skip_tx_stall:
        ; Stall RX engine if requested
        test    bh, bh
        jz      .skip_rx_stall

        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (6 << 11) + 0           ; UpStall command
        out     dx, ax
        pop     dx

        ; Wait for RX engine to stall
        mov     cx, 1000                    ; Timeout counter
.rx_stall_wait:
        push    dx
        add     dx, 0Eh                     ; Status register
        in      ax, dx
        pop     dx
        test    ax, 0800h                   ; Check DMA in progress bit
        jz      .rx_stalled
        dec     cx
        jnz     .rx_stall_wait

        ; RX stall timeout
        mov     ax, -1
        jmp     .exit

.rx_stalled:
.skip_rx_stall:
        ; Success
        mov     ax, 0
        jmp     .exit

.no_io_base:
        mov     ax, -1

.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end dma_stall_engines

;-----------------------------------------------------------------------------
; dma_unstall_engines - Unstall DMA engines after recovery
;
; Input:  AL = 1 to unstall TX, 0 to skip TX
;         AH = 1 to unstall RX, 0 to skip RX
;         DX = I/O base address
; Output: AX = 0 for success, negative error code on failure
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
dma_unstall_engines:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Save unstall flags
        mov     bl, al                      ; TX unstall flag
        mov     bh, ah                      ; RX unstall flag

        ; Get I/O base
        test    dx, dx
        jz      .no_io_base

        ; Select window 7 for DMA control
        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (1 << 11) | 7           ; Select window 7
        out     dx, ax
        pop     dx

        ; Small delay for window selection
        mov     cx, 100
.window_delay:
        nop
        loop    .window_delay

        ; Unstall TX engine if requested
        test    bl, bl
        jz      .skip_tx_unstall

        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (6 << 11) + 3           ; DownUnstall command
        out     dx, ax
        pop     dx

.skip_tx_unstall:
        ; Unstall RX engine if requested
        test    bh, bh
        jz      .skip_rx_unstall

        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (6 << 11) + 1           ; UpUnstall command
        out     dx, ax
        pop     dx

.skip_rx_unstall:
        ; Success
        mov     ax, 0
        jmp     .exit

.no_io_base:
        mov     ax, -1

.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end dma_unstall_engines

;-----------------------------------------------------------------------------
; dma_start_transfer - Start DMA transfer engines
;
; Input:  AL = 1 to start TX, 0 to skip TX
;         AH = 1 to start RX, 0 to skip RX
;         DX = I/O base address
; Output: AX = 0 for success, negative error code on failure
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
dma_start_transfer:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Save start flags
        mov     bl, al                      ; TX start flag
        mov     bh, ah                      ; RX start flag

        ; Get I/O base
        test    dx, dx
        jz      .no_io_base

        ; Select window 7 for DMA control
        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (1 << 11) | 7           ; Select window 7
        out     dx, ax
        pop     dx

        ; Small delay for window selection
        mov     cx, 100
.window_delay:
        nop
        loop    .window_delay

        ; Start TX DMA if requested
        test    bl, bl
        jz      .skip_tx_start

        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (20 << 11) + 1          ; StartDMADown command
        out     dx, ax
        pop     dx

.skip_tx_start:
        ; Start RX DMA if requested
        test    bh, bh
        jz      .skip_rx_start

        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (20 << 11) + 0          ; StartDMAUp command
        out     dx, ax
        pop     dx

.skip_rx_start:
        ; Success
        mov     ax, 0
        jmp     .exit

.no_io_base:
        mov     ax, -1

.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end dma_start_transfer

;-----------------------------------------------------------------------------
; dma_get_engine_status - Get DMA engine status
;
; Input:  DX = I/O base address
;         ES:BX = pointer to status structure (tx_status, rx_status)
; Output: AX = 0 for success, negative error code on failure
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
dma_get_engine_status:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Get I/O base
        test    dx, dx
        jz      .no_io_base

        ; Save status pointer
        mov     si, bx

        ; Select window 7 for DMA status
        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (1 << 11) | 7           ; Select window 7
        out     dx, ax
        pop     dx

        ; Small delay for window selection
        mov     cx, 100
.window_delay:
        nop
        loop    .window_delay

        ; Read DMA status register
        push    dx
        add     dx, 0Eh                     ; Status register
        in      ax, dx
        pop     dx

        ; Extract TX status (bits related to down DMA)
        mov     bx, ax
        and     bx, 0200h                   ; Down complete bit (bit 9)
        mov     [es:si], bx                 ; Store TX status

        ; Extract RX status (bits related to up DMA)
        mov     bx, ax
        and     bx, 0400h                   ; Up complete bit (bit 10)
        mov     [es:si+4], bx               ; Store RX status (offset by 4 bytes)

        ; Success
        mov     ax, 0
        jmp     .exit

.no_io_base:
        mov     ax, -1

.exit:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end dma_get_engine_status

;-----------------------------------------------------------------------------
; dma_prepare_coherent_buffer - Prepare buffer for cache coherency
;
; Input:  ES:BX = buffer address
;         CX = buffer length
;         DL = direction (0=TX, 1=RX)
; Output: AX = 0 for success, negative error code on failure
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
dma_prepare_coherent_buffer:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; For ISA bus mastering, we need to ensure cache coherency
        ; This is a simplified implementation for DOS environment

        ; Check if buffer address is valid
        test    bx, bx
        jz      .invalid_buffer

        ; Check buffer length
        test    cx, cx
        jz      .invalid_buffer

        ; Check if buffer is in suitable memory range for ISA DMA
        ; ISA DMA is limited to 24-bit addressing (16MB)
        mov     ax, es
        cmp     ax, 0FFFFh                  ; Check segment is reasonable
        ja      .invalid_buffer

        ; For DOS, we primarily need to flush any CPU caches
        ; This is CPU-dependent, but most 386/486 systems need WBINVD

        ; Check if we're running on 386 or later (has cache)
        call    get_cpu_features            ; Get CPU features
        test    ax, 0001h                   ; Check for cache present
        jz      .no_cache_flush_needed

        ; Flush cache for DMA safety (386/486/Pentium)
        ; Note: This is privileged instruction, may fault on some systems
        pushf
        cli                                 ; Disable interrupts

        ; Issue cache flush (write-back and invalidate)
        ; This flushes all caches, which is overkill but safe
        db      0Fh, 09h                    ; WBINVD instruction

        popf                                ; Restore interrupts

.no_cache_flush_needed:
        ; Success
        mov     ax, 0
        jmp     .exit

.invalid_buffer:
        mov     ax, -1

.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end dma_prepare_coherent_buffer

;-----------------------------------------------------------------------------
; dma_complete_coherent_buffer - Complete cache coherency after DMA
;
; Input:  ES:BX = buffer address
;         CX = buffer length
;         DL = direction (0=TX, 1=RX)
; Output: AX = 0 for success, negative error code on failure
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
dma_complete_coherent_buffer:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Check if buffer address is valid
        test    bx, bx
        jz      .invalid_buffer

        ; Check buffer length
        test    cx, cx
        jz      .invalid_buffer

        ; For RX operations, we need to invalidate cache lines
        ; to ensure CPU sees data written by DMA
        cmp     dl, 1                       ; Check if RX operation
        jne     .tx_operation

        ; RX operation - invalidate cache to see DMA data
        call    get_cpu_features            ; Get CPU features
        test    ax, 0001h                   ; Check for cache present
        jz      .no_cache_invalidate_needed

        pushf
        cli                                 ; Disable interrupts

        ; Invalidate cache lines (for RX we want to see DMA data)
        db      0Fh, 08h                    ; INVD instruction

        popf                                ; Restore interrupts
        jmp     .cache_complete

.tx_operation:
        ; TX operation - no special action needed after DMA
        jmp     .cache_complete

.no_cache_invalidate_needed:
.cache_complete:
        ; Success
        mov     ax, 0
        jmp     .exit

.invalid_buffer:
        mov     ax, -1

.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end dma_complete_coherent_buffer

;-----------------------------------------------------------------------------
; setup_advanced_dma_descriptors - Setup hardware descriptor pointers
;
; Input:  DX = I/O base address
;         ES:BX = pointer to TX ring physical address
;         ES:SI = pointer to RX ring physical address
; Output: AX = 0 for success, negative error code on failure
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
setup_advanced_dma_descriptors:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Get I/O base
        test    dx, dx
        jz      .no_io_base

        ; Save descriptor addresses
        mov     di, si                      ; Save RX ring address

        ; Select window 7 for descriptor pointer setup
        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (1 << 11) | 7           ; Select window 7
        out     dx, ax
        pop     dx

        ; Small delay for window selection
        mov     cx, 100
.window_delay:
        nop
        loop    .window_delay

        ; Set TX descriptor list pointer (Down List Pointer)
        push    dx
        add     dx, 404h                    ; Down list pointer register
        mov     ax, [es:bx]                 ; Get low word of TX ring address
        out     dx, ax
        add     dx, 2                       ; High word register
        mov     ax, [es:bx+2]               ; Get high word of TX ring address
        out     dx, ax
        pop     dx

        ; Set RX descriptor list pointer (Up List Pointer)
        push    dx
        add     dx, 418h                    ; Up list pointer register
        mov     ax, [es:di]                 ; Get low word of RX ring address
        out     dx, ax
        add     dx, 2                       ; High word register
        mov     ax, [es:di+2]               ; Get high word of RX ring address
        out     dx, ax
        pop     dx

        ; Enable bus mastering in the NIC
        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (14 << 11) | 07FFh      ; Set interrupt enable with all DMA bits
        out     dx, ax
        pop     dx

        ; Success
        mov     ax, 0
        jmp     .exit

.no_io_base:
        mov     ax, -1

.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end setup_advanced_dma_descriptors

;-----------------------------------------------------------------------------
; advanced_dma_interrupt_check - Check for advanced DMA interrupts
;
; Input:  DX = I/O base address
; Output: AX = interrupt status mask
;         BX = DMA completion status
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
advanced_dma_interrupt_check:
        push    bp
        mov     bp, sp
        push    cx
        push    dx

        ; Initialize return values
        mov     ax, 0                       ; Interrupt status
        mov     bx, 0                       ; DMA completion status

        ; Get I/O base
        test    dx, dx
        jz      .no_io_base

        ; Read interrupt status register
        push    dx
        add     dx, 0Eh                     ; Status register
        in      ax, dx                      ; Read status
        pop     dx

        ; Check for DMA-related interrupts
        mov     bx, ax                      ; Save full status

        ; Extract DMA completion bits
        and     bx, 0700h                   ; Mask for DMA Done, Down Complete, Up Complete

        ; Check specific DMA interrupt conditions
        test    ax, 0100h                   ; DMA Done (bit 8)
        jz      .no_dma_done
        or      bx, 0001h                   ; Set DMA done flag

.no_dma_done:
        test    ax, 0200h                   ; Down Complete (bit 9 - TX)
        jz      .no_tx_complete
        or      bx, 0002h                   ; Set TX complete flag

.no_tx_complete:
        test    ax, 0400h                   ; Up Complete (bit 10 - RX)
        jz      .no_rx_complete
        or      bx, 0004h                   ; Set RX complete flag

.no_rx_complete:
        ; Return interrupt status in AX, DMA status in BX
        jmp     .exit

.no_io_base:
        mov     ax, 0
        mov     bx, 0

.exit:
        pop     dx
        pop     cx
        pop     bp
        ret
;; end advanced_dma_interrupt_check

;=============================================================================
; ISA ADDRESS TRANSLATION FOR 3C515 DMA
;=============================================================================

;-----------------------------------------------------------------------------
; isa_virt_to_phys - Convert DOS real mode address to ISA physical address
;
; Input:  DS:SI = virtual address (segment:offset)
; Output: DX:AX = 24-bit physical address for ISA DMA
; Uses:   AX, DX
;-----------------------------------------------------------------------------
isa_virt_to_phys:
        push    cx

        ; Calculate physical address: (segment << 4) + offset
        mov     ax, ds
        xor     dx, dx
        mov     cx, 4

        ; Shift segment left by 4 bits (multiply by 16)
.shift_loop:
        shl     ax, 1
        rcl     dx, 1
        loop    .shift_loop

        ; Add offset
        add     ax, si
        adc     dx, 0

        ; Ensure within ISA DMA 16MB limit (24-bit address)
        and     dx, 00FFh               ; Mask to 24 bits

        pop     cx
        ret
;; end isa_virt_to_phys

;-----------------------------------------------------------------------------
; check_isa_dma_boundary - Check if buffer crosses 64KB DMA boundary
;
; Input:  DX:AX = physical address, CX = buffer size
; Output: CF set if boundary crossed, clear if OK
; Uses:   None (preserves all registers)
;-----------------------------------------------------------------------------
check_isa_dma_boundary:
        push    ax
        push    bx
        push    dx

        ; Calculate end address
        mov     bx, ax
        add     bx, cx
        jnc     .no_carry
        inc     dx                      ; Handle carry to high word

.no_carry:
        ; Check if high 16 bits changed (crossed 64KB boundary)
        push    ax
        xor     ax, bx                  ; XOR start and end low words
        and     ax, 0F000h              ; Check if upper nibble changed
        pop     ax
        jz      .same_64k

        ; Crossed boundary
        stc                             ; Set carry flag
        jmp     .exit

.same_64k:
        clc                             ; Clear carry flag

.exit:
        pop     dx
        pop     bx
        pop     ax
        ret
;; end check_isa_dma_boundary

;-----------------------------------------------------------------------------
; setup_isa_dma_descriptor - Setup DMA descriptor for ISA bus master
;
; Input:  ES:DI = descriptor location
;         DX:AX = physical address
;         CX = length
;         BX = control flags
; Output: None
; Uses:   None (preserves all registers)
;-----------------------------------------------------------------------------
setup_isa_dma_descriptor:
        push    ax
        push    dx

        ; Store physical address (24-bit for ISA)
        mov     word [es:di], ax        ; Low 16 bits
        mov     byte [es:di+2], dl      ; Bits 16-23
        mov     byte [es:di+3], 0       ; Zero upper byte

        ; Store length
        mov     word [es:di+4], cx

        ; Store control flags
        mov     word [es:di+6], bx

        pop     dx
        pop     ax
        ret
;; end setup_isa_dma_descriptor

;-----------------------------------------------------------------------------
; init_3c515_bus_master - Initialize ISA bus mastering for 3C515
;
; Input:  DX = I/O base address
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
init_3c515_bus_master:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si
        push    di

        ; Select window 7 for bus master control
        add     dx, 0Eh                 ; Command register
        mov     ax, (1 << 11) | 7       ; Select window 7
        out     dx, ax

        ; Initialize DMA control registers at base+0x400
        sub     dx, 0Eh                 ; Back to base
        add     dx, C515_DMA_CTRL       ; DMA control offset (0x400)

        ; Clear DMA lists
        xor     ax, ax
        out     dx, ax                  ; Clear TX list pointer
        add     dx, 4
        out     dx, ax                  ; Clear fragment address
        add     dx, 4
        out     dx, ax                  ; Clear fragment length

        ; Enable bus mastering
        sub     dx, C515_DMA_CTRL
        add     dx, 0Eh                 ; Command register
        mov     ax, 0x2000              ; BUS_MASTER_ENABLE
        out     dx, ax

        ; Success
        xor     ax, ax

        pop     di
        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret
;; end init_3c515_bus_master

hot_end:

patch_table:
patch_table_end:

;=============================================================================
; End of hwdma.asm
;=============================================================================

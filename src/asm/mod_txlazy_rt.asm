;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_txlazy_rt.asm
;; @brief TX interrupt coalescing JIT runtime module
;;
;; 16-bit NASM module implementing transmit interrupt coalescing for reduced
;; CPU overhead. Batches TX completions and defers interrupts until K_PKTS
;; threshold is reached or TX ring approaches high water mark.
;;
;; Last Updated: 2026-02-01 15:30:00 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16
CPU 8086

%include "patch_macros.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Constants
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
TX_RING_SIZE    equ 32
TX_RING_MASK    equ 31
K_PKTS          equ 8               ; Interrupt threshold
TX_HIGH_WATER   equ 24              ; Ring high water mark
STATE_SIZE      equ 51              ; Per-NIC state size in bytes
MAX_NICS        equ 8               ; Maximum supported NICs

CPU_REQ         equ 0               ; 8086 baseline
CAP_FLAGS       equ 0x0000          ; No special capabilities
PATCH_COUNT     equ 0               ; No patch points in this module

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module segment
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
section .text class=MODULE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 64-byte module header
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _mod_txlazy_rt_header
_mod_txlazy_rt_header:
header:
    db 'PKTDRV',0                   ; 7 bytes - module signature
    db 1, 0                         ; 2 bytes - version 1.0
    dw hot_start                    ; hot section start offset
    dw hot_end                      ; hot section end offset
    dw 0, 0                         ; cold_start, cold_end (unused)
    dw patch_table                  ; patch table offset
    dw PATCH_COUNT                  ; number of patch entries
    dw (hot_end - header)           ; module_size
    dw (hot_end - hot_start)        ; required_memory
    db CPU_REQ                      ; cpu_requirements (8086)
    db 0                            ; nic_type (0 = any)
    dw CAP_FLAGS                    ; capability flags
    times (64 - ($ - header)) db 0  ; pad header to 64 bytes

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Hot section - TX coalescing state and functions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hot_start:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Per-NIC state array (51 bytes per NIC, 8 NICs = 408 bytes)
;;
;; Layout per NIC:
;;   +0:  tx_head (word)            - Ring head index
;;   +2:  tx_tail (word)            - Ring tail index
;;   +4:  tx_count (word)           - Number of pending TX descriptors
;;   +6:  tx_since_irq (word)       - Packets transmitted since last interrupt
;;   +8:  threshold (word)          - Interrupt threshold (default K_PKTS)
;;   +10: high_water (word)         - Ring high water mark (default TX_HIGH_WATER)
;;   +12: enabled (byte)            - Coalescing enabled flag
;;   +13: total_tx (dword)          - Total packets transmitted
;;   +17: total_coalesced (dword)   - Total interrupts deferred
;;   +21: total_interrupts (dword)  - Total interrupts generated
;;   +25: ring_base (dword)         - Far pointer to descriptor ring (unused)
;;   +29: ring entries (22 bytes)   - Simplified descriptor tracking (11 x 2-byte)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
tx_states:
    times (STATE_SIZE * MAX_NICS) db 0

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; tx_lazy_should_interrupt_ - Determine if TX interrupt should fire
;;
;; Watcom far call convention:
;;   Input:  AX = nic_index (0-7)
;;   Output: AX = 1 if interrupt should fire, 0 if defer
;;   Clobbers: BX, CX, DX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global tx_lazy_should_interrupt_
tx_lazy_should_interrupt_:
    push    bx

    ;; Calculate state pointer: BX = tx_states + (AX * STATE_SIZE)
    mov     cx, STATE_SIZE
    mul     cx                      ; DX:AX = AX * STATE_SIZE
    mov     bx, ax                  ; BX = offset into tx_states
    add     bx, tx_states

    ;; Check if coalescing enabled (+12 offset)
    cmp     byte [bx + 12], 0
    je      .always_interrupt       ; If disabled, always interrupt

    ;; Check if tx_since_irq >= threshold
    mov     ax, [bx + 6]            ; AX = tx_since_irq
    cmp     ax, [bx + 8]            ; Compare with threshold
    jge     .should_interrupt       ; If >= threshold, interrupt

    ;; Defer interrupt
    xor     ax, ax                  ; Return 0
    pop     bx
    retf

.should_interrupt:
    mov     ax, 1                   ; Return 1
    pop     bx
    retf

.always_interrupt:
    mov     ax, 1                   ; Return 1
    pop     bx
    retf

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; tx_lazy_post_boomerang_ - Post-transmit handling for Boomerang NICs
;;
;; Watcom far call convention:
;;   Input:  AX = nic_index, DX = descriptor_addr
;;   Output: AX = 1 if ring full (stall), 0 otherwise
;;   Clobbers: BX, CX, DX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global tx_lazy_post_boomerang_
tx_lazy_post_boomerang_:
    push    bx
    push    si

    ;; Calculate state pointer: BX = tx_states + (AX * STATE_SIZE)
    mov     cx, STATE_SIZE
    mul     cx                      ; DX:AX = AX * STATE_SIZE
    mov     bx, ax
    add     bx, tx_states

    ;; Increment tx_count (+4 offset)
    inc     word [bx + 4]

    ;; Increment tx_since_irq (+6 offset)
    inc     word [bx + 6]

    ;; Increment total_tx (dword at +13)
    add     word [bx + 13], 1
    adc     word [bx + 15], 0

    ;; Advance tx_head: tx_head = (tx_head + 1) & TX_RING_MASK
    mov     ax, [bx + 0]            ; AX = tx_head
    inc     ax
    and     ax, TX_RING_MASK
    mov     [bx + 0], ax            ; Store new tx_head

    ;; Check if tx_count >= high_water
    mov     ax, [bx + 4]            ; AX = tx_count
    cmp     ax, [bx + 10]           ; Compare with high_water
    jge     .ring_full

    ;; Not full
    xor     ax, ax                  ; Return 0
    pop     si
    pop     bx
    retf

.ring_full:
    mov     ax, 1                   ; Return 1 (stall)
    pop     si
    pop     bx
    retf

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; tx_lazy_post_vortex_ - Post-transmit handling for Vortex NICs (PIO)
;;
;; Watcom far call convention:
;;   Input:  AX = nic_index
;;   Output: AX = 0 (always success for PIO)
;;   Clobbers: BX, CX, DX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global tx_lazy_post_vortex_
tx_lazy_post_vortex_:
    push    bx

    ;; Calculate state pointer: BX = tx_states + (AX * STATE_SIZE)
    mov     cx, STATE_SIZE
    mul     cx                      ; DX:AX = AX * STATE_SIZE
    mov     bx, ax
    add     bx, tx_states

    ;; Increment tx_count (+4 offset)
    inc     word [bx + 4]

    ;; Increment tx_since_irq (+6 offset)
    inc     word [bx + 6]

    ;; Increment total_tx (dword at +13)
    add     word [bx + 13], 1
    adc     word [bx + 15], 0

    ;; Always return 0 for PIO (no stalling)
    xor     ax, ax
    pop     bx
    retf

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; tx_lazy_reclaim_batch_ - Reclaim completed TX descriptors in batch
;;
;; Called from interrupt handler to reclaim all completed TXs.
;;
;; Watcom far call convention:
;;   Input:  AX = nic_index
;;   Output: AX = number of descriptors reclaimed
;;   Clobbers: BX, CX, DX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global tx_lazy_reclaim_batch_
tx_lazy_reclaim_batch_:
    push    bx
    push    si

    ;; Calculate state pointer: BX = tx_states + (AX * STATE_SIZE)
    mov     cx, STATE_SIZE
    mul     cx                      ; DX:AX = AX * STATE_SIZE
    mov     bx, ax
    add     bx, tx_states

    ;; Get number to reclaim (current tx_count)
    mov     ax, [bx + 4]            ; AX = tx_count
    mov     si, ax                  ; Save reclaim count in SI

    ;; Reset tx_count to 0
    mov     word [bx + 4], 0

    ;; Update tx_tail to tx_head (all reclaimed)
    mov     cx, [bx + 0]            ; CX = tx_head
    mov     [bx + 2], cx            ; tx_tail = tx_head

    ;; Reset tx_since_irq to 0
    mov     word [bx + 6], 0

    ;; Increment total_interrupts (dword at +21)
    add     word [bx + 21], 1
    adc     word [bx + 23], 0

    ;; Increment total_coalesced by (reclaimed - 1) if reclaimed > 1
    cmp     si, 1
    jle     .done_stats

    mov     ax, si
    dec     ax                      ; AX = reclaimed - 1
    add     word [bx + 17], ax      ; Add to total_coalesced
    adc     word [bx + 19], 0

.done_stats:
    mov     ax, si                  ; Return reclaim count
    pop     si
    pop     bx
    retf

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; tx_lazy_get_stats_ - Copy statistics to caller buffer
;;
;; Watcom far call convention:
;;   Input:  AX = nic_index, DX:BX = far pointer to output buffer (12 bytes)
;;   Output: None (void)
;;   Clobbers: AX, BX, CX, DX, SI, DI, ES
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global tx_lazy_get_stats_
tx_lazy_get_stats_:
    push    es
    push    di
    push    si
    push    bx

    ;; Save output buffer far pointer
    mov     es, dx                  ; ES:BX = output buffer
    mov     di, bx

    ;; Calculate state pointer: SI = tx_states + (AX * STATE_SIZE)
    mov     cx, STATE_SIZE
    mul     cx                      ; DX:AX = AX * STATE_SIZE
    mov     si, ax
    add     si, tx_states

    ;; Copy total_tx (dword at +13)
    mov     ax, [si + 13]
    mov     [es:di], ax
    mov     ax, [si + 15]
    mov     [es:di + 2], ax

    ;; Copy total_coalesced (dword at +17)
    mov     ax, [si + 17]
    mov     [es:di + 4], ax
    mov     ax, [si + 19]
    mov     [es:di + 6], ax

    ;; Copy total_interrupts (dword at +21)
    mov     ax, [si + 21]
    mov     [es:di + 8], ax
    mov     ax, [si + 23]
    mov     [es:di + 10], ax

    pop     bx
    pop     si
    pop     di
    pop     es
    retf

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; tx_lazy_reset_stats_ - Zero statistics counters
;;
;; Watcom far call convention:
;;   Input:  AX = nic_index
;;   Output: None (void)
;;   Clobbers: AX, BX, CX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global tx_lazy_reset_stats_
tx_lazy_reset_stats_:
    push    bx

    ;; Calculate state pointer: BX = tx_states + (AX * STATE_SIZE)
    mov     cx, STATE_SIZE
    mul     cx                      ; DX:AX = AX * STATE_SIZE
    mov     bx, ax
    add     bx, tx_states

    ;; Zero total_tx (dword at +13)
    mov     word [bx + 13], 0
    mov     word [bx + 15], 0

    ;; Zero total_coalesced (dword at +17)
    mov     word [bx + 17], 0
    mov     word [bx + 19], 0

    ;; Zero total_interrupts (dword at +21)
    mov     word [bx + 21], 0
    mov     word [bx + 23], 0

    pop     bx
    retf

hot_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch table (empty - no patch points in this module)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
patch_table:
    ;; No patch entries

; ============================================================================
; mod_rxbatch_rt.asm - NASM 16-bit JIT Module for Batched RX Ring Refill
; ============================================================================
; Created: 2026-02-01 14:30:00
; CPU Target: 8086
; Module Type: MOD_CAP_CORE
; Description: Runtime batched receive ring buffer management with refill
;              tracking and statistics collection.
; ============================================================================

    cpu 8086
    bits 16

%include "patch_macros.inc"

; ============================================================================
; Constants
; ============================================================================

RX_STATE_SIZE       equ 24      ; per-NIC state size (bytes)
MAX_NICS            equ 8       ; maximum number of NICs
RX_RING_SIZE        equ 16      ; ring buffer size
RX_RING_MASK        equ 15      ; ring buffer index mask
DEFAULT_THRESHOLD   equ 4       ; default refill threshold

; Per-NIC State Layout (24 bytes)
; ============================================================================
; Offset  Size  Field
; ------  ----  -----
; +0      2     rx_head         (word) - next slot to fill
; +2      2     rx_tail         (word) - next slot to process
; +4      2     rx_count        (word) - buffers currently in ring
; +6      2     rx_capacity     (word) - max ring size (RX_RING_SIZE)
; +8      2     refill_threshold (word) - when to trigger refill
; +10     4     total_rx        (dword) - total packets received
; +14     4     total_refills   (dword) - total refill operations
; +18     4     total_drops     (dword) - total dropped packets
; +22     2     enabled         (word) - NIC enabled flag
; ============================================================================

; ============================================================================
; Module Header (64 bytes)
; ============================================================================

section .text class=MODULE

global _mod_rxbatch_rt_header
_mod_rxbatch_rt_header:
    db 'MOD', 0                      ; +0  magic
    dd mod_end - _mod_rxbatch_rt_header ; +4  module size
    dw 0x0001                         ; +8  version
    dw 0x0001                         ; +10 MOD_CAP_CORE
    dw 0                              ; +12 cpu_req (8086)
    dw 0                              ; +14 nic_req (any)
    dd _mod_init                      ; +16 init function
    dd _mod_cleanup                   ; +20 cleanup function
    dd PATCH_COUNT                    ; +24 patch count
    dd 0                              ; +28 patch table offset
    times 32 db 0                     ; +32 reserved (32 bytes)

PATCH_COUNT equ 0

; ============================================================================
; Data Section - Hot Path Data
; ============================================================================

section .data

align 4
rx_states:
    times (RX_STATE_SIZE * MAX_NICS) db 0  ; 192 bytes total

rx_initialized:
    db 0

align 2

; ============================================================================
; Code Section - Exported Functions
; ============================================================================

section .text

; ----------------------------------------------------------------------------
; rx_batch_refill_ - Refill RX ring buffer
; ----------------------------------------------------------------------------
; Input:  AX = nic_index (0-7)
; Output: AX = number of slots refilled (0 if full or invalid)
; Uses:   AX, BX, CX, DX, SI
; ----------------------------------------------------------------------------
global rx_batch_refill_
rx_batch_refill_:
    push bp
    mov bp, sp
    push si
    push di

    ; Validate NIC index
    cmp ax, MAX_NICS
    jae .invalid_nic

    ; Calculate state offset: nic_index * RX_STATE_SIZE
    mov cx, RX_STATE_SIZE
    mul cx                          ; AX = nic_index * 24
    mov si, ax                      ; SI = offset into rx_states
    add si, rx_states

    ; Check if ring is full: rx_count >= rx_capacity
    mov ax, [si + 4]                ; AX = rx_count
    mov dx, [si + 6]                ; DX = rx_capacity
    cmp ax, dx
    jae .ring_full

    ; Calculate free slots: rx_capacity - rx_count
    sub dx, ax                      ; DX = slots to fill
    mov cx, dx                      ; CX = slots to fill (counter)
    test cx, cx
    jz .ring_full

    ; Refill loop
    xor di, di                      ; DI = slots filled counter

.refill_loop:
    ; Advance rx_head with wrap
    mov ax, [si + 0]                ; AX = rx_head
    inc ax
    and ax, RX_RING_MASK            ; Wrap around
    mov [si + 0], ax                ; Update rx_head

    ; Increment rx_count
    inc word [si + 4]

    ; Track slots filled
    inc di

    loop .refill_loop

    ; Update total_refills counter (dword at +14)
    mov ax, di                      ; AX = slots filled
    xor dx, dx                      ; DX:AX = slots filled (dword)
    add [si + 14], ax               ; Add low word
    adc [si + 16], dx               ; Add high word with carry

    ; Return slots filled
    mov ax, di
    jmp .done

.ring_full:
.invalid_nic:
    xor ax, ax                      ; Return 0

.done:
    pop di
    pop si
    pop bp
    retf

; ----------------------------------------------------------------------------
; rx_batch_process_ - Process packets from RX ring
; ----------------------------------------------------------------------------
; Input:  AX = nic_index (0-7)
;         DX = max_packets to process
; Output: AX = number of packets processed
; Uses:   AX, BX, CX, DX, SI
; ----------------------------------------------------------------------------
global rx_batch_process_
rx_batch_process_:
    push bp
    mov bp, sp
    push si
    push di

    ; Validate NIC index
    cmp ax, MAX_NICS
    jae .invalid_nic

    ; Calculate state offset: nic_index * RX_STATE_SIZE
    mov cx, RX_STATE_SIZE
    mul cx                          ; AX = nic_index * 24
    mov si, ax                      ; SI = offset into rx_states
    add si, rx_states

    ; Determine how many packets to process
    mov ax, [si + 4]                ; AX = rx_count
    test ax, ax
    jz .no_packets                  ; No packets available

    ; Limit to max_packets
    cmp ax, dx
    jbe .count_ok
    mov ax, dx                      ; Use max_packets limit

.count_ok:
    mov cx, ax                      ; CX = packets to process
    xor di, di                      ; DI = packets processed counter

.process_loop:
    ; Advance rx_tail with wrap
    mov ax, [si + 2]                ; AX = rx_tail
    inc ax
    and ax, RX_RING_MASK            ; Wrap around
    mov [si + 2], ax                ; Update rx_tail

    ; Decrement rx_count
    dec word [si + 4]

    ; Track packets processed
    inc di

    loop .process_loop

    ; Update total_rx counter (dword at +10)
    mov ax, di                      ; AX = packets processed
    xor dx, dx                      ; DX:AX = packets processed (dword)
    add [si + 10], ax               ; Add low word
    adc [si + 12], dx               ; Add high word with carry

    ; Return packets processed
    mov ax, di
    jmp .done

.no_packets:
.invalid_nic:
    xor ax, ax                      ; Return 0

.done:
    pop di
    pop si
    pop bp
    retf

; ----------------------------------------------------------------------------
; rx_batch_get_stats_ - Retrieve RX statistics
; ----------------------------------------------------------------------------
; Input:  AX = nic_index (0-7)
;         DX:BX = far pointer to output buffer (12 bytes)
;                 [0-3]: total_rx (dword)
;                 [4-7]: total_refills (dword)
;                 [8-11]: total_drops (dword)
; Output: None (void)
; Uses:   AX, BX, CX, DX, SI, DI, ES
; ----------------------------------------------------------------------------
global rx_batch_get_stats_
rx_batch_get_stats_:
    push bp
    mov bp, sp
    push si
    push di
    push es

    ; Save output pointer
    mov es, dx                      ; ES:DI = output buffer
    mov di, bx

    ; Validate NIC index
    cmp ax, MAX_NICS
    jae .invalid_nic

    ; Calculate state offset: nic_index * RX_STATE_SIZE
    mov cx, RX_STATE_SIZE
    mul cx                          ; AX = nic_index * 24
    mov si, ax                      ; SI = offset into rx_states
    add si, rx_states

    ; Copy total_rx (dword at +10)
    mov ax, [si + 10]
    mov es:[di + 0], ax             ; Low word
    mov ax, [si + 12]
    mov es:[di + 2], ax             ; High word

    ; Copy total_refills (dword at +14)
    mov ax, [si + 14]
    mov es:[di + 4], ax             ; Low word
    mov ax, [si + 16]
    mov es:[di + 6], ax             ; High word

    ; Copy total_drops (dword at +18)
    mov ax, [si + 18]
    mov es:[di + 8], ax             ; Low word
    mov ax, [si + 20]
    mov es:[di + 10], ax            ; High word

    jmp .done

.invalid_nic:
    ; Zero out output buffer on error
    xor ax, ax
    mov cx, 6                       ; 6 words = 12 bytes
.zero_loop:
    stosw
    loop .zero_loop

.done:
    pop es
    pop di
    pop si
    pop bp
    retf

; ----------------------------------------------------------------------------
; rx_alloc_64k_safe_ - Allocate 64K-safe DMA buffer (stub)
; ----------------------------------------------------------------------------
; Input:  AX = length (bytes)
;         DX:BX = far pointer to phys_out (dword)
; Output: DX:AX = virtual address (always 0:0 for stub)
;         phys_out = physical address (always 0 for stub)
; Uses:   AX, BX, CX, DX, ES, DI
; Notes:  Actual allocation happens at init time. This is a runtime stub.
; ----------------------------------------------------------------------------
global rx_alloc_64k_safe_
rx_alloc_64k_safe_:
    push bp
    mov bp, sp
    push es
    push di

    ; Write 0 to phys_out (dword)
    mov es, dx                      ; ES:DI = phys_out
    mov di, bx
    xor ax, ax
    stosw                           ; Low word = 0
    stosw                           ; High word = 0

    ; Return NULL pointer (DX:AX = 0:0)
    xor dx, dx
    xor ax, ax

    pop di
    pop es
    pop bp
    retf

; ----------------------------------------------------------------------------
; _mod_init - Module initialization
; ----------------------------------------------------------------------------
; Input:  None
; Output: AX = 0 on success, non-zero on failure
; Uses:   AX, BX, CX, SI, DI
; ----------------------------------------------------------------------------
_mod_init:
    push bp
    mov bp, sp
    push si

    ; Check if already initialized
    mov al, [rx_initialized]
    test al, al
    jnz .already_init

    ; Initialize all NIC states
    mov si, rx_states
    mov cx, MAX_NICS

.init_loop:
    ; rx_head = 0
    mov word [si + 0], 0
    ; rx_tail = 0
    mov word [si + 2], 0
    ; rx_count = 0
    mov word [si + 4], 0
    ; rx_capacity = RX_RING_SIZE
    mov word [si + 6], RX_RING_SIZE
    ; refill_threshold = DEFAULT_THRESHOLD
    mov word [si + 8], DEFAULT_THRESHOLD
    ; total_rx = 0
    mov word [si + 10], 0
    mov word [si + 12], 0
    ; total_refills = 0
    mov word [si + 14], 0
    mov word [si + 16], 0
    ; total_drops = 0
    mov word [si + 18], 0
    mov word [si + 20], 0
    ; enabled = 0
    mov word [si + 22], 0

    add si, RX_STATE_SIZE
    loop .init_loop

    ; Mark as initialized
    mov byte [rx_initialized], 1

.already_init:
    xor ax, ax                      ; Success
    pop si
    pop bp
    ret

; ----------------------------------------------------------------------------
; _mod_cleanup - Module cleanup
; ----------------------------------------------------------------------------
; Input:  None
; Output: None
; Uses:   AX
; ----------------------------------------------------------------------------
_mod_cleanup:
    push bp
    mov bp, sp

    ; Mark as uninitialized
    mov byte [rx_initialized], 0

    pop bp
    ret

; ============================================================================
; Module End Marker
; ============================================================================
mod_end:

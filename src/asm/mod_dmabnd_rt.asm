; ============================================================================
; mod_dmabnd_rt.asm - DMA Bounce Buffer Management Runtime Module
; ============================================================================
; JIT runtime module for managing DMA bounce buffers to avoid 64KB boundary
; crossing and handle buffers above 1MB in conventional/extended memory.
;
; Created: 2026-02-01 15:30:00 PST
; Target: 8086+ real mode
; Assembler: NASM
; Calling Convention: Watcom far call (retf)
; ============================================================================

[bits 16]
[cpu 8086]

%include "patch_macros.inc"

; ============================================================================
; Constants
; ============================================================================
NUM_TX_BOUNCE   equ 4           ; TX bounce buffer count
NUM_RX_BOUNCE   equ 4           ; RX bounce buffer count
BOUNCE_BUF_SIZE equ 1536        ; max Ethernet frame size
POOL_SIZE       equ 24          ; sizeof bounce_pool_t structure

PATCH_COUNT     equ 0           ; No patchable locations

; ============================================================================
; Bounce Pool Structure Layout (24 bytes)
; ============================================================================
; Offset  Size  Field
; ------  ----  -----
; +0      word  base_seg        - segment of pool memory
; +2      word  base_off        - offset within segment
; +4      word  buf_size        - size of each buffer (BOUNCE_BUF_SIZE)
; +6      word  num_bufs        - number of buffers in pool
; +8      word  bitmap          - allocation bitmap (bit=1 means in use)
; +10     word  phys_base_lo    - physical address low word
; +12     word  phys_base_hi    - physical address high word
; +14     dword alloc_count     - total allocations
; +18     dword free_count      - total frees
; +22     word  exhausted_count - times pool was exhausted
; ============================================================================

section .text class=MODULE

; ============================================================================
; Module Header (64 bytes)
; ============================================================================
global _mod_dmabnd_rt_header
_mod_dmabnd_rt_header:
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
    db 0                            ; cpu_requirements (8086)
    db 0                            ; nic_type (0 = any)
    dw 8000h                        ; capability flags (MOD_CAP_CORE)
    times (64 - ($ - header)) db 0  ; pad header to 64 bytes

hot_start:

; ============================================================================
; Hot Data Section (frequently accessed variables)
; ============================================================================
tx_pool:        times POOL_SIZE db 0    ; TX bounce buffer pool (24 bytes)
rx_pool:        times POOL_SIZE db 0    ; RX bounce buffer pool (24 bytes)
boundary_stats: times 44 db 0           ; Boundary crossing statistics
pools_init:     db 0                    ; Pools initialized flag
v86_detected:   db 0                    ; Virtual 8086 mode detected flag

; ============================================================================
; Function: dma_check_buffer_safety_
; ============================================================================
; Check if a buffer is safe for ISA DMA operations
;
; Input:
;   DX:AX = Far pointer to buffer (segment:offset)
;   BX    = Buffer length in bytes
;   [bp+8]:[bp+6] = Far pointer to result structure (37 bytes)
;
; Output:
;   Result structure populated with safety analysis
;   AX = 0 (success)
;
; Result structure (37 bytes):
;   +0  byte   is_safe         - 1 if safe for DMA, 0 if not
;   +1  byte   crosses_64k     - 1 if crosses 64KB boundary
;   +2  dword  phys_addr       - physical address
;   +6  word   dma_page        - DMA page number (addr >> 16)
;   +8  word   page_offset     - offset within DMA page
;   +10 byte   above_1mb       - 1 if above 1MB
;   +11 byte   above_16mb      - 1 if above 16MB (ISA limit)
;   +12 dword  boundary_cross  - bytes to next 64KB boundary
;   +16 byte   needs_bounce    - 1 if bounce buffer required
;   +17-36 reserved
; ============================================================================
global dma_check_buffer_safety_
dma_check_buffer_safety_:
    push bp
    mov bp, sp
    push ds
    push es
    push si
    push di
    push bx
    push cx
    push dx

    ; Load result pointer into ES:DI
    mov es, [bp+8]
    mov di, [bp+6]

    ; Clear result structure
    push di
    mov cx, 37
    xor al, al
    rep stosb
    pop di

    ; Calculate physical address: phys = (segment * 16) + offset
    ; DX = segment, AX = offset, BX = length
    mov cx, dx              ; CX = segment
    mov dx, ax              ; DX = offset (save)

    ; Multiply segment by 16: shift left 4 bits
    mov al, cl
    mov ah, ch
    mov cl, 4
    shl ax, cl              ; AX = (segment & 0x0FFF) << 4

    mov cx, dx              ; Restore segment to CX
    mov dx, ax              ; DX = low bits of (seg * 16)

    ; Now compute high word of segment * 16
    xor ax, ax
    mov al, ch
    shr al, cl              ; AX = (segment >> 12)

    ; Add offset to get full physical address
    ; Physical address now: AX:DX (high:low)
    add dx, [bp-18]         ; Add original offset (saved in stack)
    adc ax, 0

    ; Store physical address at result+2 (dword)
    mov [es:di+2], dx       ; Low word
    mov [es:di+4], ax       ; High word

    ; Check if above 1MB (0x100000)
    cmp ax, 0x10
    jb .below_1mb
    ja .above_1mb
    cmp dx, 0
    jbe .below_1mb

.above_1mb:
    mov byte [es:di+10], 1  ; Set above_1mb flag

    ; Check if above 16MB (0x1000000) - ISA DMA limit
    cmp ax, 0x100
    jae .above_16mb

    ; Not safe if above 1MB (simplified check)
    mov byte [es:di+0], 0   ; is_safe = 0
    mov byte [es:di+16], 1  ; needs_bounce = 1
    jmp .done

.above_16mb:
    mov byte [es:di+11], 1  ; Set above_16mb flag
    mov byte [es:di+0], 0   ; is_safe = 0
    mov byte [es:di+16], 1  ; needs_bounce = 1
    jmp .done

.below_1mb:
    ; Check for 64KB boundary crossing
    ; Current offset in page: DX (low word of physical address)
    ; Add buffer length and check for carry/overflow
    mov cx, bx              ; CX = length
    add cx, dx              ; CX = offset + length
    jnc .no_64k_cross       ; If no carry, doesn't cross 64KB

.crosses_64k:
    mov byte [es:di+1], 1   ; Set crosses_64k flag
    mov byte [es:di+0], 0   ; is_safe = 0
    mov byte [es:di+16], 1  ; needs_bounce = 1

    ; Calculate bytes to boundary: 0x10000 - offset
    mov cx, 0x10000
    sub cx, dx
    mov [es:di+12], cx      ; Store boundary_cross (low word)
    mov word [es:di+14], 0  ; High word is 0
    jmp .done

.no_64k_cross:
    ; Buffer is safe: below 1MB and doesn't cross 64KB boundary
    mov byte [es:di+0], 1   ; is_safe = 1
    mov byte [es:di+16], 0  ; needs_bounce = 0

.done:
    ; Calculate DMA page and offset
    mov ax, [es:di+4]       ; High word of physical address
    mov dx, [es:di+2]       ; Low word
    mov [es:di+6], ax       ; DMA page = phys_addr >> 16
    mov [es:di+8], dx       ; Page offset = low word

    pop dx
    pop cx
    pop bx
    pop di
    pop si
    pop es
    pop ds
    pop bp

    xor ax, ax              ; Return 0 (success)
    retf

; ============================================================================
; Function: dma_get_tx_bounce_buffer_
; ============================================================================
; Allocate a TX bounce buffer from the pool
;
; Input:
;   None
;
; Output:
;   DX:AX = Far pointer to allocated buffer (segment:offset)
;           Returns 0:0 if pool exhausted
; ============================================================================
global dma_get_tx_bounce_buffer_
dma_get_tx_bounce_buffer_:
    push bp
    mov bp, sp
    push bx
    push cx
    push si

    cli                         ; Disable interrupts for atomic operation

    ; Load TX pool structure
    mov si, tx_pool

    ; Check bitmap for free buffer
    mov ax, [si+8]              ; Load bitmap
    mov cx, NUM_TX_BOUNCE       ; Number of buffers to check
    xor bx, bx                  ; BX = bit index

.scan_loop:
    test ax, 1                  ; Test bit 0
    jz .found_free              ; If bit is 0, buffer is free

    shr ax, 1                   ; Shift to next bit
    inc bx                      ; Increment bit index
    loop .scan_loop

    ; No free buffers found
    inc word [si+22]            ; Increment exhausted_count
    sti                         ; Re-enable interrupts
    xor dx, dx                  ; Return NULL (0:0)
    xor ax, ax
    jmp .exit

.found_free:
    ; Mark buffer as allocated by setting bit in bitmap
    mov ax, 1
    mov cx, bx                  ; CX = bit index
    jcxz .no_shift              ; If bit 0, no shift needed
.shift_loop:
    shl ax, 1
    loop .shift_loop
.no_shift:
    or [si+8], ax               ; Set bit in bitmap

    ; Increment allocation count
    add word [si+14], 1         ; Increment alloc_count (low word)
    adc word [si+16], 0         ; Increment high word if carry

    ; Calculate buffer address: base + (bit_index * BOUNCE_BUF_SIZE)
    mov ax, bx                  ; AX = bit_index
    mov cx, BOUNCE_BUF_SIZE
    mul cx                      ; DX:AX = bit_index * BOUNCE_BUF_SIZE

    ; Add base offset
    add ax, [si+2]              ; Add base_off
    adc dx, 0

    ; Add to segment if offset overflows
    mov cx, [si+0]              ; CX = base_seg
    add cx, dx                  ; Adjust segment for overflow

    mov dx, cx                  ; DX = segment
                                ; AX = offset
    sti                         ; Re-enable interrupts

.exit:
    pop si
    pop cx
    pop bx
    pop bp
    retf

; ============================================================================
; Function: dma_release_tx_bounce_buffer_
; ============================================================================
; Release a TX bounce buffer back to the pool
;
; Input:
;   DX:AX = Far pointer to buffer to release (segment:offset)
;
; Output:
;   None (void)
; ============================================================================
global dma_release_tx_bounce_buffer_
dma_release_tx_bounce_buffer_:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si

    cli                         ; Disable interrupts for atomic operation

    ; Load TX pool structure
    mov si, tx_pool

    ; Calculate bit index: (offset - base_off) / BOUNCE_BUF_SIZE
    sub ax, [si+2]              ; AX = offset - base_off
    mov cx, BOUNCE_BUF_SIZE
    xor dx, dx                  ; Clear DX for division
    div cx                      ; AX = bit_index, DX = remainder

    mov bx, ax                  ; BX = bit_index

    ; Clear bit in bitmap
    mov ax, 1
    mov cx, bx                  ; CX = bit index
    jcxz .no_shift2             ; If bit 0, no shift needed
.shift_loop2:
    shl ax, 1
    loop .shift_loop2
.no_shift2:
    not ax                      ; Invert to create clear mask
    and [si+8], ax              ; Clear bit in bitmap

    ; Increment free count
    add word [si+18], 1         ; Increment free_count (low word)
    adc word [si+20], 0         ; Increment high word if carry

    sti                         ; Re-enable interrupts

    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    retf

; ============================================================================
; Function: dma_get_rx_bounce_buffer_
; ============================================================================
; Allocate an RX bounce buffer from the pool
;
; Input:
;   None
;
; Output:
;   DX:AX = Far pointer to allocated buffer (segment:offset)
;           Returns 0:0 if pool exhausted
; ============================================================================
global dma_get_rx_bounce_buffer_
dma_get_rx_bounce_buffer_:
    push bp
    mov bp, sp
    push bx
    push cx
    push si

    cli                         ; Disable interrupts for atomic operation

    ; Load RX pool structure
    mov si, rx_pool

    ; Check bitmap for free buffer
    mov ax, [si+8]              ; Load bitmap
    mov cx, NUM_RX_BOUNCE       ; Number of buffers to check
    xor bx, bx                  ; BX = bit index

.scan_loop:
    test ax, 1                  ; Test bit 0
    jz .found_free              ; If bit is 0, buffer is free

    shr ax, 1                   ; Shift to next bit
    inc bx                      ; Increment bit index
    loop .scan_loop

    ; No free buffers found
    inc word [si+22]            ; Increment exhausted_count
    sti                         ; Re-enable interrupts
    xor dx, dx                  ; Return NULL (0:0)
    xor ax, ax
    jmp .exit

.found_free:
    ; Mark buffer as allocated by setting bit in bitmap
    mov ax, 1
    mov cx, bx                  ; CX = bit index
    jcxz .no_shift              ; If bit 0, no shift needed
.shift_loop:
    shl ax, 1
    loop .shift_loop
.no_shift:
    or [si+8], ax               ; Set bit in bitmap

    ; Increment allocation count
    add word [si+14], 1         ; Increment alloc_count (low word)
    adc word [si+16], 0         ; Increment high word if carry

    ; Calculate buffer address: base + (bit_index * BOUNCE_BUF_SIZE)
    mov ax, bx                  ; AX = bit_index
    mov cx, BOUNCE_BUF_SIZE
    mul cx                      ; DX:AX = bit_index * BOUNCE_BUF_SIZE

    ; Add base offset
    add ax, [si+2]              ; Add base_off
    adc dx, 0

    ; Add to segment if offset overflows
    mov cx, [si+0]              ; CX = base_seg
    add cx, dx                  ; Adjust segment for overflow

    mov dx, cx                  ; DX = segment
                                ; AX = offset
    sti                         ; Re-enable interrupts

.exit:
    pop si
    pop cx
    pop bx
    pop bp
    retf

; ============================================================================
; Function: dma_release_rx_bounce_buffer_
; ============================================================================
; Release an RX bounce buffer back to the pool
;
; Input:
;   DX:AX = Far pointer to buffer to release (segment:offset)
;
; Output:
;   None (void)
; ============================================================================
global dma_release_rx_bounce_buffer_
dma_release_rx_bounce_buffer_:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si

    cli                         ; Disable interrupts for atomic operation

    ; Load RX pool structure
    mov si, rx_pool

    ; Calculate bit index: (offset - base_off) / BOUNCE_BUF_SIZE
    sub ax, [si+2]              ; AX = offset - base_off
    mov cx, BOUNCE_BUF_SIZE
    xor dx, dx                  ; Clear DX for division
    div cx                      ; AX = bit_index, DX = remainder

    mov bx, ax                  ; BX = bit_index

    ; Clear bit in bitmap
    mov ax, 1
    mov cx, bx                  ; CX = bit index
    jcxz .no_shift2             ; If bit 0, no shift needed
.shift_loop2:
    shl ax, 1
    loop .shift_loop2
.no_shift2:
    not ax                      ; Invert to create clear mask
    and [si+8], ax              ; Clear bit in bitmap

    ; Increment free count
    add word [si+18], 1         ; Increment free_count (low word)
    adc word [si+20], 0         ; Increment high word if carry

    sti                         ; Re-enable interrupts

    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    retf

; ============================================================================
; Function: is_safe_for_direct_dma_
; ============================================================================
; Quick check if buffer is safe for direct DMA (no bounce needed)
;
; Input:
;   DX:AX = Far pointer to buffer (segment:offset)
;   BX    = Buffer length in bytes
;
; Output:
;   AX = 1 if safe for direct DMA, 0 if needs bounce buffer
; ============================================================================
global is_safe_for_direct_dma_
is_safe_for_direct_dma_:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx

    ; Calculate physical address: phys = (segment * 16) + offset
    mov cx, dx              ; CX = segment
    mov dx, ax              ; DX = offset (save)

    ; Multiply segment by 16
    mov al, cl
    mov ah, ch
    mov cl, 4
    shl ax, cl              ; AX = (segment & 0x0FFF) << 4

    mov cx, dx              ; Restore segment to CX
    push ax                 ; Save low bits

    xor ax, ax
    mov al, ch
    mov cl, 4
    shr al, cl              ; AX = (segment >> 12)

    pop dx                  ; DX = low bits of (seg * 16)
    add dx, [bp-6]          ; Add original offset
    adc ax, 0               ; AX:DX = physical address

    ; Check if above 16MB (0x1000000)
    cmp ax, 0x100
    jae .not_safe

    ; Check if adding length crosses 64KB boundary
    mov cx, bx              ; CX = length
    add cx, dx              ; Add offset
    jc .not_safe            ; If carry, crosses 64KB boundary

    ; Safe for direct DMA
    mov ax, 1
    jmp .exit

.not_safe:
    xor ax, ax              ; Return 0 (not safe)

.exit:
    pop dx
    pop cx
    pop bx
    pop bp
    retf

; ============================================================================
; Function: dma_get_boundary_stats_
; ============================================================================
; Retrieve DMA boundary crossing statistics
;
; Input:
;   DX:AX = Far pointer to output buffer (44 bytes)
;
; Output:
;   Statistics copied to output buffer
;   None (void)
; ============================================================================
global dma_get_boundary_stats_
dma_get_boundary_stats_:
    push bp
    mov bp, sp
    push ds
    push es
    push si
    push di
    push cx

    ; Setup source (DS:SI) and destination (ES:DI)
    mov si, boundary_stats
    push cs
    pop ds                  ; DS = CS (source segment)

    mov es, dx              ; ES = destination segment
    mov di, ax              ; DI = destination offset

    ; Copy 44 bytes
    mov cx, 44
    rep movsb

    pop cx
    pop di
    pop si
    pop es
    pop ds
    pop bp
    retf

; ============================================================================
; Hot Section End
; ============================================================================
hot_end:

; ============================================================================
; Patch Table (empty - no patchable locations)
; ============================================================================
patch_table:

; ============================================================================
; End of mod_dmabnd_rt.asm
; ============================================================================

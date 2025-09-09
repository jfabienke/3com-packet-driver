; el3_smc.asm - Self-Modifying Code optimization for hot paths
; Uses runtime code patching to eliminate indirect calls and branches

section .text

global _el3_install_smc_hooks
global _el3_smc_tx_entry
global _el3_smc_rx_entry

; SMC markers for patching
SMC_TX_IOBASE   equ 0xDEAD
SMC_RX_IOBASE   equ 0xBEEF
SMC_TX_HANDLER  equ 0xCAFE
SMC_RX_HANDLER  equ 0xFEED

; Pipeline flush macro for safe SMC on 486+
%macro FLUSH_PIPELINE 0
    jmp     short $+2       ; Near jump to flush prefetch queue
    nop                     ; Landing pad
    nop
%endmacro

; Install SMC hooks for a device
; void el3_install_smc_hooks(struct el3_device *dev)
_el3_install_smc_hooks:
    push    bp
    mov     bp, sp
    push    si
    push    di
    push    es
    
    ; Get device pointer
    mov     si, [bp+4]      ; dev parameter
    
    ; Get I/O base address
    mov     ax, [si+8]      ; dev->iobase
    
    ; Patch TX entry point with I/O base
    push    cs
    pop     es
    mov     di, tx_iobase_patch
    stosw
    mov     di, tx_iobase_patch2
    stosw
    
    ; Patch RX entry point with I/O base  
    mov     di, rx_iobase_patch
    stosw
    mov     di, rx_iobase_patch2
    stosw
    
    ; Check generation and patch handler addresses
    mov     al, [si+12]     ; dev->generation
    
    cmp     al, 0           ; EL3_GEN_VORTEX
    je      .patch_pio
    
    ; Patch for DMA handler
    mov     ax, dma_tx_handler
    mov     di, tx_handler_patch
    stosw
    
    mov     ax, dma_rx_handler
    mov     di, rx_handler_patch
    stosw
    
    jmp     .flush
    
.patch_pio:
    ; Patch for PIO handler
    mov     ax, pio_tx_handler
    mov     di, tx_handler_patch
    stosw
    
    mov     ax, pio_rx_handler
    mov     di, rx_handler_patch
    stosw
    
.flush:
    ; Flush pipeline after patching
    FLUSH_PIPELINE
    
    pop     es
    pop     di
    pop     si
    pop     bp
    ret

; SMC TX entry point - gets patched at runtime
_el3_smc_tx_entry:
    push    bp
    push    si
    push    di
    
    ; Load I/O base (patched at runtime)
    mov     dx, SMC_TX_IOBASE
tx_iobase_patch equ $-2
    
    ; Jump to handler (patched at runtime)
    jmp     word SMC_TX_HANDLER
tx_handler_patch equ $-2

; PIO TX handler (hot path)
pio_tx_handler:
    ; Check TX free space
    add     dx, 0x1C        ; PORT_TX_FREE
    in      ax, dx
    
    cmp     ax, [bp+8]      ; Compare with packet length
    jb      .tx_busy
    
    ; Reset port to TX data
    sub     dx, 0x1C
    
    ; Send packet length
    mov     ax, [bp+8]
    out     dx, ax
    
    ; Send reserved word
    xor     ax, ax
    out     dx, ax
    
    ; Setup for data copy
    mov     si, [bp+6]      ; Source buffer
    mov     cx, [bp+8]      ; Length
    shr     cx, 1           ; Convert to words
    
    ; Fast word copy loop
    cld
.tx_loop:
    lodsw
    out     dx, ax
    loop    .tx_loop
    
    ; Handle odd byte
    test    word [bp+8], 1
    jz      .tx_done
    lodsb
    out     dx, al
    
.tx_done:
    xor     ax, ax          ; Success
    jmp     .tx_exit
    
.tx_busy:
    mov     ax, -1          ; Busy
    
.tx_exit:
    pop     di
    pop     si
    pop     bp
    ret

; DMA TX handler (hot path)
dma_tx_handler:
    ; Get TX ring head
    mov     si, [bp+4]      ; dev pointer
    mov     al, [si+24]     ; dev->tx_head
    
    ; Calculate next head
    inc     al
    and     al, 0x0F        ; Wrap at 16
    
    ; Check if ring full
    cmp     al, [si+25]     ; dev->tx_tail
    je      .dma_tx_full
    
    ; Get descriptor address
    mov     di, [si+16]     ; dev->dma_tx_ring
    movzx   bx, byte [si+24]; Current head
    shl     bx, 4           ; * sizeof(descriptor)
    add     di, bx
    
    ; Copy data to DMA buffer
    push    es
    push    ds
    
    mov     ax, [di+8]      ; Buffer physical address low
    mov     dx, [di+10]     ; Buffer physical address high
    
    ; Convert physical to linear
    mov     bx, ax
    shr     dx, 4
    mov     es, dx
    mov     di, bx
    
    mov     si, [bp+6]      ; Source buffer
    mov     cx, [bp+8]      ; Length
    rep     movsb
    
    pop     ds
    pop     es
    
    ; Update descriptor
    mov     ax, [bp+8]
    or      ax, 0x8000      ; Set ownership bit
    mov     [di+12], ax     ; Update length field
    
    ; Update head pointer
    mov     si, [bp+4]
    inc     byte [si+24]
    and     byte [si+24], 0x0F
    
    ; Kick DMA engine
    mov     dx, SMC_TX_IOBASE
tx_iobase_patch2 equ $-2
    add     dx, 0x0E        ; PORT_CMD
    mov     ax, 0x00CA      ; DMA TX start
    out     dx, ax
    
    xor     ax, ax          ; Success
    jmp     .dma_tx_exit
    
.dma_tx_full:
    mov     ax, -1          ; Ring full
    
.dma_tx_exit:
    pop     di
    pop     si
    pop     bp
    ret

; SMC RX entry point - gets patched at runtime
_el3_smc_rx_entry:
    push    bp
    push    si
    push    di
    
    ; Load I/O base (patched at runtime)
    mov     dx, SMC_RX_IOBASE
rx_iobase_patch equ $-2
    
    ; Jump to handler (patched at runtime)
    jmp     word SMC_RX_HANDLER
rx_handler_patch equ $-2

; PIO RX handler (hot path)
pio_rx_handler:
    ; Check RX status
    add     dx, 0x18        ; PORT_RX_STATUS
    in      ax, dx
    
    test    ax, 0x8000      ; Check if packet ready
    jz      .rx_empty
    
    test    ax, 0x4000      ; Check for errors
    jnz     .rx_error
    
    ; Get packet length
    and     ax, 0x1FFF
    mov     cx, ax
    
    ; Reset port to RX data
    sub     dx, 0x18
    
    ; Setup for data copy
    mov     di, [bp+6]      ; Destination buffer
    push    cx              ; Save length
    
    shr     cx, 1           ; Convert to words
    
    ; Fast word copy loop
    cld
.rx_loop:
    in      ax, dx
    stosw
    loop    .rx_loop
    
    ; Handle odd byte
    pop     cx
    test    cx, 1
    jz      .rx_done
    in      al, dx
    stosb
    
.rx_done:
    ; Return packet length
    mov     ax, cx
    jmp     .rx_exit
    
.rx_error:
    ; Reset RX
    sub     dx, 0x18
    add     dx, 0x0E        ; PORT_CMD
    mov     ax, 0x2800      ; CMD_RX_RESET
    out     dx, ax
    mov     ax, 0x2000      ; CMD_RX_ENABLE
    out     dx, ax
    
.rx_empty:
    xor     ax, ax          ; No packet
    
.rx_exit:
    pop     di
    pop     si
    pop     bp
    ret

; DMA RX handler (hot path)
dma_rx_handler:
    ; Get RX ring head
    mov     si, [bp+4]      ; dev pointer
    movzx   bx, byte [si+26]; dev->rx_head
    
    ; Get descriptor address
    mov     di, [si+20]     ; dev->dma_rx_ring
    shl     bx, 4           ; * sizeof(descriptor)
    add     di, bx
    
    ; Check ownership
    mov     ax, [di+4]      ; Status word
    test    ax, 0x8000      ; Check ownership bit
    jnz     .dma_rx_empty
    
    ; Check for errors
    test    ax, 0x8000
    jz      .dma_rx_error
    
    ; Get packet length
    mov     cx, [di+6]
    shr     cx, 16
    and     cx, 0x1FFF
    
    ; Copy from DMA buffer
    push    es
    push    ds
    
    mov     ax, [di+8]      ; Buffer physical address low
    mov     dx, [di+10]     ; Buffer physical address high
    
    ; Convert physical to linear
    mov     bx, ax
    shr     dx, 4
    mov     ds, dx
    mov     si, bx
    
    mov     di, [bp+6]      ; Destination buffer
    push    cx              ; Save length
    rep     movsb
    
    pop     ax              ; Return length
    pop     ds
    pop     es
    
    ; Reset descriptor
    mov     di, [si+20]
    movzx   bx, byte [si+26]
    shl     bx, 4
    add     di, bx
    mov     word [di+4], 0x8000
    
    ; Update head pointer
    inc     byte [si+26]
    and     byte [si+26], 0x0F
    
    jmp     .dma_rx_exit
    
.dma_rx_error:
    ; Reset descriptor
    mov     word [di+4], 0x8000
    inc     byte [si+26]
    and     byte [si+26], 0x0F
    
.dma_rx_empty:
    xor     ax, ax          ; No packet
    
.dma_rx_exit:
    pop     di
    pop     si
    pop     bp
    ret
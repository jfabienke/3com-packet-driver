; el3_smc_enhanced.asm - Enhanced SMC with ISR deferral integration
; Combines existing SMC optimization with new performance techniques

section .text

global _el3_install_enhanced_smc_hooks
global _el3_enhanced_isr_entry
global _el3_enhanced_tx_entry
global _el3_enhanced_rx_entry

; Import from isr_tiny.asm
extern _tiny_isr_entry
extern _install_tiny_isr

; Import from workqueue.c
extern workqueue_enqueue_rx
extern workqueue_enqueue_tx_complete
extern workqueue_get_pending_flag

; Enhanced SMC markers
SMC_IOBASE          equ 0xDEAD
SMC_WORK_FLAG       equ 0xBEEF
SMC_COPY_THRESHOLD  equ 0xCAFE
SMC_K_PKTS          equ 0xFEED
SMC_DOORBELL_REG    equ 0xABCD
SMC_WINDOW_STATE    equ 0x1234

; Performance counters
section .data
enhanced_smc_stats:
    isr_calls           dd 0
    work_generated      dd 0
    copy_break_small    dd 0
    copy_break_large    dd 0
    interrupts_coalesced dd 0
    doorbells_batched   dd 0

section .text

; Pipeline flush macro for safe SMC
%macro FLUSH_PIPELINE 0
    jmp     short $+2
    nop
    nop
%endmacro

; Install enhanced SMC hooks with all optimizations
; void el3_install_enhanced_smc_hooks(struct el3_device *dev, struct smc_config *config)
_el3_install_enhanced_smc_hooks:
    push    bp
    mov     bp, sp
    push    si
    push    di
    push    es
    push    bx
    
    ; Get parameters
    mov     si, [bp+4]      ; dev pointer
    mov     bx, [bp+6]      ; config pointer
    
    ; Install basic tiny ISR first
    push    si
    call    _install_tiny_isr
    add     sp, 2
    
    ; Get device configuration
    mov     ax, [si+8]      ; dev->iobase
    mov     cl, [si+12]     ; dev->generation
    mov     dl, [si+13]     ; dev->irq
    
    ; Patch I/O base addresses
    mov     [smc_iobase_patch1], ax
    mov     [smc_iobase_patch2], ax
    mov     [smc_iobase_patch3], ax
    
    ; Get work flag address from workqueue
    mov     al, [si+14]     ; dev->device_id
    push    ax
    call    workqueue_get_pending_flag
    add     sp, 2
    mov     [smc_work_flag_patch], ax
    
    ; Patch copy-break threshold
    mov     ax, [bx+0]      ; config->copy_break_threshold
    test    ax, ax
    jnz     .has_threshold
    mov     ax, 192         ; Default threshold
.has_threshold:
    mov     [smc_copy_threshold_patch], ax
    
    ; Patch K_PKTS for interrupt coalescing
    mov     al, [bx+2]      ; config->k_pkts
    test    al, al
    jnz     .has_k_pkts
    mov     al, 8           ; Default K_PKTS
.has_k_pkts:
    mov     [smc_k_pkts_patch], al
    
    ; Patch doorbell register
    mov     ax, [si+8]      ; dev->iobase
    cmp     byte [si+12], 0 ; Check if Vortex (PIO)
    je      .vortex_doorbell
    ; Boomerang+ DMA doorbell
    add     ax, 0x24        ; DN_LIST_PTR
    jmp     .set_doorbell
.vortex_doorbell:
    add     ax, 0x0E        ; Command register
.set_doorbell:
    mov     [smc_doorbell_patch], ax
    
    ; Initialize window state tracking for Vortex
    cmp     byte [si+12], 0
    jne     .skip_window
    mov     byte [smc_window_state_patch], 1    ; Start in window 1
    
.skip_window:
    ; Flush pipeline after all patches
    FLUSH_PIPELINE
    
    pop     bx
    pop     es
    pop     di
    pop     si
    pop     bp
    ret

; Enhanced ISR entry with work queue integration
_el3_enhanced_isr_entry:
    push    ax
    push    dx
    
    ; Read interrupt status (SMC-patched)
    mov     dx, SMC_IOBASE
smc_iobase_patch1 equ $-2
    add     dx, 0x0E            ; Status register
    in      ax, dx
    
    ; Quick interrupt cause check
    test    ax, 0x8000          ; Check interrupt bit
    jz      .not_ours
    
    ; ACK interrupt
    out     dx, ax              ; Write back to clear
    
    ; Determine work type based on status
    test    ax, 0x4000          ; RX ready?
    jnz     .rx_work
    test    ax, 0x2000          ; TX complete?
    jnz     .tx_work
    
    ; Generic work - just set flag
    jmp     .set_work_flag
    
.rx_work:
    ; RX packet available - highest priority
    mov     dx, SMC_WORK_FLAG
smc_work_flag_patch equ $-2
    mov     byte [dx], 1        ; Set work pending
    
    ; Update stats
    inc     dword [enhanced_smc_stats + 4]  ; work_generated
    jmp     .eoi
    
.tx_work:
    ; TX completion - lower priority
    mov     dx, SMC_WORK_FLAG
    mov     byte [dx], 1
    jmp     .eoi
    
.set_work_flag:
    ; Generic work
    mov     dx, SMC_WORK_FLAG
    mov     byte [dx], 1
    
.eoi:
    ; EOI to PIC
    mov     al, 0x20
    out     0x20, al            ; Master PIC (slave handled by tiny ISR)
    
    ; Update ISR call count
    inc     dword [enhanced_smc_stats]
    
.not_ours:
    pop     dx
    pop     ax
    iret

; Enhanced TX path with interrupt coalescing and doorbell batching
_el3_enhanced_tx_entry:
    push    bp
    mov     bp, sp
    push    ax
    push    bx
    push    cx
    push    dx
    push    si
    push    di
    
    ; Get parameters: tx_buffer, length, device_id
    mov     si, [bp+4]      ; tx_buffer
    mov     cx, [bp+6]      ; length
    mov     bl, [bp+8]      ; device_id
    
    ; Copy-break decision for TX (SMC-patched threshold)
    cmp     cx, 192         ; Default threshold
smc_copy_threshold_patch equ $-2
    ja      .large_tx
    
    ; Small TX packet - copy to DMA buffer and use immediate PIO
    call    handle_small_tx
    inc     dword [enhanced_smc_stats + 8]   ; copy_break_small
    jmp     .tx_done
    
.large_tx:
    ; Large TX packet - use zero-copy DMA
    call    handle_large_tx_dma
    inc     dword [enhanced_smc_stats + 12]  ; copy_break_large
    
    ; Check interrupt coalescing
    mov     al, [tx_since_irq]      ; Global counter (should be per-device)
    inc     al
    cmp     al, 8                   ; K_PKTS threshold
smc_k_pkts_patch equ $-1
    jb      .no_interrupt
    
    ; Request interrupt on this packet
    or      word [di+FLAGS_OFFSET], 0x8000  ; TX_INT_BIT
    xor     al, al                  ; Reset counter
    inc     dword [enhanced_smc_stats + 16] ; interrupts_coalesced
    
.no_interrupt:
    mov     [tx_since_irq], al
    
    ; Doorbell batching
    inc     byte [pending_tx_ops]
    cmp     byte [pending_tx_ops], 4    ; Batch threshold
    jb      .skip_doorbell
    
    ; Send doorbell (SMC-patched register)
    mov     dx, SMC_DOORBELL_REG
smc_doorbell_patch equ $-2
    mov     ax, [tx_ring_phys_addr]     ; Physical ring address
    out     dx, ax
    
    mov     byte [pending_tx_ops], 0
    inc     dword [enhanced_smc_stats + 20] ; doorbells_batched
    
.skip_doorbell:
.tx_done:
    pop     di
    pop     si
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    pop     bp
    ret

; Enhanced RX path with copy-break and batch processing
_el3_enhanced_rx_entry:
    push    bp
    mov     bp, sp
    push    ax
    push    bx
    push    cx
    push    dx
    push    si
    push    di
    
    ; Check for available packets (SMC-optimized)
    mov     dx, SMC_IOBASE
smc_iobase_patch2 equ $-2
    add     dx, 0x18            ; RX status register
    in      ax, dx
    
    test    ax, 0x8000          ; Packet available?
    jz      .no_packets
    
    ; Get packet length
    and     ax, 0x1FFF          ; Length mask
    mov     cx, ax              ; Save length
    
    ; Copy-break decision
    cmp     cx, 192             ; Threshold (SMC-patched)
smc_copy_threshold_patch2 equ $-2
    ja      .large_rx
    
    ; Small packet - copy to UMB buffer
    call    copy_small_rx_packet
    inc     dword [enhanced_smc_stats + 8]   ; copy_break_small
    jmp     .rx_done
    
.large_rx:
    ; Large packet - zero-copy to application buffer
    call    handle_large_rx_zerocopy
    inc     dword [enhanced_smc_stats + 12]  ; copy_break_large
    
.rx_done:
    ; Schedule batch RX refill if needed
    dec     byte [rx_buffers_available]
    cmp     byte [rx_buffers_available], 4  ; Low watermark
    ja      .skip_refill
    
    call    schedule_batch_rx_refill
    
.skip_refill:
.no_packets:
    pop     di
    pop     si
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    pop     bp
    ret

; Window-optimized operations for Vortex NICs
; Minimizes window switching overhead
_vortex_window_optimized:
    push    ax
    push    dx
    
    ; Check current window state (SMC-tracked)
    mov     al, [SMC_WINDOW_STATE]
smc_window_state_patch equ $-1
    
    ; Target window in BL
    cmp     al, bl
    je      .same_window
    
    ; Switch window
    mov     dx, SMC_IOBASE
smc_iobase_patch3 equ $-2
    add     dx, 0x0E            ; Command register
    mov     ax, 0x0800
    or      al, bl              ; Select window command + window number
    out     dx, ax
    
    ; Update tracked state
    mov     [smc_window_state_patch], bl
    
.same_window:
    pop     dx
    pop     ax
    ret

; Data section for SMC state
section .data

; TX interrupt coalescing state
tx_since_irq        db 0
pending_tx_ops      db 0
rx_buffers_available db 16

; TX ring physical address (set during init)
tx_ring_phys_addr   dw 0

; Helper function implementations
section .text

; Handle small TX packet with immediate PIO
handle_small_tx:
    ; Implementation would copy to small buffer and use PIO
    ; This is a stub for the actual implementation
    ret

; Handle large TX with DMA
handle_large_tx_dma:
    ; Implementation would set up DMA descriptor
    ; This is a stub for the actual implementation
    ret

; Copy small RX packet to UMB buffer
copy_small_rx_packet:
    ; Implementation would copy packet data
    ; This is a stub for the actual implementation
    ret

; Handle large RX with zero-copy
handle_large_rx_zerocopy:
    ; Implementation would pass buffer pointer directly
    ; This is a stub for the actual implementation
    ret

; Schedule batch RX buffer refill
schedule_batch_rx_refill:
    ; Implementation would refill multiple RX descriptors at once
    ; This is a stub for the actual implementation
    ret

; SMC configuration structure interface
; Called from C code to set up tunable parameters
; void el3_set_smc_config(struct smc_config *config)
global _el3_set_smc_config
_el3_set_smc_config:
    push    bp
    mov     bp, sp
    push    si
    
    mov     si, [bp+4]          ; config pointer
    
    ; Update copy-break threshold
    mov     ax, [si+0]          ; config->copy_break_threshold
    mov     [smc_copy_threshold_patch], ax
    mov     [smc_copy_threshold_patch2], ax
    
    ; Update K_PKTS
    mov     al, [si+2]          ; config->k_pkts
    mov     [smc_k_pkts_patch], al
    
    ; Flush pipeline after changes
    FLUSH_PIPELINE
    
    pop     si
    pop     bp
    ret

; Get SMC statistics
; void el3_get_smc_stats(struct smc_stats *stats)
global _el3_get_smc_stats
_el3_get_smc_stats:
    push    bp
    mov     bp, sp
    push    si
    push    di
    push    cx
    
    mov     di, [bp+4]          ; stats pointer
    mov     si, enhanced_smc_stats
    mov     cx, 6               ; 6 dwords to copy
    
    cld
    rep     movsd               ; Copy stats structure
    
    pop     cx
    pop     di
    pop     si
    pop     bp
    ret
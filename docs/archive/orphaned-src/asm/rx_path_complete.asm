;-----------------------------------------------------------------------------
; @file rx_path_complete.asm
; @brief Complete RX path with copy-break and ISA optimizations
;
; Pure assembly RX implementation for 3C509B (PIO) and 3C515-TX (DMA)
; Features copy-break threshold optimization for small packets
;
; ISA Reality: Optimized for 5.55 MB/s with 192-byte copy-break
;-----------------------------------------------------------------------------

.MODEL SMALL
.386

include 'patch_macros.inc'

_TEXT SEGMENT
        ASSUME CS:_TEXT, DS:_DATA

;=============================================================================
; EXTERNAL REFERENCES
;=============================================================================

EXTERN vds_lock_buffer:PROC
EXTERN vds_unlock_buffer:PROC
EXTERN vds_get_physical_address:PROC
EXTERN deliver_packet:PROC          ; Packet driver API delivery
EXTERN cache_operation:PROC         ; For cache flush/invalidate

;=============================================================================
; CONSTANTS
;=============================================================================

; 3Com register offsets
RX_FIFO                 equ 00h     ; Window 1
RX_STATUS               equ 08h     ; Window 1
RX_BYTES_AVAIL          equ 0Ah     ; Window 1
COMMAND_REG             equ 0Eh
DMA_CTRL                equ 18h     ; Window 3

; RX Status bits
RX_INCOMPLETE           equ 8000h   ; Packet still arriving
RX_ERROR                equ 4000h   ; Error in packet

; Commands
CMD_RX_DISCARD          equ 4000h   ; Discard top RX packet
CMD_RX_ENABLE           equ 2800h   ; Enable receiver
CMD_RX_RESET            equ 2800h   ; Reset receiver

; DMA commands
DMA_RX_START            equ 0004h   ; Start RX DMA
DMA_RX_COMPLETE         equ 0008h   ; RX DMA complete

; Copy-break threshold (tunable)
COPY_BREAK_DEFAULT      equ 192     ; Optimal for cache lines
MAX_PACKET_SIZE         equ 1536    ; Ethernet maximum

;=============================================================================
; DATA SEGMENT
;=============================================================================

_DATA SEGMENT

; Device configuration
PUBLIC rx_device_type
PUBLIC rx_io_base
PUBLIC rx_use_dma
PUBLIC rx_copy_break

rx_device_type          dw 0        ; 0=3C509B, 1=3C515-TX
rx_io_base              dw 0        ; I/O base address
rx_use_dma              db 0        ; 1 if using DMA
rx_copy_break           dw 192      ; Copy-break threshold

; RX statistics
PUBLIC rx_packets_received
PUBLIC rx_bytes_received
PUBLIC rx_copy_break_count
PUBLIC rx_zero_copy_count
PUBLIC rx_errors

rx_packets_received     dd 0
rx_bytes_received       dd 0
rx_copy_break_count     dd 0        ; Small packets copied
rx_zero_copy_count      dd 0        ; Large packets zero-copy
rx_errors               dd 0

; RX ring buffers (3C515-TX DMA)
RX_RING_SIZE            equ 16      ; 16 descriptors
rx_ring_head            dw 0
rx_ring_tail            dw 0

; RX DMA descriptors
rx_dma_descriptors:
    REPT RX_RING_SIZE
        dd 0                        ; Next descriptor
        dd 0                        ; Status
        dd 0                        ; Physical address
        dd 0                        ; Length
    ENDM

; RX buffers
; Small buffer pool for copy-break (cache-line aligned)
ALIGN 32
rx_copy_buffers:
    db 8 * 256 dup(0)              ; 8 buffers of 256 bytes

; Large buffer pool for zero-copy
rx_large_buffers:
    db 4 * 1536 dup(0)             ; 4 full-size buffers

; Current packet being processed
rx_current_length       dw 0
rx_current_buffer       dw 0
rx_current_status       dw 0

_DATA ENDS

;=============================================================================
; RX PATH IMPLEMENTATION
;=============================================================================

;-----------------------------------------------------------------------------
; handle_rx_complete - Process RX interrupt (called from ISR bottom half)
;
; Input:  DX = I/O base
;         AX = interrupt status
;-----------------------------------------------------------------------------
PUBLIC handle_rx_complete
handle_rx_complete PROC
        push    bx
        push    cx
        push    si
        push    di
        push    es
        
        ; Select Window 1 for RX (GPT-5 optimized with caching)
        mov     al, 1                   ; Window 1
        cmp     al, [_current_windows + bx]
        je      .window_already_set
        push    bx
        shl     bx, 1                   ; Scale for word addressing
        mov     dx, [_cmd_ports + bx]   ; Precomputed command port
        shr     bx, 1                   ; Restore BX
        pop     bx
        mov     ax, 0800h + 1           ; CMD_SELECT_WINDOW (0x0800) + window 1
        out     dx, ax
        mov     [_current_windows + bx], 1
.window_already_set:
        
.rx_loop:
        ; Check RX status (use precomputed status port)
        push    bx
        shl     bx, 1                   ; Scale for word addressing
        mov     dx, [_status_ports + bx]
        shr     bx, 1                   ; Restore BX
        pop     bx
        in      ax, dx
        
        ; Check if packet ready
        test    ax, RX_INCOMPLETE
        jnz     .done                   ; Still arriving
        
        test    ax, RX_ERROR
        jnz     .rx_error
        
        ; Get packet length (lower 11 bits)
        and     ax, 07FFh
        mov     [rx_current_length], ax
        mov     cx, ax                  ; CX = length
        
        ; Decide copy-break vs zero-copy
        cmp     cx, [rx_copy_break]
        ja      .zero_copy_path
        
        ; === COPY-BREAK PATH (small packets) ===
        call    rx_copy_break_path
        jmp     .next_packet
        
.zero_copy_path:
        ; === ZERO-COPY PATH (large packets) ===
        cmp     [rx_use_dma], 0
        je      .pio_large
        call    rx_dma_path
        jmp     .next_packet
        
.pio_large:
        call    rx_pio_large_path
        
.next_packet:
        ; Check for more packets
        jmp     .rx_loop
        
.rx_error:
        inc     word ptr [rx_errors]
        adc     word ptr [rx_errors+2], 0
        
        ; Discard bad packet
        mov     dx, [rx_io_base]
        add     dx, COMMAND_REG
        mov     ax, CMD_RX_DISCARD
        out     dx, ax
        jmp     .rx_loop
        
.done:
        pop     es
        pop     di
        pop     si
        pop     cx
        pop     bx
        ret
handle_rx_complete ENDP

;-----------------------------------------------------------------------------
; rx_copy_break_path - Handle small packets with copy
;
; Optimized for cache efficiency - copies to aligned buffer
;-----------------------------------------------------------------------------
rx_copy_break_path PROC NEAR
        push    ax
        push    cx
        push    dx
        push    si
        push    di
        push    es
        
        ; Update statistics
        inc     word ptr [rx_copy_break_count]
        adc     word ptr [rx_copy_break_count+2], 0
        
        ; Get copy buffer (simple round-robin)
        mov     bx, [rx_current_buffer]
        inc     bx
        and     bx, 7                   ; 8 buffers
        mov     [rx_current_buffer], bx
        
        ; Calculate buffer address
        mov     ax, bx
        shl     ax, 8                   ; * 256
        add     ax, offset rx_copy_buffers
        mov     di, ax
        
        ; Setup for copy
        push    ds
        pop     es                      ; ES:DI = destination
        
        mov     dx, [rx_io_base]
        add     dx, RX_FIFO
        mov     cx, [rx_current_length]
        
        ; ISA optimization: Use 16-bit transfers
        push    cx                      ; Save original count
        shr     cx, 1                   ; Word count
        jz      .single_byte            ; Handle 1-byte packet
        
        ; Read from FIFO with REP INSW
        cld                             ; Forward direction
        rep     insw                    ; Maximum ISA efficiency
        
        ; Check for odd byte
        pop     cx                      ; Original count
        test    cx, 1                   ; Odd?
        jz      .even_bytes
        
        ; Read final odd byte
        in      al, dx                  ; Get last byte
        stosb                           ; Store it
        jmp     .even_bytes
        
.single_byte:
        pop     cx                      ; Restore count
        in      al, dx                  ; Single byte packet
        stosb
        
.even_bytes:
        ; Discard packet from NIC
        mov     dx, [rx_io_base]
        add     dx, COMMAND_REG
        mov     ax, CMD_RX_DISCARD
        out     dx, ax
        
        ; Deliver to packet driver
        push    ds
        pop     es
        mov     si, di
        sub     si, [rx_current_length]  ; Back to start
        mov     cx, [rx_current_length]
        call    deliver_packet
        
        ; Update statistics
        mov     cx, [rx_current_length]
        add     word ptr [rx_bytes_received], cx
        adc     word ptr [rx_bytes_received+2], 0
        inc     word ptr [rx_packets_received]
        adc     word ptr [rx_packets_received+2], 0
        
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     ax
        ret
rx_copy_break_path ENDP

;-----------------------------------------------------------------------------
; rx_pio_large_path - Handle large packets with PIO (3C509B)
;
; Zero-copy where possible, direct to application buffer
;-----------------------------------------------------------------------------
rx_pio_large_path PROC NEAR
        push    ax
        push    cx
        push    dx
        push    si
        push    di
        push    es
        
        ; Update statistics
        inc     word ptr [rx_zero_copy_count]
        adc     word ptr [rx_zero_copy_count+2], 0
        
        ; Allocate large buffer
        mov     di, offset rx_large_buffers
        push    ds
        pop     es
        
        ; Read packet with REP INSW (PIO - no cache issues)
        mov     dx, [rx_io_base]
        add     dx, RX_FIFO
        mov     cx, [rx_current_length]
        push    cx                      ; Save original count
        shr     cx, 1                   ; Word count
        jz      .single              ; Handle 1-byte packet
        
        cld                             ; Forward direction
        rep     insw                    ; Read words
        
        ; Check for odd byte
        pop     cx                      ; Original count
        test    cx, 1                   ; Odd?
        jz      .even
        
        ; Read final odd byte
        in      al, dx                  ; Get last byte
        stosb                           ; Store it
        jmp     .even
        
.single:
        pop     cx                      ; Restore count
        in      al, dx                  ; Single byte
        stosb
        
.even:
        ; Discard from NIC
        mov     dx, [rx_io_base]
        add     dx, COMMAND_REG
        mov     ax, CMD_RX_DISCARD
        out     dx, ax
        
        ; Deliver packet
        mov     si, offset rx_large_buffers
        mov     cx, [rx_current_length]
        push    ds
        pop     es
        call    deliver_packet
        
        ; Update statistics
        mov     cx, [rx_current_length]
        add     word ptr [rx_bytes_received], cx
        adc     word ptr [rx_bytes_received+2], 0
        inc     word ptr [rx_packets_received]
        adc     word ptr [rx_packets_received+2], 0
        
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     ax
        ret
rx_pio_large_path ENDP

;-----------------------------------------------------------------------------
; rx_dma_path - DMA receive for 3C515-TX
;
; True zero-copy with ring buffer management
;-----------------------------------------------------------------------------
rx_dma_path PROC NEAR
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        
        ; Update statistics
        inc     word ptr [rx_zero_copy_count]
        adc     word ptr [rx_zero_copy_count+2], 0
        
        ; Get current ring descriptor
        mov     bx, [rx_ring_head]
        mov     ax, bx
        shl     ax, 4                   ; * 16 bytes per descriptor
        add     ax, offset rx_dma_descriptors
        mov     si, ax
        
        ; Check descriptor status
        mov     eax, [si + 4]           ; Status field
        test    eax, 8000h              ; Owned by NIC?
        jnz     .no_packet
        
        ; Get packet from descriptor
        mov     cx, ax
        and     cx, 07FFh               ; Length in low 11 bits
        
        ; Get buffer address
        mov     eax, [si + 8]           ; Physical address
        
        ; === CRITICAL: Invalidate cache AFTER DMA writes to memory ===
        ; This ensures CPU sees fresh DMA data, not stale cache lines
        call    cache_operation         ; Invalidate stale cache lines
        
        ; VDS is already managing this buffer
        ; Now safe to deliver it
        push    si
        mov     si, ax                  ; Buffer address
        push    ds
        pop     es
        call    deliver_packet
        pop     si
        
        ; Return descriptor to NIC
        mov     dword ptr [si + 4], 8000h  ; Set owned bit
        
        ; Advance ring head
        inc     bx
        and     bx, RX_RING_SIZE - 1
        mov     [rx_ring_head], bx
        
        ; Update statistics
        add     word ptr [rx_bytes_received], cx
        adc     word ptr [rx_bytes_received+2], 0
        inc     word ptr [rx_packets_received]
        adc     word ptr [rx_packets_received+2], 0
        
.no_packet:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
rx_dma_path ENDP

;-----------------------------------------------------------------------------
; init_rx_ring - Initialize RX DMA ring (3C515-TX)
;-----------------------------------------------------------------------------
PUBLIC init_rx_ring
init_rx_ring PROC
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        
        ; Initialize each descriptor
        xor     bx, bx                  ; Descriptor index
        
.init_loop:
        ; Calculate descriptor address
        mov     ax, bx
        shl     ax, 4                   ; * 16
        add     ax, offset rx_dma_descriptors
        mov     si, ax
        
        ; Allocate and lock buffer with VDS
        push    bx
        mov     di, offset rx_large_buffers
        mov     ax, bx
        mov     cx, 1536
        mul     cx
        add     di, ax
        push    ds
        pop     es
        mov     cx, 1536                ; Buffer size
        call    vds_lock_buffer
        pop     bx
        
        ; Get physical address
        push    bx
        call    vds_get_physical_address
        pop     bx
        
        ; Fill descriptor
        mov     [si + 8], ax            ; Physical address low
        mov     [si + 10], dx           ; Physical address high
        mov     word ptr [si + 12], 1536 ; Buffer length
        mov     dword ptr [si + 4], 8000h ; Owned by NIC
        
        ; Link to next descriptor
        inc     bx
        cmp     bx, RX_RING_SIZE
        jae     .last_desc
        
        ; Calculate next descriptor physical address
        mov     ax, bx
        shl     ax, 4
        add     ax, offset rx_dma_descriptors
        mov     [si], ax                ; Next pointer
        jmp     .next_desc
        
.last_desc:
        ; Last descriptor points to first
        mov     word ptr [si], offset rx_dma_descriptors
        mov     bx, RX_RING_SIZE        ; Exit loop
        
.next_desc:
        cmp     bx, RX_RING_SIZE
        jb      .init_loop
        
        ; Program DMA controller with ring start
        mov     dx, [rx_io_base]
        add     dx, DMA_CTRL
        mov     eax, offset rx_dma_descriptors
        out     dx, eax
        
        ; Start RX DMA
        mov     ax, DMA_RX_START
        out     dx, ax
        
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
init_rx_ring ENDP

_TEXT ENDS
END
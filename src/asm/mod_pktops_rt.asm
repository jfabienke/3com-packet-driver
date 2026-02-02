; mod_pktops_rt.asm - NASM 16-bit JIT module for TX/RX packet operations and DMA completion handling
; Created: 2026-02-01 15:30:00
; CPU Target: 8086
; Purpose: Runtime packet operations with DMA completion tracking and ISR-safe queuing

        cpu 8086
        bits 16

%include "patch_macros.inc"

; ============================================================================
; CONSTANTS
; ============================================================================

TX_COMP_RING_SIZE   equ 32
TX_COMP_RING_MASK   equ 31
TX_COMP_ENTRY_SIZE  equ 6       ; seg:off:len (2+2+2)
VDS_UNLOCK_SIZE     equ 8
VDS_UNLOCK_MAX      equ 16
ETH_HLEN            equ 14      ; Ethernet header length
ETH_TYPE_OFF        equ 12      ; Ethertype offset in Ethernet frame
MIN_FRAME           equ 60      ; minimum Ethernet frame
MAX_FRAME           equ 1514    ; maximum Ethernet frame (no VLAN)

PATCH_COUNT         equ 0

; ============================================================================
; EXTERNAL FUNCTIONS
; ============================================================================

extern hardware_send_packet_
extern api_process_received_packet_
extern dma_map_tx_
extern dma_unmap_tx_
extern rx_batch_refill_
extern tx_lazy_should_interrupt_
extern tx_lazy_reclaim_batch_

; ============================================================================
; MODULE HEADER (64 bytes)
; ============================================================================

section .text class=MODULE

global _mod_pktops_rt_header
_mod_pktops_rt_header:
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
; EXPORTED FUNCTIONS
; ============================================================================

global packet_send_enhanced_
global packet_receive_from_nic_
global packet_receive_process_
global packet_queue_tx_completion_
global packet_process_tx_completions_
global packet_process_deferred_work_
global packet_isr_receive_
global packet_build_ethernet_frame_
global packet_parse_ethernet_header_
global packet_send_with_retry_
global packet_get_ethertype_
global packet_test_internal_loopback_

; ============================================================================
; DATA SECTION (Hot data - TX completion ring, VDS queue, stats)
; ============================================================================

section .data

align 2

; TX completion ring (SPSC: ISR writes head, main loop reads tail)
tx_comp_ring:
        times (TX_COMP_ENTRY_SIZE * TX_COMP_RING_SIZE) db 0  ; 192 bytes

tx_comp_head:
        dw 0                            ; written by ISR (CLI protected)

tx_comp_tail:
        dw 0                            ; read by main loop

; VDS unlock queue
vds_queue:
        times (VDS_UNLOCK_SIZE * VDS_UNLOCK_MAX) db 0  ; 128 bytes

vds_head:
        dw 0

vds_tail:
        dw 0

; Statistics
pktops_stats:
        times 32 db 0

; ============================================================================
; FUNCTION IMPLEMENTATIONS
; ============================================================================

section .text

; ----------------------------------------------------------------------------
; packet_send_enhanced_ - Enhanced packet send with DMA mapping
; Input: DX:AX = buffer far pointer
;        BX = length
;        CX = nic_index
; Returns: AX = 0 on success, -1 on failure
; ----------------------------------------------------------------------------
packet_send_enhanced_:
        push bp
        mov bp, sp
        push si
        push di
        push ds

        ; Save parameters
        push dx                         ; buffer segment
        push ax                         ; buffer offset
        push bx                         ; length
        push cx                         ; nic_index

        ; Validate length (MIN_FRAME <= len <= MAX_FRAME)
        cmp bx, MIN_FRAME
        jb .error                       ; too short
        cmp bx, MAX_FRAME
        ja .error                       ; too long

        ; Call dma_map_tx_(buffer_seg, buffer_off, length, nic_index)
        pop cx                          ; nic_index
        pop bx                          ; length
        pop ax                          ; buffer_off
        pop dx                          ; buffer_seg

        push cx                         ; save nic_index
        push bx                         ; save length
        push dx                         ; save buffer_seg
        push ax                         ; save buffer_off

        ; dma_map_tx_ call: DX:AX=buffer, BX=length, CX=nic_index
        call far dma_map_tx_
        or ax, ax                       ; check mapping result
        jnz .map_failed

        ; Mapping succeeded, DX:AX now contains mapped buffer
        pop si                          ; discard original buffer_off
        pop si                          ; discard original buffer_seg
        pop bx                          ; restore length
        pop cx                          ; restore nic_index

        push dx                         ; save mapped buffer seg
        push ax                         ; save mapped buffer off
        push bx                         ; save length

        ; Call hardware_send_packet_(mapped_buffer_seg:off, length, nic_index)
        ; DX:AX = buffer, BX = length, CX = nic_index
        call far hardware_send_packet_
        or ax, ax
        jnz .send_failed

        ; Success - queue TX completion
        pop bx                          ; length
        pop ax                          ; mapped buffer off
        pop dx                          ; mapped buffer seg

        ; Queue completion: DX:AX = mapping, BX = length
        call near queue_tx_comp_internal

        xor ax, ax                      ; return 0
        jmp .done

.send_failed:
        pop bx                          ; clean stack
        pop ax
        pop dx
        jmp .error

.map_failed:
        add sp, 8                       ; clean stack (4 pushes)

.error:
        mov ax, -1                      ; return -1

.done:
        pop ds
        pop di
        pop si
        pop bp
        retf

; ----------------------------------------------------------------------------
; packet_receive_from_nic_ - Receive packet from NIC
; Input: AX = nic_index
; Returns: AX = packet length or -1 on error
; ----------------------------------------------------------------------------
packet_receive_from_nic_:
        push bp
        mov bp, sp

        ; Simplified: just call api_process_received_packet_
        ; In real implementation, would read from NIC hardware
        call far api_process_received_packet_

        pop bp
        retf

; ----------------------------------------------------------------------------
; packet_receive_process_ - Process received packets
; Input: AX = nic_index
; Returns: AX = count of packets processed
; ----------------------------------------------------------------------------
packet_receive_process_:
        push bp
        mov bp, sp
        push bx

        mov bx, ax                      ; save nic_index
        xor ax, ax                      ; count = 0

        ; Call api_process_received_packet_ with nic_index
        mov ax, bx
        call far api_process_received_packet_

        ; Return count in AX (simplified to 1 or 0)
        or ax, ax
        jz .done
        mov ax, 1

.done:
        pop bx
        pop bp
        retf

; ----------------------------------------------------------------------------
; packet_queue_tx_completion_ - Queue TX completion (ISR-safe)
; Input: DX:AX = mapping far pointer
;        BX = length
; Returns: AX = 0 on success
; ----------------------------------------------------------------------------
packet_queue_tx_completion_:
        push bp
        mov bp, sp

        call near queue_tx_comp_internal

        xor ax, ax
        pop bp
        retf

; Internal helper for queuing TX completion
queue_tx_comp_internal:
        push si
        push di
        push es

        cli                             ; ISR-safe

        ; Get ring position
        mov si, [tx_comp_head]
        mov di, si

        ; Calculate entry address: tx_comp_ring + (head * TX_COMP_ENTRY_SIZE)
        mov cx, TX_COMP_ENTRY_SIZE
        mul cx                          ; AX = SI * 6 (destroyed AX, but we'll restore)
        mov si, ax
        add si, tx_comp_ring

        ; Write entry: seg:off:len (DX:AX:BX)
        mov [si], dx                    ; segment
        mov [si+2], ax                  ; offset
        mov [si+4], bx                  ; length

        ; Advance head
        mov ax, di
        inc ax
        and ax, TX_COMP_RING_MASK
        mov [tx_comp_head], ax

        sti

        pop es
        pop di
        pop si
        ret

; ----------------------------------------------------------------------------
; packet_process_tx_completions_ - Process TX completions
; Returns: AX = count processed
; ----------------------------------------------------------------------------
packet_process_tx_completions_:
        push bp
        mov bp, sp
        push si
        push di
        push bx
        push cx
        push dx

        xor cx, cx                      ; count = 0

.loop:
        ; Check if ring is empty
        mov ax, [tx_comp_tail]
        cmp ax, [tx_comp_head]
        je .done

        ; Calculate entry address
        mov si, ax
        mov di, TX_COMP_ENTRY_SIZE
        mul di                          ; AX = tail * 6
        mov si, ax
        add si, tx_comp_ring

        ; Read entry
        mov dx, [si]                    ; segment
        mov ax, [si+2]                  ; offset
        mov bx, [si+4]                  ; length

        ; Call dma_unmap_tx_(DX:AX = mapping, BX = length)
        push cx
        call far dma_unmap_tx_
        pop cx

        ; Advance tail
        mov ax, [tx_comp_tail]
        inc ax
        and ax, TX_COMP_RING_MASK
        mov [tx_comp_tail], ax

        inc cx                          ; increment count
        jmp .loop

.done:
        mov ax, cx                      ; return count

        pop dx
        pop cx
        pop bx
        pop di
        pop si
        pop bp
        retf

; ----------------------------------------------------------------------------
; packet_process_deferred_work_ - Process deferred work (TX completions, RX refill)
; Returns: void
; ----------------------------------------------------------------------------
packet_process_deferred_work_:
        push bp
        mov bp, sp
        push ax
        push cx

        ; Process TX completions
        call far packet_process_tx_completions_

        ; Call rx_batch_refill_ for each active NIC (simplified: just NIC 0)
        xor ax, ax                      ; nic_index = 0
        call far rx_batch_refill_

        pop cx
        pop ax
        pop bp
        retf

; ----------------------------------------------------------------------------
; packet_isr_receive_ - ISR-safe receive handler
; Input: AX = nic_index
; Returns: AX = 0/1
; ----------------------------------------------------------------------------
packet_isr_receive_:
        push bp
        mov bp, sp

        ; Minimal ISR handling - queue for deferred processing
        ; For now, just return success
        mov ax, 1

        pop bp
        retf

; ----------------------------------------------------------------------------
; packet_build_ethernet_frame_ - Build Ethernet frame with header
; Input: DX:AX = dest buffer far pointer
;        BX = src buffer pointer (near)
;        CX = length
; Returns: AX = total length or -1 on error
; ----------------------------------------------------------------------------
packet_build_ethernet_frame_:
        push bp
        mov bp, sp
        push si
        push di
        push ds
        push es

        ; Validate length
        cmp cx, MAX_FRAME
        ja .error

        ; Setup ES:DI = dest, DS:SI = src
        mov es, dx
        mov di, ax

        push ds
        pop es                          ; ES = DS for source
        mov si, bx

        ; Copy ethernet header (14 bytes)
        mov cx, ETH_HLEN
        rep movsb

        ; Total length = header + payload
        mov ax, cx
        add ax, ETH_HLEN
        jmp .done

.error:
        mov ax, -1

.done:
        pop es
        pop ds
        pop di
        pop si
        pop bp
        retf

; ----------------------------------------------------------------------------
; packet_parse_ethernet_header_ - Parse Ethernet header
; Input: DX:AX = packet far pointer
;        BX = length
; Returns: AX = ethertype or -1 if too short
; ----------------------------------------------------------------------------
packet_parse_ethernet_header_:
        push bp
        mov bp, sp
        push ds
        push si

        ; Check minimum length
        cmp bx, ETH_HLEN
        jb .error

        ; Setup DS:SI = packet
        mov ds, dx
        mov si, ax

        ; Read ethertype at offset 12
        mov ax, [si + ETH_TYPE_OFF]
        jmp .done

.error:
        mov ax, -1

.done:
        pop si
        pop ds
        pop bp
        retf

; ----------------------------------------------------------------------------
; packet_send_with_retry_ - Send packet with one retry on failure
; Input: DX:AX = buffer far pointer
;        BX = length
;        CX = nic_index
; Returns: AX = 0 on success, -1 on failure
; ----------------------------------------------------------------------------
packet_send_with_retry_:
        push bp
        mov bp, sp
        push dx
        push bx
        push cx

        ; First attempt
        call far packet_send_enhanced_
        or ax, ax
        jz .success

        ; Retry once
        pop cx
        pop bx
        pop dx
        push dx
        push bx
        push cx

        call far packet_send_enhanced_

.success:
        pop cx
        pop bx
        pop dx
        pop bp
        retf

; ----------------------------------------------------------------------------
; packet_get_ethertype_ - Get ethertype from packet
; Input: DX:AX = packet far pointer
; Returns: AX = ethertype
; ----------------------------------------------------------------------------
packet_get_ethertype_:
        push bp
        mov bp, sp
        push ds
        push si

        ; Setup DS:SI = packet
        mov ds, dx
        mov si, ax

        ; Read word at offset ETH_TYPE_OFF
        mov ax, [si + ETH_TYPE_OFF]

        pop si
        pop ds
        pop bp
        retf

; ----------------------------------------------------------------------------
; packet_test_internal_loopback_ - Test internal loopback (stub)
; Returns: AX = -1 (not supported)
; ----------------------------------------------------------------------------
packet_test_internal_loopback_:
        push bp
        mov bp, sp

        mov ax, -1                      ; not supported

        pop bp
        retf

; ============================================================================
; END OF MODULE
; ============================================================================

hot_end:

patch_table:

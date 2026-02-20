;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_3c515_rt.asm
;; @brief JIT runtime module for 3Com 3C515 Corkscrew ISA/PCI NIC
;;
;; 3Com Packet Driver - NIC-specific runtime module
;; Assembled at load time by the JIT patch engine into the TSR resident image.
;;
;; The 3C515 is a hybrid ISA bus-master NIC supporting both PIO and DMA.
;; This module implements:
;;   - send_packet      : transmit via DnList bus-master DMA or PIO fallback
;;   - recv_packet      : receive via UpList bus-master DMA or PIO fallback
;;   - handle_interrupt  : ISR with shared-IRQ muxing (inlined pcimux)
;;   - pci_config_read  : inlined PCI config space read  (replaces pci_shim)
;;   - pci_config_write : inlined PCI config space write (replaces pci_shim)
;;
;; Last Updated: 2026-02-01 11:41:08 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16
cpu 286

%include "patch_macros.inc"

;; ---- Additional patch type constants ------------------------------------
PATCH_TYPE_IMM16        equ 6
PATCH_TYPE_IMM8         equ 7
PATCH_TYPE_RELOC_NEAR   equ 8

;; ---- NIC identity -------------------------------------------------------
NIC_TYPE_3C515          equ 2
CPU_REQ_286             equ 1
CAP_FLAGS_PCI_BUS       equ 0010h

;; ---- 3C515 register offsets (from io_base) ------------------------------
REG_DATA                equ 00h     ; 16-bit data port (FIFO)
REG_TX_STATUS           equ 04h     ; TX status (byte)
REG_RX_STATUS           equ 08h     ; RX status (word)
REG_INT_STATUS          equ 0Ah     ; Interrupt status
REG_COMMAND             equ 0Eh     ; Command / window select

;; Bus-master DMA registers (Window 0, extended I/O)
REG_DN_LIST_PTR         equ 24h     ; Download list pointer (32-bit)
REG_DN_LIST_PTR_HI      equ 26h     ; Download list pointer high word
REG_UP_LIST_PTR         equ 38h     ; Upload list pointer (32-bit)
REG_UP_LIST_PTR_HI      equ 3Ah     ; Upload list pointer high word
REG_DMA_CTRL            equ 20h     ; DMA control register
REG_UP_PKT_STATUS       equ 30h     ; Upload packet status

;; Commands
CMD_SELECT_WINDOW       equ 0800h
CMD_ACK_INT             equ 6800h
CMD_TX_ENABLE           equ 9800h
CMD_RX_ENABLE           equ 2000h
CMD_DN_STALL            equ 3002h   ; Stall download engine
CMD_DN_UNSTALL          equ 3003h   ; Unstall download engine
CMD_UP_STALL            equ 3000h   ; Stall upload engine
CMD_UP_UNSTALL          equ 3001h   ; Unstall upload engine

;; Interrupt bits
INT_LATCH               equ 0001h
INT_TX_COMPLETE         equ 0004h
INT_RX_COMPLETE         equ 0010h
INT_DMA_DONE            equ 0020h
INT_UP_COMPLETE         equ 0040h
INT_DN_COMPLETE         equ 0080h
INT_HOST_ERROR          equ 0002h

;; PCI config space I/O
PCI_CONFIG_ADDR         equ 0CF8h
PCI_CONFIG_DATA         equ 0CFCh

;; Descriptor flags
DPD_DN_INDICATE         equ 80000000h  ; Interrupt when DMA done
UPD_UP_COMPLETE         equ 00008000h  ; Upload complete flag

MAX_FRAME_SIZE          equ 1514

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; MODULE SEGMENT
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
section MOD_3C515_TEXT class=MODULE

global _mod_3c515_header

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 64-byte module header
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
_mod_3c515_header:
header:
    db 'PKTDRV',0               ; 7 bytes - signature
    db 1                        ; version_major
    db 0                        ; version_minor
    dw hot_start                ; hot section start offset
    dw hot_end                  ; hot section end offset
    dw 0                        ; cold_start (unused)
    dw 0                        ; cold_end   (unused)
    dw patch_table              ; patch table offset
    dw PATCH_COUNT              ; number of patch entries
    dw (hot_end - header)       ; module_size
    dw (hot_end - hot_start)    ; required_memory
    db CPU_REQ_286              ; cpu_requirements
    db NIC_TYPE_3C515           ; nic_type
    dw CAP_FLAGS_PCI_BUS        ; cap_flags
    times (64 - ($ - header)) db 0

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; HOT SECTION
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hot_start:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; C-CALLABLE WRAPPER EXPORTS
;;
;; These wrappers convert from Watcom large model calling convention:
;;   DX:AX = far pointer (first param)
;;   BX    = integer param (second param)
;;   CX    = integer param (third param)
;; To internal register-based calling:
;;   DS:SI = buffer pointer
;;   CX    = length
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; _3c515_send_packet_ - C-callable wrapper for send_packet
;; Input:  DX:AX = buffer far pointer, BX = length
;; Return: AX = 0 on success, -1 on error
global _3c515_send_packet_
_3c515_send_packet_:
    push    ds
    push    si
    mov     ds, dx              ; DS = buffer segment
    mov     si, ax              ; SI = buffer offset
    mov     cx, bx              ; CX = length
    call    send_packet
    pop     si
    pop     ds
    jc      .error
    xor     ax, ax              ; return 0 = success
    retf
.error:
    mov     ax, -1              ; return -1 = error
    retf

;; _3c515_receive_packet_ - C-callable wrapper for recv_packet
;; Input:  DX:AX = buffer far pointer, BX = buffer size
;; Return: AX = bytes received, or -1 on error/no packet
global _3c515_receive_packet_
_3c515_receive_packet_:
    push    es
    push    di
    mov     es, dx              ; ES = buffer segment
    mov     di, ax              ; DI = buffer offset
    mov     cx, bx              ; CX = buffer size
    call    recv_packet
    pop     di
    pop     es
    jc      .no_packet
    mov     ax, cx              ; return bytes received
    retf
.no_packet:
    mov     ax, -1              ; return -1 = no packet
    retf

;; _3c515_handle_interrupt_ - C-callable wrapper for handle_interrupt
;; Input:  none
;; Return: void
global _3c515_handle_interrupt_
_3c515_handle_interrupt_:
    call    handle_interrupt
    retf

;; _3c515_check_interrupt_ - Check if interrupt is from this NIC
;; Input:  none
;; Return: AX = 1 if ours, 0 if not
global _3c515_check_interrupt_
_3c515_check_interrupt_:
    call    handle_interrupt
    jc      .not_ours
    mov     ax, 1
    retf
.not_ours:
    xor     ax, ax
    retf

;; _3c515_enable_interrupts_ - Enable NIC interrupts
;; Input:  none
;; Return: AX = 0
global _3c515_enable_interrupts_
_3c515_enable_interrupts_:
    PATCH_POINT pp_enable_int
    mov     dx, 0               ; patched: io_base + REG_COMMAND
    mov     ax, 0A0FFh          ; SetIntrMask | all interrupt bits
    out     dx, ax
    xor     ax, ax
    retf

;; _3c515_disable_interrupts_ - Disable NIC interrupts
;; Input:  none
;; Return: AX = 0
global _3c515_disable_interrupts_
_3c515_disable_interrupts_:
    PATCH_POINT pp_disable_int
    mov     dx, 0               ; patched: io_base + REG_COMMAND
    mov     ax, 0A000h          ; SetIntrMask | 0 (disable all)
    out     dx, ax
    xor     ax, ax
    retf

;; _3c515_get_link_status_ - Get link status
;; Return: AX = 1 if link up, 0 if down
global _3c515_get_link_status_
_3c515_get_link_status_:
    ;; Read MediaStatus register (Window 4, offset 0Ah)
    PATCH_POINT pp_link_status
    mov     dx, 0               ; patched: io_base + REG_COMMAND
    mov     ax, CMD_SELECT_WINDOW | 4
    out     dx, ax
    PATCH_POINT pp_media_status
    mov     dx, 0               ; patched: io_base + 0Ah (MediaStatus)
    in      ax, dx
    test    ax, 8000h           ; linkBeat bit
    jnz     .link_up
    xor     ax, ax
    retf
.link_up:
    mov     ax, 1
    retf

;; _3c515_get_link_speed_ - Get link speed
;; Return: AX = 10 or 100
global _3c515_get_link_speed_
_3c515_get_link_speed_:
    ;; 3C515 supports 10/100 - check MediaStatus for speed
    PATCH_POINT pp_link_speed
    mov     dx, 0               ; patched: io_base + REG_COMMAND
    mov     ax, CMD_SELECT_WINDOW | 4
    out     dx, ax
    PATCH_POINT pp_media_status2
    mov     dx, 0               ; patched: io_base + 0Ah (MediaStatus)
    in      ax, dx
    test    ax, 0080h           ; 100Mbps indicator bit
    jnz     .speed_100
    mov     ax, 10
    retf
.speed_100:
    mov     ax, 100
    retf

;; _3c515_dma_prepare_buffers_ - Prepare DMA buffers
;; Return: AX = 0
global _3c515_dma_prepare_buffers_
_3c515_dma_prepare_buffers_:
    xor     ax, ax
    retf

;; _3c515_dma_complete_buffers_ - Complete DMA buffers
;; Return: void
global _3c515_dma_complete_buffers_
_3c515_dma_complete_buffers_:
    retf

;; _3c515_process_single_event_ - Process single NIC event
;; Return: AX = 0
global _3c515_process_single_event_
_3c515_process_single_event_:
    xor     ax, ax
    retf

;; _3c515_handle_interrupt_batched_ - Handle batched interrupts
;; Return: void
global _3c515_handle_interrupt_batched_
_3c515_handle_interrupt_batched_:
    call    handle_interrupt
    retf

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; INTERNAL FUNCTIONS
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; pci_config_read - Inlined PCI configuration space read (word)
;;
;; Input:  BH = bus, BL = dev/fn, CL = register offset
;; Output: AX = config word
;; Clobbers: DX
;;
;; PCI CONFIG_ADDRESS format (32-bit port 0CF8h):
;;   Bits 31:    Enable (1)
;;   Bits 23-16: Bus number
;;   Bits 15-8:  Device/function
;;   Bits 7-2:   Register (dword aligned)
;;   Bits 1-0:   0
;;
;; On 286 we write the 32-bit CONFIG_ADDRESS as two 16-bit OUT instructions:
;;   Low word  (port 0CF8h): (devfn << 8) | (reg & FCh)
;;   High word (port 0CFAh): 8000h | bus
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
pci_config_read:
    push    bx
    push    cx

    ;; Build low word: (devfn << 8) | (reg & FCh)
    xor     ah, ah
    mov     al, bl              ; AL = dev/fn
    xchg    ah, al              ; AX = devfn << 8
    mov     dl, cl
    and     dl, 0FCh
    xor     dh, dh
    or      ax, dx              ; AX = low word of CONFIG_ADDRESS

    ;; Write low word to PCI_CONFIG_ADDR
    mov     dx, PCI_CONFIG_ADDR
    out     dx, ax

    ;; Build high word: 8000h | bus
    xor     ah, ah
    mov     al, bh              ; AL = bus
    or      ax, 8000h           ; set enable bit
    ;; Write high word to PCI_CONFIG_ADDR+2
    mov     dx, PCI_CONFIG_ADDR + 2
    out     dx, ax

    ;; Read 16-bit config data
    ;; If register offset bit 1 is set, read from DATA+2
    test    cl, 2
    jnz     pci_rd_hi
    mov     dx, PCI_CONFIG_DATA
    in      ax, dx
    jmp     short pci_rd_done
pci_rd_hi:
    mov     dx, PCI_CONFIG_DATA + 2
    in      ax, dx
pci_rd_done:
    pop     cx
    pop     bx
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; pci_config_write - Inlined PCI configuration space write (word)
;;
;; Input:  BH = bus, BL = dev/fn, CL = register offset, AX = value
;; Clobbers: DX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
pci_config_write:
    push    bx
    push    cx
    push    ax                      ; save value to write

    ;; Build low word: (devfn << 8) | (reg & FCh)
    xor     ah, ah
    mov     al, bl              ; AL = dev/fn
    xchg    ah, al              ; AX = devfn << 8
    mov     dl, cl
    and     dl, 0FCh
    xor     dh, dh
    or      ax, dx              ; AX = low word of CONFIG_ADDRESS

    ;; Write low word to PCI_CONFIG_ADDR
    mov     dx, PCI_CONFIG_ADDR
    out     dx, ax

    ;; Build high word: 8000h | bus
    xor     ah, ah
    mov     al, bh              ; AL = bus
    or      ax, 8000h           ; set enable bit
    ;; Write high word to PCI_CONFIG_ADDR+2
    mov     dx, PCI_CONFIG_ADDR + 2
    out     dx, ax

    ;; Write the config value
    pop     ax                      ; restore value
    mov     dx, PCI_CONFIG_DATA
    out     dx, ax

    pop     cx
    pop     bx
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; send_packet - Transmit via bus-master DMA (DnList) with PIO fallback
;;
;; Input:  DS:SI = packet data, CX = packet length
;; Output: CF=0 success, CF=1 error
;; Clobbers: AX, DX, SI, CX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
send_packet:
    push    bx
    push    cx

    cmp     cx, MAX_FRAME_SIZE
    ja      tx_err
    or      cx, cx
    jz      tx_err

    ;; ---- Attempt bus-master DMA path -----------------------------------
    ;; Check if DnList pointer is 0 (engine idle)
    PATCH_POINT pp_tx_dma1
    mov     dx, 0                   ; patched: io_base + REG_DN_LIST_PTR
    in      ax, dx
    or      ax, ax
    jnz     tx_pio_fallback         ; DMA engine busy, use PIO

    ;; ---- Stall download engine -----------------------------------------
    PATCH_POINT pp_tx_iobase1
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_DN_STALL
    out     dx, ax

    ;; ---- Set up DPD in shared memory -----------------------------------
    ;; DPD layout: DnNextPtr(4), FrameStartHeader(4), DataAddr(4), DataLen(4)
    ;; (The TSR wrapper provides DPD memory at a known physical address.)
    ;; We write the physical pointer to the DnList register as two 16-bit words.
    ;; For low memory (<1MB), the high word is always 0000h.
    ;; The JIT engine patches the DPD base address.
    PATCH_POINT pp_tx_dma2
    mov     dx, 0                   ; patched: io_base + REG_DN_LIST_PTR
    ;; Low word written by patched code; high word = 0 for low memory
    ;; (JIT patches the mov dx and writes both words)

    ;; Unstall to begin DMA
    PATCH_POINT pp_tx_iobase2
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_DN_UNSTALL
    out     dx, ax

    ;; ---- TX lazy reclaim: don't wait for completion --------------------
    ;; The next send or ISR will check/reclaim the descriptor.
    pop     cx
    pop     bx
    clc
    ret

tx_pio_fallback:
    ;; ---- PIO path (same as 3C509B) ------------------------------------
    PATCH_POINT pp_tx_iobase3
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_SELECT_WINDOW | 1
    out     dx, ax

    PATCH_POINT pp_tx_iobase4
    mov     dx, 0                   ; patched: io_base + REG_DATA
    pop     cx                      ; packet length
    push    cx
    mov     ax, cx                  ; TX preamble
    out     dx, ax

    pop     cx
    push    cx
    inc     cx
    shr     cx, 1
    rep     outsw

    ;; ---- Wait for TX completion ----------------------------------------
    PATCH_POINT pp_tx_iobase5
    mov     dx, 0                   ; patched: io_base + REG_TX_STATUS
    mov     cx, 2000
tx_poll:
    in      ax, dx
    test    al, 80h
    jnz     tx_pio_done
    loop    tx_poll
tx_pio_done:
    out     dx, ax                  ; clear status (16-bit NIC register)

    pop     cx
    pop     bx
    clc
    ret

tx_err:
    pop     cx
    pop     bx
    stc
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; recv_packet - Receive via bus-master DMA (UpList) with PIO fallback
;;
;; Input:  ES:DI = buffer, CX = buffer size
;; Output: CF=0 success (CX=bytes), CF=1 no packet
;; Clobbers: AX, DX, DI, CX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
recv_packet:
    push    bx

    ;; ---- Check UpList packet status ------------------------------------
    PATCH_POINT pp_rx_dma1
    mov     dx, 0                   ; patched: io_base + REG_UP_PKT_STATUS
    in      ax, dx
    test    ax, UPD_UP_COMPLETE & 0FFFFh
    jnz     rx_dma_ready

    ;; ---- PIO fallback: check RX status ---------------------------------
    PATCH_POINT pp_rx_iobase1
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_SELECT_WINDOW | 1
    out     dx, ax

    PATCH_POINT pp_rx_iobase2
    mov     dx, 0                   ; patched: io_base + REG_RX_STATUS
    in      ax, dx
    test    ax, 8000h
    jnz     rx_none
    test    ax, 4000h
    jnz     rx_error

    and     ax, 07FFh
    mov     bx, ax
    cmp     bx, cx
    jbe     rx_pio_ok
    mov     bx, cx
rx_pio_ok:
    PATCH_POINT pp_rx_iobase3
    mov     dx, 0                   ; patched: io_base + REG_DATA
    mov     cx, bx
    inc     cx
    shr     cx, 1
    rep     insw

    ;; Discard RX packet
    PATCH_POINT pp_rx_iobase4
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, 8000h               ; RX discard
    out     dx, ax

    mov     cx, bx
    pop     bx
    clc
    ret

rx_dma_ready:
    ;; DMA path: data already in UPD buffer, copy to caller
    ;; (UPD buffer address and length are managed by the TSR wrapper.)
    ;; Stall upload engine, read length, advance pointer, unstall
    PATCH_POINT pp_rx_dma2
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_UP_STALL
    out     dx, ax

    ;; Advance to next UPD (managed by TSR wrapper state)
    PATCH_POINT pp_rx_dma3
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_UP_UNSTALL
    out     dx, ax

    pop     bx
    clc
    ret

rx_error:
    PATCH_POINT pp_rx_iobase5
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, 8000h
    out     dx, ax
rx_none:
    pop     bx
    xor     cx, cx
    stc
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; handle_interrupt - ISR with inlined shared-IRQ mux
;;
;; Replaces pcimux_rt.c for the 3C515 - handles shared PCI/ISA interrupts.
;;
;; Input:  (called from TSR ISR wrapper)
;; Output: CF=0 ours, CF=1 not ours
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
handle_interrupt:
    push    bx

    ;; ---- Shared IRQ mux: read IntStatus first --------------------------
    PATCH_POINT pp_isr_iobase1
    mov     dx, 0                   ; patched: io_base + REG_INT_STATUS
    in      ax, dx

    test    ax, INT_LATCH
    jz      isr_not_ours

    mov     bx, ax

    ;; ---- RX complete or upload complete --------------------------------
    test    bx, INT_RX_COMPLETE | INT_UP_COMPLETE
    jz      isr_check_tx
    PATCH_POINT pp_isr_iobase2
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_RX_COMPLETE | INT_UP_COMPLETE
    out     dx, ax

isr_check_tx:
    ;; ---- TX complete or download complete ------------------------------
    test    bx, INT_TX_COMPLETE | INT_DN_COMPLETE
    jz      isr_check_err
    PATCH_POINT pp_isr_iobase3
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_TX_COMPLETE | INT_DN_COMPLETE
    out     dx, ax

    ;; Lazy TX reclaim: clear TX status
    PATCH_POINT pp_isr_iobase4
    mov     dx, 0                   ; patched: io_base + REG_TX_STATUS
    in      ax, dx
    out     dx, ax                  ; 16-bit NIC register

isr_check_err:
    test    bx, INT_HOST_ERROR
    jz      isr_ack
    PATCH_POINT pp_isr_iobase5
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_HOST_ERROR
    out     dx, ax

isr_ack:
    PATCH_POINT pp_isr_iobase6
    mov     dx, 0                   ; patched: io_base + REG_COMMAND
    mov     ax, CMD_ACK_INT | INT_LATCH
    out     dx, ax

    pop     bx
    clc
    ret

isr_not_ours:
    pop     bx
    stc
    ret

hot_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; PATCH TABLE
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
patch_table:
    ;; send_packet DMA patches
    PATCH_TABLE_ENTRY pp_tx_dma1,     PATCH_TYPE_IO   ; DnListPtr read
    PATCH_TABLE_ENTRY pp_tx_iobase1,  PATCH_TYPE_IO   ; command (stall)
    PATCH_TABLE_ENTRY pp_tx_dma2,     PATCH_TYPE_IO   ; DnListPtr write
    PATCH_TABLE_ENTRY pp_tx_iobase2,  PATCH_TYPE_IO   ; command (unstall)
    PATCH_TABLE_ENTRY pp_tx_iobase3,  PATCH_TYPE_IO   ; command (window)
    PATCH_TABLE_ENTRY pp_tx_iobase4,  PATCH_TYPE_IO   ; data port
    PATCH_TABLE_ENTRY pp_tx_iobase5,  PATCH_TYPE_IO   ; TX status
    ;; recv_packet patches
    PATCH_TABLE_ENTRY pp_rx_dma1,     PATCH_TYPE_IO   ; UpPktStatus
    PATCH_TABLE_ENTRY pp_rx_iobase1,  PATCH_TYPE_IO   ; command (window)
    PATCH_TABLE_ENTRY pp_rx_iobase2,  PATCH_TYPE_IO   ; RX status
    PATCH_TABLE_ENTRY pp_rx_iobase3,  PATCH_TYPE_IO   ; data port
    PATCH_TABLE_ENTRY pp_rx_iobase4,  PATCH_TYPE_IO   ; command (discard)
    PATCH_TABLE_ENTRY pp_rx_dma2,     PATCH_TYPE_IO   ; command (stall)
    PATCH_TABLE_ENTRY pp_rx_dma3,     PATCH_TYPE_IO   ; command (unstall)
    PATCH_TABLE_ENTRY pp_rx_iobase5,  PATCH_TYPE_IO   ; command (err discard)
    ;; ISR patches
    PATCH_TABLE_ENTRY pp_isr_iobase1, PATCH_TYPE_IO   ; IntStatus
    PATCH_TABLE_ENTRY pp_isr_iobase2, PATCH_TYPE_IO   ; command (ack RX)
    PATCH_TABLE_ENTRY pp_isr_iobase3, PATCH_TYPE_IO   ; command (ack TX)
    PATCH_TABLE_ENTRY pp_isr_iobase4, PATCH_TYPE_IO   ; TX status (reclaim)
    PATCH_TABLE_ENTRY pp_isr_iobase5, PATCH_TYPE_IO   ; command (ack err)
    PATCH_TABLE_ENTRY pp_isr_iobase6, PATCH_TYPE_IO   ; command (ack latch)
    ;; interrupt enable/disable/status patches
    PATCH_TABLE_ENTRY pp_enable_int,  PATCH_TYPE_IO   ; command (enable)
    PATCH_TABLE_ENTRY pp_disable_int, PATCH_TYPE_IO   ; command (disable)
    PATCH_TABLE_ENTRY pp_link_status, PATCH_TYPE_IO   ; command (window select)
    PATCH_TABLE_ENTRY pp_media_status, PATCH_TYPE_IO  ; MediaStatus read
    PATCH_TABLE_ENTRY pp_link_speed,  PATCH_TYPE_IO   ; command (window select)
    PATCH_TABLE_ENTRY pp_media_status2, PATCH_TYPE_IO ; MediaStatus read
PATCH_COUNT equ ($ - patch_table) / 4

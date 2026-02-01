;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_dma_isa.asm
;; @brief ISA DMA channel setup and transfer module
;;
;; JIT-assembled runtime module for 16-bit DOS packet driver TSR.
;; Programs the 8237 DMA controller for ISA DMA transfers.
;; Handles both 8-bit channels (0-3) and 16-bit channels (5-7).
;;
;; Last Updated: 2026-02-01 11:02:20 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16

%include "patch_macros.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Constants
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
CPU_REQ         equ 0               ; 8086 baseline
CAP_FLAGS       equ 0x0001          ; ISA_DMA capability
PATCH_COUNT     equ 2               ; DMA channel, page register port

;; 8237 DMA controller ports (8-bit channels 0-3)
DMA1_ADDR_BASE  equ 0x00           ; Channel 0 address at 0x00, +2 per chan
DMA1_COUNT_BASE equ 0x01           ; Channel 0 count at 0x01, +2 per chan
DMA1_STATUS     equ 0x08           ; Status register
DMA1_COMMAND    equ 0x08           ; Command register (write)
DMA1_REQUEST    equ 0x09           ; Request register
DMA1_MASK       equ 0x0A           ; Single mask register
DMA1_MODE       equ 0x0B           ; Mode register
DMA1_FLIPFLOP   equ 0x0C          ; Clear byte pointer flip-flop

;; 8237 DMA controller ports (16-bit channels 5-7)
DMA2_ADDR_BASE  equ 0xC0           ; Channel 4 address at 0xC0, +4 per chan
DMA2_COUNT_BASE equ 0xC2           ; Channel 4 count at 0xC2, +4 per chan
DMA2_STATUS     equ 0xD0           ; Status register
DMA2_MASK       equ 0xD4           ; Single mask register
DMA2_MODE       equ 0xD6           ; Mode register
DMA2_FLIPFLOP   equ 0xD8          ; Clear byte pointer flip-flop

;; Page register ports indexed by DMA channel (0-7)
;; Channel:  0     1     2     3     -     5     6     7
;; Port:    87h   83h   81h   82h   --    8Bh   89h   8Ah

;; DMA mode bits
DMA_MODE_READ   equ 0x44           ; Single mode, read (device->memory), autoinit off
DMA_MODE_WRITE  equ 0x48           ; Single mode, write (memory->device), autoinit off

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module segment
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
section .text class=MODULE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 64-byte module header
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _mod_dma_isa_header
_mod_dma_isa_header:
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
;; Hot section - DMA controller programming routines
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hot_start:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; dma_setup_transfer - Program 8237 DMA controller for a transfer
;;
;; Input:
;;   AL = DMA mode (DMA_MODE_READ or DMA_MODE_WRITE)
;;   BX = physical address bits [15:0] (or [16:1] for 16-bit channels)
;;   DL = page register value (physical address bits [23:16])
;;   CX = transfer byte count minus 1
;; Output:
;;   None (DMA controller programmed, channel still masked)
;; Clobbers: AX, DX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
dma_setup_transfer:
    push    bx
    push    cx

    ;; Get DMA channel number (patched at runtime)
    PATCH_POINT pp_dma_channel
    mov     ah, 0x00                ; Placeholder: patched to DMA channel (IMM8)

    ;; Combine mode with channel number
    or      al, ah                  ; AL = mode | channel

    ;; Mask the DMA channel before programming
    push    ax
    mov     al, ah
    or      al, 0x04               ; Set mask bit
    cmp     ah, 4
    jae     dma_setup_16bit

    ;; --- 8-bit channel (0-3) setup ---
dma_setup_8bit:
    ;; Mask channel
    out     DMA1_MASK, al
    jmp     short $+2              ; I/O delay

    ;; Clear byte pointer flip-flop
    xor     al, al
    out     DMA1_FLIPFLOP, al
    jmp     short $+2

    ;; Set base address (low byte then high byte)
    mov     al, bl
    out     DMA1_ADDR_BASE, al     ; Note: actual port = base + 2*channel
    jmp     short $+2
    mov     al, bh
    out     DMA1_ADDR_BASE, al
    jmp     short $+2

    ;; Set transfer count (low byte then high byte)
    mov     al, cl
    out     DMA1_COUNT_BASE, al
    jmp     short $+2
    mov     al, ch
    out     DMA1_COUNT_BASE, al
    jmp     short $+2

    ;; Set mode
    pop     ax                      ; Restore mode | channel
    out     DMA1_MODE, al
    jmp     short $+2

    ;; Set page register (patched at runtime)
    PATCH_POINT pp_page_port
    mov     al, dl
    out     0x87, al                ; Placeholder: patched to page register port
    jmp     short dma_setup_done

    ;; --- 16-bit channel (5-7) setup ---
dma_setup_16bit:
    ;; Mask channel (channel number in bits 1:0, relative to DMA2)
    and     al, 0x07
    or      al, 0x04
    out     DMA2_MASK, al
    jmp     short $+2

    ;; Clear byte pointer flip-flop
    xor     al, al
    out     DMA2_FLIPFLOP, al
    jmp     short $+2

    ;; Set base address (word address for 16-bit channels)
    mov     al, bl
    out     DMA2_ADDR_BASE, al
    jmp     short $+2
    mov     al, bh
    out     DMA2_ADDR_BASE, al
    jmp     short $+2

    ;; Set transfer count (word count for 16-bit channels)
    shr     cx, 1                   ; Convert bytes to words
    mov     al, cl
    out     DMA2_COUNT_BASE, al
    jmp     short $+2
    mov     al, ch
    out     DMA2_COUNT_BASE, al
    jmp     short $+2

    ;; Set mode
    pop     ax
    out     DMA2_MODE, al
    jmp     short $+2

    ;; Set page register
    mov     al, dl
    out     0x8B, al                ; Placeholder: patched to page register port

dma_setup_done:
    pop     cx
    pop     bx
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; dma_start - Unmask DMA channel to begin transfer
;;
;; Input: None (uses patched channel number)
;; Output: None
;; Clobbers: AL
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
dma_start:
    mov     al, 0x00                ; Patched to channel number
    cmp     al, 4
    jae     dma_start_16bit
    ;; 8-bit: unmask by writing channel with mask bit clear
    out     DMA1_MASK, al
    ret
dma_start_16bit:
    and     al, 0x03                ; Relative channel for DMA2
    out     DMA2_MASK, al
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; dma_stop - Mask DMA channel to halt transfer
;;
;; Input: None (uses patched channel number)
;; Output: None
;; Clobbers: AL
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
dma_stop:
    mov     al, 0x00                ; Patched to channel number
    or      al, 0x04               ; Set mask bit
    cmp     al, 0x08
    jae     dma_stop_16bit
    out     DMA1_MASK, al
    ret
dma_stop_16bit:
    and     al, 0x07
    out     DMA2_MASK, al
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; dma_get_residue - Read remaining byte count from DMA controller
;;
;; Input: None (uses patched channel number)
;; Output:
;;   AX = remaining byte count
;; Clobbers: AX, DX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
dma_get_residue:
    mov     al, 0x00                ; Patched to channel number
    cmp     al, 4
    jae     dma_residue_16bit

    ;; 8-bit channel: clear flip-flop, read count register
    xor     al, al
    out     DMA1_FLIPFLOP, al
    jmp     short $+2
    in      al, DMA1_COUNT_BASE    ; Low byte
    mov     ah, al
    jmp     short $+2
    in      al, DMA1_COUNT_BASE    ; High byte
    xchg    al, ah                  ; AX = count remaining
    inc     ax                      ; Count register is count-1
    ret

dma_residue_16bit:
    ;; 16-bit channel: clear flip-flop, read count, convert to bytes
    xor     al, al
    out     DMA2_FLIPFLOP, al
    jmp     short $+2
    in      al, DMA2_COUNT_BASE
    mov     ah, al
    jmp     short $+2
    in      al, DMA2_COUNT_BASE
    xchg    al, ah
    inc     ax
    shl     ax, 1                   ; Convert words to bytes
    ret

hot_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch table
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
patch_table:
    PATCH_TABLE_ENTRY pp_dma_channel, 7    ; IMM8 - DMA channel number
    PATCH_TABLE_ENTRY pp_page_port,   PATCH_TYPE_IO ; IO - page register port

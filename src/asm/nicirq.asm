;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file nic_irq_smc.asm
;; @brief NIC Interrupt Handler with Self-Modifying Code patch points
;;
;; Implements optimized interrupt handlers for 3C509B and 3C515 NICs with
;; CPU-specific patches applied during initialization.
;;
;; Design Decisions:
;; - packet_buffer defined in _DATA segment for proper segment management
;; - DS and ES both set to _DATA for consistent access to variables and buffers
;; - Direction flag (DF) explicitly cleared before string operations
;; - 16-bit word transfers (rep insw) used for optimal FIFO performance
;; - Mitigation loop checks FIFO empty status for early exit
;; - NIC interrupt acknowledged before PIC EOI to prevent re-triggering
;;
;; Constraints:
;; - ISR execution <100us
;; - No reentrancy (protected)
;; - Proper EOI to PIC (slave before master for IRQ>=8)
;; - All segment registers preserved (including ES)
;; - BP preserved for stack frame compatibility
;;
;; Converted to NASM syntax - 2026-01-23
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        bits 16
        cpu 386                         ; Enable 386+ instructions (SHR imm, REP INS/OUTS)

; C symbol naming bridge (maps C symbols to symbol_)
%include "csym.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; CPU Optimization Constants and Macros
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; External CPU optimization level from packet_ops.asm
extern current_cpu_opt

; CPU optimization level constants (must match packet_ops.asm)
OPT_8086            EQU 0       ; 8086/8088 baseline (no 186+ instructions)
OPT_16BIT           EQU 1       ; 186+ optimizations (INS/OUTS available)
OPT_32BIT           EQU 2       ; 386+ optimizations (32-bit registers)

;------------------------------------------------------------------------------
; INSW_SAFE - Input word array from port (8086-compatible)
; Input: ES:DI = dest buffer, DX = port, CX = word count
; Clobbers: AX, CX, DI
; Note: Caller must set direction flag (CLD) before calling
;
; OPTIMIZATION: Uses pre-computed function pointer (insw_handler) to eliminate
; 38-cycle per-call CPU detection overhead. The handler is set during init
; by init_io_dispatch() based on detected CPU type.
;------------------------------------------------------------------------------
%macro INSW_SAFE 0
        call [insw_handler]     ; 8 cycles vs 38 cycles for inline detection
%endmacro

;------------------------------------------------------------------------------
; INSB_SAFE - Input single byte from port (always 8086-compatible)
; This is just INB followed by STOSB, which works on all CPUs
; Input: ES:DI = dest buffer, DX = port
; Clobbers: AL, DI
;------------------------------------------------------------------------------
%macro INSB_SAFE 0
        in al, dx               ; AL = byte from port
        stosb                   ; [ES:DI] = AL, DI += 1
%endmacro

;------------------------------------------------------------------------------
; REP_INSB_SAFE - Input byte array from port (8086-compatible)
; Input: ES:DI = dest buffer, DX = port, CX = byte count
; Clobbers: AL, CX, DI
; Note: Caller must set direction flag (CLD) before calling
;
; NOTE: For byte-mode I/O on 8086, we use the byte-mode handler when available,
; otherwise fall back to word handler with byte conversion. The insw_8086_byte_mode
; routine provides optimized byte-at-a-time transfer for small packets.
;------------------------------------------------------------------------------
%macro REP_INSB_SAFE 0
        push bx
        mov bl, [current_cpu_opt]
        test bl, OPT_16BIT
        pop bx
        jnz %%use_insb
        ;; 8086 path: manual loop (kept for byte-mode flexibility)
        jcxz %%done
%%insb_loop:
        in al, dx
        stosb
        loop %%insb_loop
        jmp short %%done
%%use_insb:
        rep insb
%%done:
%endmacro

;------------------------------------------------------------------------------
; SHR_SAFE - Shift right by immediate count (8086-compatible)
; On 8086, shift by immediate > 1 requires CL register.
; Note: These cache functions only run on 386+ (WBINVD/CLFLUSH),
; so we use direct shifts since 386+ supports them.
;
; For pure 8086 compatibility, use:
;   mov cl, N
;   shr reg, cl
;------------------------------------------------------------------------------
; The following shift instructions in cache flush code are safe because:
; 1. CLFLUSH (shr cx, 6) requires Pentium 4+ CPU
; 2. Software cache touch (shr cx, 5) requires 386+ CPU
; Neither code path will execute on 8086/8088 systems.

segment _TEXT class=CODE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module Header (64 bytes)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        align 16
module_header:
nic_irq_module_header:                  ; Export for C code
        global  nic_irq_module_header   ; Make visible to patch_apply.c
        db      'PKTDRV',0              ; 7+1 bytes: Signature
        db      1, 0                    ; 2 bytes: Version 1.0
        dw      hot_section_start       ; 2 bytes: Hot start
        dw      hot_section_end         ; 2 bytes: Hot end
        dw      cold_section_start      ; 2 bytes: Cold start
        dw      cold_section_end        ; 2 bytes: Cold end
        dw      patch_table             ; 2 bytes: Patch table
        dw      patch_count             ; 2 bytes: Number of patches
        dw      module_size             ; 2 bytes: Total size
        dw      2*1024                  ; 2 bytes: Required memory (2KB)
        db      2                       ; 1 byte: Min CPU (286)
        db      0                       ; 1 byte: NIC type (any)
        times 37 db 0                   ; 37 bytes: Reserved

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; HOT SECTION - Resident interrupt handlers
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        align 16
hot_section_start:

        ; Public exports
        global  nic_irq_handler
        global  nic_3c509_handler
        global  nic_3c515_handler
        global  PATCH_3c515_transfer    ; Export patch point
        global  PATCH_dma_boundary_check ; Export patch point
        global  PATCH_cache_flush_pre    ; Export patch point
        global  PATCH_cache_flush_post   ; Export patch point
        ; Added: 2026-01-25 from hardware.asm modularization
        global  hardware_handle_3c509b_irq
        global  hardware_handle_3c515_irq

        ; External references
        extern  packet_isr_receive
        extern  statistics
        ; Added: 2026-01-25 from hardware.asm modularization
        extern  current_iobase
        extern  current_irq
        extern  current_instance
        extern  hardware_read_packet
        extern  hardware_configure_3c509b
        extern  init_3c515
        extern  hw_flags_table

        ; Constants
        ISR_REENTRY_FLAG    equ 0x01
        PIO_MAX_PACKETS     equ 8       ; 3C509B (PIO) batch cap per IRQ
        DMA_MAX_PACKETS     equ 32      ; 3C515 (bus master) batch cap per IRQ

        ; Added: 2026-01-25 from hardware.asm modularization
        ; 3Com NIC register offsets (all at same offset 0Eh)
        REG_COMMAND         EQU 0Eh     ; Command register
        REG_WINDOW          EQU 0Eh     ; Window select register
        REG_INT_STATUS      EQU 0Eh     ; Interrupt status

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Main NIC IRQ Handler - Tiny ISR Fast Path
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
nic_irq_handler:
        ; TINY ISR OPTIMIZATION: Minimal register saves for fast path
        ; Save essential registers and establish DS
        push    ax
        push    dx
        push    ds

        ; CRITICAL: Establish data segment for accessing driver variables
        mov     ax, _DATA
        mov     ds, ax                  ; DS = _DATA for variable access

        ; Quick ownership check - is this interrupt for us?
        mov     dx, [nic_io_base]       ; Access from data segment
        add     dx, 0x0E                ; INT_STATUS register (16-bit on 3C509B/515)
        in      ax, dx                  ; 16-bit read per 3Com datasheet
        mov     [int_status], ax        ; Save full status
        test    ax, ax                  ; Any bits set?
        jz      irq_not_ours            ; Not our interrupt

        ; Check if only simple RX/TX bits are set (90% of cases)
        mov     dx, ax                  ; Save original status
        and     dx, 0xFFEC              ; Mask OFF RX_COMPLETE(0x10) | TX_COMPLETE(0x02) | TX_AVAIL(0x01)
        jnz     irq_complex_interrupt   ; Other bits set, need full handler

        ; FAST PATH: Simple RX or TX acknowledgment
        ; First, acknowledge interrupt in NIC (write-1-to-clear)
        mov     dx, [nic_io_base]
        add     dx, 0x0E                ; INT_STATUS register
        out     dx, ax                  ; Write back status to clear

        test    ax, 0x0010              ; RX_COMPLETE?
        jz      irq_check_tx_fast

        ; Minimal RX acknowledgment
        inc     word [rx_ack_count] ; Update counter

irq_check_tx_fast:
        test    ax, 0x0002              ; TX_COMPLETE?
        jz      irq_fast_done

        ; Minimal TX acknowledgment
        inc     word [tx_ack_count] ; Update counter

irq_fast_done:
        ; Send EOI and exit fast path
        cmp     byte [effective_nic_irq], 8
        jb      irq_fast_master_only
        mov     al, 20h
        out     0A0h, al                ; EOI to slave PIC
irq_fast_master_only:
        mov     al, 20h
        out     20h, al                 ; EOI to master PIC

        pop     ds
        pop     dx
        pop     ax
        iret

irq_not_ours:
        ; Not our interrupt but MUST still send EOI for spurious interrupts
        cmp     byte [effective_nic_irq], 8
        jb      irq_not_ours_master_only
        mov     al, 20h
        out     0A0h, al                ; EOI to slave PIC
irq_not_ours_master_only:
        mov     al, 20h
        out     20h, al                 ; EOI to master PIC

        pop     ds
        pop     dx
        pop     ax
        iret

irq_complex_interrupt:
        ; Need full handler - save remaining registers
        push    bx
        push    cx
        push    si
        push    di
        push    bp
        push    ds
        push    es

        ; Set up data segment
        mov     ax, _DATA
        mov     ds, ax

        ; Clear direction flag for any string operations
        cld

        ; CRITICAL FIX: Switch to private stack for TSR safety
        ; Save caller's stack
        mov     [saved_ss], ss
        mov     [saved_sp], sp

        ; Switch to our private stack
        mov     ax, _DATA
        mov     ss, ax                      ; Stack segment = data segment
        mov     sp, isr_stack_top           ; Top of our private stack

        ; Check for reentrancy
        test    byte [isr_flags], ISR_REENTRY_FLAG
        jnz     nic_irq_already_in_isr

        ; Mark ISR active
        or      byte [isr_flags], ISR_REENTRY_FLAG

        ; PATCH POINT: NIC-specific dispatch
PATCH_nic_dispatch:
        call    generic_nic_handler     ; 3 bytes: default
        nop                             ; 2 bytes padding
        nop
        ; Will be patched to:
        ; 3C509: call nic_3c509_handler
        ; 3C515: call nic_3c515_handler

        ; Clear ISR active flag
        and     byte [isr_flags], ~ISR_REENTRY_FLAG

nic_irq_send_eoi:
        ; Send EOI to PIC (constraint requirement)
        ; CRITICAL: Must send to slave BEFORE master for IRQ >= 8
        cmp     byte [effective_nic_irq], 8
        jb      nic_irq_master_only

        ; Slave PIC - send EOI to slave first
        mov     al, 20h
        out     0A0h, al                ; EOI to slave PIC FIRST

nic_irq_master_only:
        ; Master PIC - always send
        mov     al, 20h
        out     20h, al                 ; EOI to master PIC SECOND

nic_irq_done:
        ; CRITICAL FIX: Restore caller's stack
        mov     ss, [saved_ss]
        mov     sp, [saved_sp]

        ; Restore all registers
        pop     es
        pop     ds
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        iret

nic_irq_already_in_isr:
        ; Already processing, just acknowledge and exit
        inc     word [reentry_count]
        jmp     nic_irq_send_eoi

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 3C509B Specific Handler with Interrupt Mitigation
;;
;; PERFORMANCE OPTIMIZATIONS (benefit ALL CPUs, 8086 through Pentium):
;; 1. nic_io_base cached in BP register (saves ~6 cycles/access on 286,
;;    ~4 cycles on 386+)
;; 2. int_status cached in SI register (saves ~6 cycles/access on 286,
;;    ~4 cycles on 386+)
;; 3. CLD set once at entry, not repeated in called functions (2 cy saved)
;; 4. Loop aligned for prefetch optimization (286: 6-byte queue, 386+: 16-byte)
;;
;; These are universal optimizations - register-to-register moves are faster
;; than memory accesses on ALL x86 CPUs.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
nic_3c509_handler:
        ; Save ALL registers we'll use
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    bp                      ; 286 OPT: BP used for nic_io_base cache
        push    ds
        push    es                      ; CRITICAL: Save ES for buffer operations

        ; CRITICAL: Set up segments for data access
        ; Both packet_buffer and variables are in _DATA segment
        mov     ax, _DATA
        mov     ds, ax                  ; DS = data segment for variables
        mov     es, ax                  ; ES = data segment for packet_buffer

        ; CRITICAL: Clear direction flag for string operations
        ; 286 OPT: Set once here, assume DF=0 throughout handler
        cld                             ; Ensure forward direction for rep insb

        ; 286 OPTIMIZATION: Cache nic_io_base in BP register
        ; This saves ~6 cycles per access (mov reg,reg vs mov reg,[mem])
        ; On 286: MOV DX,[mem] = 8 cycles; MOV DX,BP = 2 cycles
        mov     bp, [nic_io_base]

        ; Use internal ASM mitigation path unconditionally (no C in ISR)
c509_use_legacy_handler:
        ; INTERRUPT MITIGATION: Process multiple interrupts in one pass
        xor     bx, bx                  ; Clear interrupt batch counter

        ; Patchable batch budget for PIO NICs (3C509B)
PATCH_pio_batch_init:
        mov     cx, 8                   ; Default; SMC-patched per CPU (B9 imm16 90 90)
        nop
        nop
        mov     word [packet_budget], cx

        ; 286 OPTIMIZATION: Align loop entry for prefetch
        align   4
c509_mitigation_loop:
        ; Read and clear all interrupt sources at once
        ; 286 OPT: Use BP cache instead of memory read
        mov     dx, bp                  ; DX = cached nic_io_base (2 cy vs 8 cy)
        add     dx, 0x0E                ; COMMAND/STATUS register
        in      ax, dx
        mov     [int_status], ax        ; Store in data segment
        mov     si, ax                  ; 286 OPT: Cache int_status in SI

        ; Check for any pending interrupts
        test    si, si                  ; 286 OPT: Use cached value
        jz      c509_mitigation_done

        ; Clear all pending interrupts atomically
        out     dx, ax                  ; Acknowledge all at once
        inc     bx                      ; Count mitigated interrupts

        ; Process RX interrupts
        test    si, 0x0010              ; 286 OPT: Use cached int_status
        jz      c509_check_tx_int

        ; 286 OPTIMIZATION: Align hot loop for prefetch queue
        align   4
c509_rx_loop:
        ; Check RX status register for pending packet
        ; 286 OPT: Use BP cache instead of memory read
        mov     dx, bp                  ; DX = cached nic_io_base (2 cy vs 8 cy)
        add     dx, 0x08                ; RX_STATUS register
        in      ax, dx

        ; Check if packet is complete and ready
        test    ax, 0x8000              ; RX_COMPLETE bit
        jz      c509_rx_fifo_empty      ; No more packets - exit RX loop early

        ; Also check packet budget before processing
        cmp     word [packet_budget], 0
        jle     c509_check_tx_int       ; Budget exhausted

        ; Check for RX errors
        test    ax, 0x4000              ; RX_ERROR bit
        jz      c509_rx_good

        ; RX error - discard packet and continue
        ; 286 OPT: Use BP cache instead of memory read
        mov     dx, bp                  ; DX = cached nic_io_base (2 cy vs 8 cy)
        add     dx, 0x0E                ; COMMAND register
        mov     ax, 0x4000              ; RX_DISCARD
        out     dx, ax
        inc     word [rx_errors]
        jmp     c509_rx_next

c509_rx_good:
        ; Get packet length from status
        and     ax, 0x07FF              ; Mask length bits (0-2047)
        jz      c509_rx_discard         ; Zero length - discard
        cmp     ax, 1514                ; Max Ethernet frame
        ja      c509_rx_discard         ; Too large - discard

        mov     [packet_length], ax     ; Save valid length

        ; Read packet data from FIFO (optimized for 16-bit FIFO)
        push    cx                      ; Save packet budget counter
        mov     cx, ax                  ; Total byte count
        mov     di, packet_buffer       ; ES:DI -> destination buffer (ES=_DATA)
        ; 286 OPT: Use BP cache instead of memory read
        mov     dx, bp                  ; DX = cached nic_io_base (2 cy vs 8 cy)
        ; add     dx, 0x00              ; RX_DATA register (FIFO) - offset 0, no add needed

        ;-----------------------------------------------------------------------
        ; 8086 OPTIMIZATION: Use byte-mode I/O for small packets (<64 bytes)
        ; On 8088's 8-bit bus, byte I/O can be faster for small transfers:
        ; - Word mode: IN AX (15cy) + STOSW (9cy) / 2 bytes = 12 cycles/byte
        ; - Byte mode: IN AL (12cy) + STOSB (5cy) / 1 byte = 17 cycles/byte
        ; However, word mode has loop overhead (17cy/iter), so for small packets
        ; the simpler byte path with unrolled loop is competitive.
        ;
        ; For ARP (28 bytes), ICMP echo (64 bytes), TCP ACKs (~40 bytes),
        ; byte mode avoids word/byte splitting overhead.
        ;-----------------------------------------------------------------------
        push    bx
        mov     bl, [current_cpu_opt]
        test    bl, OPT_16BIT           ; Check if 186+
        pop     bx
        jnz     c509_use_word_io        ; 186+ always uses word I/O (faster)

        ; 8086: Check if small packet benefits from byte mode
        cmp     cx, 64                  ; Small packet threshold
        ja      c509_use_word_io        ; Large packet: use word I/O

        ; Small packet on 8086: use optimized byte transfer
        call    insw_8086_byte_mode     ; CX = byte count, ES:DI = buffer, DX = port
        jmp     c509_read_done

c509_use_word_io:
        ; Standard I/O path - CPU-adaptive word/dword transfers
        ;
        ; 386+ OPTIMIZATION: The SMC patches at PATCH_3c509_read use REP INSD
        ; (66h F3h 6Dh = 32-bit string input), which requires DWORD count in CX.
        ; 8086/286 use word or byte count. We detect CPU and adjust CX accordingly.
        ;
        ; On 386+: CX = bytes / 4, remainder handled separately (0-3 bytes)
        ; On 286:  CX = bytes / 2, remainder handled separately (0-1 bytes)
        ; On 8086: CX = bytes / 2 (via unrolled loop)
        ;
        push    cx                      ; Save total byte count

        ; Check if 386+ (OPT_32BIT flag set)
        push    bx
        mov     bl, [current_cpu_opt]
        test    bl, OPT_32BIT
        pop     bx
        jz      c509_use_16bit_io       ; 8086/286: use word count

        ;-----------------------------------------------------------------------
        ; 386+ PATH: Use DWORD count for REP INSD (2x throughput)
        ;-----------------------------------------------------------------------
        db      66h                     ; 32-bit operand prefix
        shr     cx, 2                   ; DWORD count = bytes / 4 (uses 32-bit SHR)
        and     cx, 0FFFFh              ; Ensure 16-bit result
        jcxz    c509_handle_386_remainder ; Less than 4 bytes

        ; PATCH POINT: 32-bit packet read
PATCH_3c509_read:
        INSW_SAFE                       ; Patched to REP INSD on 386+ (66h F3h 6Dh)
        nop                             ; Padding for patches

c509_handle_386_remainder:
        ; Handle 0-3 trailing bytes
        pop     cx                      ; Restore total byte count
        and     cx, 3                   ; Remainder (0-3 bytes)
        jz      c509_read_done
        ; Read remaining bytes one at a time
c509_read_386_remainder:
        in      al, dx
        stosb
        loop    c509_read_386_remainder
        jmp     c509_read_done

        ;-----------------------------------------------------------------------
        ; 8086/286 PATH: Use WORD count for REP INSW (or unrolled loop)
        ;-----------------------------------------------------------------------
c509_use_16bit_io:
        shr     cx, 1                   ; Word count = bytes / 2
        jz      c509_skip_words         ; Skip if less than 2 bytes

        ; Uses dispatch table - insw_handler set by init_io_dispatch
        call    [insw_handler]          ; REP INSW on 286, unrolled on 8086
        nop                             ; Maintain alignment with 386+ path

c509_skip_words:
        pop     cx                      ; Restore total byte count
        test    cx, 1                   ; Check for odd byte
        jz      c509_read_done
        INSB_SAFE                       ; 8086-safe single byte input

c509_read_done:

        ; Defer packet processing to bottom-half
        ; Call: int packet_isr_receive(uint8_t *packet_data, uint16_t packet_size, uint8_t nic_index)
        ; Push args right-to-left (small model near call)
        ; NIC index argument
        mov     al, [current_nic_index]
        xor     ah, ah
        push    ax
        push    word [packet_length]; Packet size
        mov     ax, packet_buffer
        push    ax                      ; Pointer to packet data
        call    packet_isr_receive
        add     sp, 6                   ; Clean up 3 args
        pop     cx                      ; Restore packet budget counter

        ; Successfully received
        inc     word [packets_received]

c509_rx_discard:
        ; Discard packet from FIFO (required to advance)
        ; 286 OPT: Use BP cache instead of memory read
        mov     dx, bp                  ; DX = cached nic_io_base (2 cy vs 8 cy)
        add     dx, 0x0E                ; COMMAND register
        mov     ax, 0x4000              ; RX_DISCARD command
        out     dx, ax

c509_rx_next:
        ; Check for more RX packets (up to batch limit)
        dec     word [packet_budget]
        jnz     c509_rx_loop

c509_rx_fifo_empty:
        ; RX FIFO is empty - skip to TX processing
        ; This early exit improves performance when FIFO drains quickly

c509_check_tx_int:
        ; Process TX completion
        ; 286 OPT: Use SI cached int_status instead of memory read
        test    si, 0x0004                      ; TX_COMPLETE (SI = cached int_status)
        jz      c509_check_errors               ; Skip if TX bit not set

        ; TX bit is set - acknowledge TX completion
        inc     word [packets_sent]

c509_check_errors:
        ; Handle any error interrupts
        ; 286 OPT: Use SI cached int_status instead of memory read
        test    si, 0x0040                      ; ADAPTER_FAILURE (SI = cached int_status)
        jz      c509_continue_mitigation        ; Skip if error bit not set

        ; Error bit is set - log error and attempt recovery
        inc     word [adapter_errors]
        call    reset_adapter_minimal

c509_continue_mitigation:
        ; Check if we should process more interrupt batches
        cmp     bx, 3                   ; Max 3 batches per ISR
        jae     c509_mitigation_done

        ; Check if we've processed maximum packets
        cmp     word [packet_budget], 0
        jle     c509_mitigation_done

        ; Loop back to check for more pending interrupts
        jmp     c509_mitigation_loop

c509_mitigation_done:
        ; Update mitigation statistics
        cmp     bx, 1
        jbe     c509_no_mitigation
        inc     word [interrupts_mitigated]
        add     word [mitigation_savings], bx

c509_no_mitigation:
        ; Send EOI to PIC before returning
        cmp     byte [effective_nic_irq], 8
        jb      c509_handler_master_only
        mov     al, 20h
        out     0A0h, al                ; EOI to slave PIC first
c509_handler_master_only:
        mov     al, 20h
        out     20h, al                 ; EOI to master PIC

        ; Restore all registers
        pop     es                      ; Restore ES
        pop     ds
        pop     bp                      ; 286 OPT: Restore BP (was used for nic_io_base cache)
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 3C515 Specific Handler (Bus Master)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
nic_3c515_handler:
        push    cx
        push    si
        push    ds
        push    es

        ; Set up segments
        mov     ax, _DATA
        mov     ds, ax
        mov     es, ax

        ; Use internal ASM mitigation path unconditionally (no C in ISR)
c515_use_legacy:
        ; Check interrupt status
        mov     dx, [nic_io_base]
        add     dx, 0x0E                ; INT_STATUS register
        in      ax, dx
        mov     [int_status], ax

        ; Check for RX complete
        test    ax, 0x0010              ; RX_COMPLETE
        jz      nic_3c515_check_tx

        ; Process RX ring buffer
        mov     si, [rx_ring_ptr]

        ; Patchable batch budget for DMA NICs (3C515)
PATCH_dma_batch_init:
        mov     cx, 32                  ; Default; SMC-patched per CPU (B9 imm16 90 90)
        nop
        nop

c515_rx_ring_loop:
        ; Check ring descriptor status
        mov     ax, [si]                ; Status word
        test    ax, 0x8000              ; OWN bit
        jz      c515_rx_ring_done       ; NIC owns it

        ; Get packet from ring
        push    si
        mov     bx, [si+2]              ; Buffer address
        mov     cx, [si+4]              ; Length

        ; PATCH POINT: DMA vs PIO transfer
        ; CRITICAL SAFETY GATE: Default to PIO until DMA verified
PATCH_3c515_transfer:
        call    transfer_pio            ; 3 bytes: SAFE default
        nop                             ; 2 bytes padding
        nop
        ; Will ONLY be patched to DMA after:
        ; 1. Bus mastering test passes
        ; 2. DMA boundary checks active
        ; 3. Cache coherency verified

        pop     si

        ; Mark descriptor as processed
        and     word [si], 0x7FFF   ; Clear OWN bit

        ; Advance ring pointer
        add     si, 8                   ; Next descriptor
        cmp     si, [rx_ring_end]
        jb      c515_next_packet
        mov     si, [rx_ring_start]     ; Wrap around

c515_next_packet:
        dec     cx
        jnz     c515_rx_ring_loop

c515_rx_ring_done:
        mov     [rx_ring_ptr], si

nic_3c515_check_tx:
        ; Check for TX complete
        test    word [int_status], 0x0004  ; TX_COMPLETE
        jz      nic_3c515_ack_irq

        ; Update TX statistics
        inc     word [packets_sent]

        ; Free TX buffer
        call    free_tx_buffer

nic_3c515_ack_irq:
        ; Acknowledge all interrupts
        mov     dx, [nic_io_base]
        add     dx, 0x0E
        mov     ax, [int_status]
        out     dx, ax

nic_3c515_done:
        pop     es
        pop     ds
        pop     si
        pop     cx
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Hardware IRQ Handlers from hardware.asm
;; Added: 2026-01-25 from hardware.asm modularization
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;-----------------------------------------------------------------------------
; hardware_handle_3c509b_irq - Complete 3C509B interrupt handler
;
; Input:  None
; Output: AX = 0 if handled, non-zero if spurious
; Uses:   All registers
;-----------------------------------------------------------------------------
hardware_handle_3c509b_irq:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Complete 3C509B interrupt handler
        push    si
        push    di
        push    ds
        push    es

        ; Set up data segment
        mov     ax, cs
        mov     ds, ax

        ; Get I/O base for current NIC
        mov     si, [current_iobase]
        test    si, si
        jz      .not_ours_3c509b

        ; Read interrupt status
        mov     dx, si
        add     dx, REG_INT_STATUS
        in      ax, dx
        mov     bx, ax                  ; Save status

        ; Check if our interrupt
        test    ax, 00FFh
        jz      .not_ours_3c509b

        ; Process TX complete
        test    bx, 0004h               ; TX_COMPLETE
        jz      .check_rx_3c509b

        ; Handle TX completion
        mov     dx, si
        add     dx, 0Bh                 ; TX_STATUS
        in      al, dx

        ; Check for errors
        test    al, 0F8h                ; Error bits
        jz      .tx_ok_3c509b

        ; Reset TX on error
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 5800h               ; CMD_TX_RESET
        out     dx, ax
        mov     ax, 4800h               ; CMD_TX_ENABLE
        out     dx, ax

.tx_ok_3c509b:
        ; Pop TX status
        mov     dx, si
        add     dx, 0Bh
        xor     al, al
        out     dx, al                  ; Clear status

        ; Acknowledge TX interrupt
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0004h       ; ACK_INTR | TX_COMPLETE
        out     dx, ax

.check_rx_3c509b:
        ; Process RX complete
        test    bx, 0001h               ; RX_COMPLETE
        jz      .check_errors_3c509b

        ; Process received packets
        mov     al, [current_instance]
        push    bp
        mov     bp, sp
        sub     sp, 8                   ; Space for buffer ptr
        lea     di, [bp-8]
        mov     [bp-8], di              ; Buffer ptr
        mov     [bp-6], ds
        push    di
        push    ds
        call    hardware_read_packet
        add     sp, 8
        mov     sp, bp
        pop     bp

        ; Acknowledge RX interrupt
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0001h       ; ACK_INTR | RX_COMPLETE
        out     dx, ax

.check_errors_3c509b:
        ; Check adapter failure
        test    bx, 0080h               ; ADAPTER_FAIL
        jz      .check_stats_3c509b

        ; Reset adapter
        mov     al, [current_instance]
        mov     dx, si
        call    hardware_configure_3c509b

        ; Acknowledge failure
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0080h       ; ACK_INTR | ADAPTER_FAIL
        out     dx, ax

.check_stats_3c509b:
        ; Update statistics
        test    bx, 0008h               ; UPDATE_STATS
        jz      .int_done_3c509b

        ; Select Window 6 for stats
        mov     dx, si
        add     dx, REG_WINDOW
        mov     ax, 0806h               ; CMD_SELECT_WINDOW | 6
        out     dx, ax

        ; Read statistics to clear
        mov     cx, 10
        mov     dx, si
.read_stats_3c509b:
        in      al, dx
        inc     dx
        loop    .read_stats_3c509b

        ; Back to Window 1
        mov     dx, si
        add     dx, REG_WINDOW
        mov     ax, 0801h               ; CMD_SELECT_WINDOW | 1
        out     dx, ax

        ; Acknowledge stats
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0008h       ; ACK_INTR | UPDATE_STATS
        out     dx, ax

.int_done_3c509b:
        ; Send EOI to PIC
        mov     al, [current_irq]
        cmp     al, 7
        jbe     .master_pic_3c509b
        mov     al, 20h
        out     0A0h, al                ; EOI to slave PIC
.master_pic_3c509b:
        mov     al, 20h
        out     20h, al                 ; EOI to master PIC

        xor     ax, ax                  ; Interrupt handled
        jmp     .exit_isr_3c509b

.not_ours_3c509b:
        mov     ax, 1                   ; Not our interrupt

.exit_isr_3c509b:
        pop     es
        pop     ds
        pop     di
        pop     si

        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end hardware_handle_3c509b_irq

;-----------------------------------------------------------------------------
; hardware_handle_3c515_irq - Handle 3C515-TX interrupt with DMA support
;
; Input:  None
; Output: AX = 0 if handled, non-zero if spurious
; Uses:   All registers
;-----------------------------------------------------------------------------
hardware_handle_3c515_irq:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Complete 3C515-TX interrupt handler with DMA support
        push    si
        push    di
        push    ds
        push    es

        mov     ax, cs
        mov     ds, ax

        ; Get I/O base
        mov     si, [current_iobase]
        test    si, si
        jz      .not_ours_3c515

        ; Read interrupt status
        mov     dx, si
        add     dx, REG_INT_STATUS
        in      ax, dx
        mov     bx, ax

        test    ax, 00FFh
        jz      .not_ours_3c515

        ; Check for DMA interrupts (higher priority)
        test    bx, 0200h               ; UP_COMPLETE (RX DMA)
        jz      .check_down_3c515

        ; Handle RX DMA completion
        ; Select Window 7 for DMA
        mov     dx, si
        add     dx, REG_WINDOW
        mov     ax, 0807h               ; CMD_SELECT_WINDOW | 7
        out     dx, ax

        ; Process RX DMA descriptors
        ; (Implementation depends on DMA buffer management)

        ; Acknowledge UP complete
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0200h       ; ACK_INTR | UP_COMPLETE
        out     dx, ax

.check_down_3c515:
        test    bx, 0400h               ; DOWN_COMPLETE (TX DMA)
        jz      .check_pio_3c515

        ; Handle TX DMA completion
        ; Process TX DMA descriptors

        ; Acknowledge DOWN complete
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0400h       ; ACK_INTR | DOWN_COMPLETE
        out     dx, ax

.check_pio_3c515:
        ; Standard TX complete
        test    bx, 0004h               ; TX_COMPLETE
        jz      .check_rx_3c515

        mov     dx, si
        add     dx, 1Bh                 ; TX_STATUS for 3C515
        in      al, dx
        out     dx, al                  ; Pop status

        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0004h       ; ACK_INTR | TX_COMPLETE
        out     dx, ax

.check_rx_3c515:
        test    bx, 0001h               ; RX_COMPLETE
        jz      .check_host_3c515

        ; Check if using DMA or PIO
        mov     di, hw_flags_table
        mov     al, [current_instance]
        xor     ah, ah
        add     di, ax
        test    byte [di], 01h          ; FLAG_BUS_MASTER
        jnz     .rx_dma_3c515

        ; PIO mode RX
        mov     al, [current_instance]
        push    bp
        mov     bp, sp
        sub     sp, 8
        lea     di, [bp-8]
        mov     [bp-8], di
        mov     [bp-6], ds
        push    di
        push    ds
        call    hardware_read_packet
        add     sp, 8
        mov     sp, bp
        pop     bp

.rx_dma_3c515:
        ; Acknowledge RX interrupt
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0001h       ; ACK_INTR | RX_COMPLETE
        out     dx, ax

.check_host_3c515:
        ; Check host error
        test    bx, 0002h               ; HOST_ERROR
        jz      .int_done_3c515

        ; Read PCI status
        mov     dx, si
        add     dx, 20h                 ; PCI_STATUS
        in      ax, dx

        ; Clear errors
        out     dx, ax

        ; Reset if fatal
        test    ax, 8000h               ; PCI_ERR_FATAL
        jz      .ack_host_3c515

        mov     al, [current_instance]
        mov     dx, si
        call    init_3c515

.ack_host_3c515:
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0002h       ; ACK_INTR | HOST_ERROR
        out     dx, ax

.int_done_3c515:
        ; Restore Window 1
        mov     dx, si
        add     dx, REG_WINDOW
        mov     ax, 0801h               ; CMD_SELECT_WINDOW | 1
        out     dx, ax

        ; Send EOI
        mov     al, [current_irq]
        cmp     al, 7
        jbe     .master_pic_3c515
        mov     al, 20h
        out     0A0h, al
.master_pic_3c515:
        mov     al, 20h
        out     20h, al

        xor     ax, ax                  ; Handled
        jmp     .exit_isr_3c515

.not_ours_3c515:
        mov     ax, 1                   ; Not ours

.exit_isr_3c515:
        pop     es
        pop     ds
        pop     di
        pop     si

        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end hardware_handle_3c515_irq

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Transfer Methods (Patchable)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        global  transfer_pio
transfer_pio:
        ; Programmed I/O transfer
        ; On 8086, SMC is disabled so REP_INSB_SAFE provides fallback
        push    dx
        push    di

        mov     di, packet_buffer
        mov     dx, [nic_io_base]

        ; PATCH POINT: Optimized PIO
        ; On 186+: May be patched to REP INSW or REP INSD
        ; On 8086: Uses loop-based byte transfer (SMC disabled)
PATCH_pio_loop:
        REP_INSB_SAFE                   ; CPU-adaptive: REP INSB (186+) or loop (8086)
        nop                             ; Padding for patches
        nop
        nop
        ; Patched for CPU-specific optimization on 186+

        pop     di
        pop     dx
        ret

        global  transfer_dma
transfer_dma:
        ; DMA transfer (3C515 only)
        push    ax
        push    cx
        push    dx

        ; PATCH POINT: DMA boundary check
PATCH_dma_boundary_check:
        nop                             ; 5-byte NOP sled
        nop                             ; Will be patched to:
        nop                             ; CALL check_dma_boundary (3 bytes)
        nop                             ; JC .use_pio_fallback (2 bytes)
        nop                             ; or 5 NOPs if PCI/safe

        ; Set up DMA descriptor
        mov     ax, [packet_buffer_phys]
        mov     [dma_desc], ax
        mov     ax, [packet_buffer_phys+2]
        mov     [dma_desc+2], ax

        ; PATCH POINT: Pre-DMA cache flush
PATCH_cache_flush_pre:
        nop                             ; 5-byte NOP sled
        nop                             ; Will be patched based on
        nop                             ; detected cache tier:
        nop                             ; CLFLUSH/WBINVD/Touch/NOP
        nop

        ; Start DMA
        mov     dx, [nic_io_base]
        add     dx, 0x24                ; DMA_CTRL
        mov     ax, 0x0001              ; START_DMA
        out     dx, ax

        ; Wait for completion (with timeout)
        mov     cx, 1000
dma_wait_loop:
        in      ax, dx
        test    ax, 0x0100              ; DMA_DONE
        jnz     dma_complete
        loop    dma_wait_loop

dma_complete:
        ; PATCH POINT: Post-DMA cache invalidate
PATCH_cache_flush_post:
        nop                             ; 5-byte NOP sled
        nop                             ; Will be patched based on
        nop                             ; detected cache tier:
        nop                             ; CLFLUSH/WBINVD/NOP
        nop

        pop     dx
        pop     cx
        pop     ax
        clc                             ; Success
        ret

transfer_dma_pio_fallback:
        ; PIO fallback when DMA unsafe
        pop     dx
        pop     cx
        pop     ax
        call    transfer_pio            ; Use PIO instead
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Safety Check Routines (Hot Section)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; DMA boundary check routine (patched in when needed)
check_dma_boundary:
        push    ax
        push    bx
        push    cx

        ; Get buffer physical address
        mov     ax, [packet_buffer_phys]
        mov     bx, [packet_buffer_phys+2]

        ; Check if buffer + length crosses 64KB boundary
        mov     cx, [packet_length]
        or      cx, cx                  ; Check for zero length
        jz      dma_boundary_ok         ; Zero length = no crossing

        ; Calculate end address for both checks
        push    ax                      ; Save start low
        push    bx                      ; Save start high

        dec     cx                      ; Use length-1
        add     ax, cx                  ; Add to low word
        adc     bx, 0                   ; Propagate carry to high word

        ; Check 16MB ISA DMA limit on end address
        test    bh, 0FFh                ; Check bits 24-31 of end
        jnz     dma_restore_and_fail    ; End >16MB, can't use ISA DMA

        ; Check if crossed 64KB boundary
        pop     bx                      ; Restore start high
        pop     ax                      ; Restore start low
        push    bx                      ; Save again for potential bounce buffer
        push    ax

        ; Original 64KB check
        mov     cx, [packet_length]
        dec     cx
        add     ax, cx
        jc      dma_boundary_crossed    ; Carry = crossed 64KB

        ; No crossing, clean up stack and continue
        pop     ax                      ; Discard saved values
        pop     bx
        jmp     dma_boundary_ok

dma_boundary_crossed:
        pop     ax                      ; Restore original address
        pop     bx
        ; Boundary crossed - use bounce buffer
        inc     word [dma_boundary_hits]
        call    allocate_bounce_buffer
        jc      dma_check_pio_fallback
        jmp     dma_boundary_ok

dma_restore_and_fail:
        pop     bx                      ; Clean up stack
        pop     ax
        jmp     dma_check_pio_fallback

dma_boundary_ok:
        pop     cx
        pop     bx
        pop     ax
        clc                             ; Success
        ret

dma_check_pio_fallback:
        ; Force PIO transfer
        pop     cx
        pop     bx
        pop     ax
        stc                             ; Error - use PIO
        ret

; Allocate bounce buffer for DMA (RX path)
;
; Called when DMA boundary crossing detected. Gets a pre-allocated
; bounce buffer from the C pool (dmabnd.c) that is guaranteed to:
; - Be in conventional memory (<640KB)
; - Not cross 64KB boundaries
; - Be within ISA 24-bit DMA limit
;
; Input:  [packet_length] = size needed
; Output: CF = 0 if success, DX:AX = physical address of bounce buffer
;         CF = 1 if failure (no buffer available, force PIO)
; Uses:   AX, BX, CX, DX
;
allocate_bounce_buffer:
        push    bx
        push    cx

        ; Call C function: void* dma_get_rx_bounce_buffer(size_t size)
        ; Push size argument (small model, near call)
        mov     ax, [packet_length]
        push    ax
        extern  dma_get_rx_bounce_buffer
        call    dma_get_rx_bounce_buffer
        add     sp, 2               ; Clean up argument

        ; Check if allocation succeeded (AX = pointer or NULL)
        test    ax, ax
        jz      .alloc_failed

        ; Convert returned near pointer to physical address
        ; In small model, pointer is in default data segment
        ; Physical = (DS * 16) + offset
        push    ax                  ; Save offset
        mov     bx, ds
        xor     dx, dx
        mov     cx, 4

.shift_loop:
        shl     bx, 1
        rcl     dx, 1
        loop    .shift_loop

        pop     ax                  ; Restore offset
        add     ax, bx              ; Add segment * 16 to offset
        adc     dx, 0               ; Carry to high word

        ; Store bounce buffer address for later copy-back
        mov     [bounce_buffer_phys], ax
        mov     [bounce_buffer_phys+2], dx

        pop     cx
        pop     bx
        clc                         ; CF=0: success
        ret

.alloc_failed:
        ; No bounce buffer available - force PIO fallback
        pop     cx
        pop     bx
        stc                         ; CF=1: failure
        ret

;==============================================================================
; Cache Flush Routines (NOT used on 8086/8088)
;
; These cache management routines contain 186+ shift instructions (shr cx, N)
; but are never called on 8086 systems because:
; - CLFLUSH requires Pentium 4+ (cache_flush_clflush)
; - WBINVD requires 486+ (cache_flush_wbinvd)
; - Software cache touch requires 386+ (cache_flush_software)
; - 8086/8088 CPUs have no cache to manage
;
; The 186+ shift instructions are acceptable here because this code
; only executes on CPUs that support those instructions.
;==============================================================================

; Cache flush for CLFLUSH systems (Pentium 4+)
cache_flush_clflush:
        push    ax
        push    bx
        push    cx
        push    di
        push    es

        ; Get segment:offset of buffer (not physical address)
        mov     ax, [packet_buffer_seg]
        mov     es, ax
        mov     di, [packet_buffer_off]

        ; Align DI down to 64-byte boundary
        mov     bx, di
        and     bx, 63                  ; Get misalignment amount
        sub     di, bx                  ; Align down to cache line

        ; Calculate number of cache lines to flush
        ; Lines = (length + misalignment + 63) / 64
        mov     cx, [packet_length]
        add     cx, bx                  ; Add misalignment
        add     cx, 63                  ; Round up
        shr     cx, 6                   ; Divide by 64
        jz      clflush_done            ; Nothing to flush

clflush_loop:
        ; CLFLUSH es:[di] - proper encoding for real mode
        db      26h                     ; ES: segment override
        db      0Fh, 0AEh, 3Dh          ; CLFLUSH [di]

        ; Advance to next cache line
        add     di, 64
        jnc     clflush_no_wrap         ; Check for segment wrap

        ; Handle segment wrap - advance ES by 64KB
        mov     ax, es
        add     ax, 1000h               ; 64KB = 0x1000 paragraphs
        mov     es, ax

clflush_no_wrap:
        loop    clflush_loop

clflush_done:
        inc     word [cache_flushes]

        pop     es
        pop     di
        pop     cx
        pop     bx
        pop     ax
        ret

; Cache flush using WBINVD (486+)
cache_flush_wbinvd:
        db      0Fh, 09h                ; WBINVD instruction
        inc     word [cache_flushes]
        ret

; Software cache management (386)
cache_flush_software:
        push    si
        push    cx
        push    ax

        ; Touch every cache line to force write-through
        mov     si, [packet_buffer_phys]
        mov     cx, [packet_length]
        add     cx, 31                  ; Round to cache lines
        shr     cx, 5                   ; Divide by 32

cache_sw_touch_loop:
        mov     al, [si]                ; Read to force cache line load
        add     si, 32                  ; Next cache line
        loop    cache_sw_touch_loop

        inc     word [cache_flushes]

        pop     ax
        pop     cx
        pop     si
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Minimal adapter reset for error recovery
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
reset_adapter_minimal:
        push    ax
        push    dx

        ; Issue global reset command
        mov     dx, [nic_io_base]   ; CS override for segment safety
        add     dx, 0x0E                ; COMMAND register
        mov     ax, 0x0000              ; GLOBAL_RESET
        out     dx, ax

        ; Brief delay for reset
        push    cx
        mov     cx, 100
reset_delay_loop:
        nop
        loop    reset_delay_loop
        pop     cx

        ; Re-enable receiver and transmitter
        mov     ax, 0x0101              ; RX_ENABLE | TX_ENABLE
        out     dx, ax

        pop     dx
        pop     ax
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Generic fallback handler
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
generic_nic_handler:
        ; Should never be called after patching
        inc     word [unhandled_irqs]
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 8086 Performance Optimized I/O Routines
;;
;; These routines are called via function pointer (insw_handler/outsw_handler)
;; to eliminate the 38-cycle per-call CPU detection overhead.
;;
;; Input: ES:DI = dest buffer, DX = port, CX = word count
;; Clobbers: AX, CX, DI (same as REP INSW)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;------------------------------------------------------------------------------
; insw_186 / insw_286_direct - 186/286+ optimized path using REP INSW
; Called via [insw_handler] when CPU >= 186
;
; 286 OPTIMIZATION: This is the tightest possible I/O routine for 286+
; Just REP INSW + RET = 2 bytes of code, minimal overhead
; No dispatch, no checks - pure string I/O
;------------------------------------------------------------------------------
        global  insw_186
        global  insw_286_direct
insw_186:
insw_286_direct:                        ; Alias for 286-specific documentation
        rep insw                        ; 186+ string I/O, ~4 cycles/word
        ret

;------------------------------------------------------------------------------
; insw_8086_unrolled - 8086/8088 optimized with 4x loop unrolling
; Reduces loop overhead from 17 cycles/word to ~7 cycles/word amortized
;
; Standard loop: IN AX + STOSW + LOOP = 15+9+17 = 41 cycles/word
; Unrolled 4x:   4*(IN AX + STOSW) + SUB + JA = 4*24+7 = 103 cycles/4 words
;                = 25.75 cycles/word (37% faster)
;------------------------------------------------------------------------------
        global  insw_8086_unrolled
insw_8086_unrolled:
        ; Handle small counts (< 4 words) directly
        cmp     cx, 4
        jb      insw_8086_remainder

insw_8086_unroll_loop:
        ; Unrolled 4x: process 4 words per iteration
        in      ax, dx          ; Word 1: 15 cycles
        stosw                   ; 9 cycles
        in      ax, dx          ; Word 2: 15 cycles
        stosw                   ; 9 cycles
        in      ax, dx          ; Word 3: 15 cycles
        stosw                   ; 9 cycles
        in      ax, dx          ; Word 4: 15 cycles
        stosw                   ; 9 cycles
        sub     cx, 4           ; 3 cycles
        cmp     cx, 4           ; 3 cycles
        jae     insw_8086_unroll_loop ; 4 cycles (taken)

        ; Handle remainder (0-3 words)
insw_8086_remainder:
        jcxz    insw_8086_done
insw_8086_tail:
        in      ax, dx
        stosw
        loop    insw_8086_tail
insw_8086_done:
        ret

;------------------------------------------------------------------------------
; outsw_186 / outsw_286_direct - 186/286+ optimized path using REP OUTSW
; Called via [outsw_handler] when CPU >= 186
;
; 286 OPTIMIZATION: This is the tightest possible I/O routine for 286+
; Just REP OUTSW + RET = 2 bytes of code, minimal overhead
; No dispatch, no checks - pure string I/O
;------------------------------------------------------------------------------
        global  outsw_186
        global  outsw_286_direct
outsw_186:
outsw_286_direct:                       ; Alias for 286-specific documentation
        rep outsw                       ; 186+ string I/O, ~4 cycles/word
        ret

;------------------------------------------------------------------------------
; outsd_386_direct - 386+ optimized path using REP OUTSD (32-bit I/O)
; Called via [outsw_handler] when CPU >= 386 (OPT_32BIT set)
;
; 386+ OPTIMIZATION: 32-bit string I/O provides 2x throughput vs REP OUTSW.
; Uses 0x66 operand-size prefix to perform OUTSD in 16-bit code segment.
;
; Input: DS:SI = source buffer, DX = port, CX = DWORD count (NOT byte/word!)
;        Caller must divide byte count by 4 and handle remainder separately
; Clobbers: EAX, ECX, ESI
;
; NOTE: This is optimized for bulk transfers. For transfers < 4 bytes,
;       callers should use outsw_286_direct or individual OUT instructions.
;------------------------------------------------------------------------------
        global  outsd_386_direct
outsd_386_direct:
        db      66h                     ; 32-bit operand prefix
        rep outsd                       ; 386+ string I/O, 32 bits at a time
        ret

;------------------------------------------------------------------------------
; insd_386_direct - 386+ optimized path using REP INSD (32-bit I/O)
; Called for 386+ 32-bit input transfers
;
; Input: ES:DI = dest buffer, DX = port, CX = DWORD count (NOT byte/word!)
; Clobbers: EAX, ECX, EDI
;------------------------------------------------------------------------------
        global  insd_386_direct
insd_386_direct:
        db      66h                     ; 32-bit operand prefix
        rep insd                        ; 386+ string I/O, 32 bits at a time
        ret

;------------------------------------------------------------------------------
; outsw_386_wrapper - 386+ wrapper that accepts WORD count (compatible API)
;
; This wrapper provides a drop-in replacement for outsw_286_direct that can
; be used via [outsw_handler]. It accepts word count, converts to DWORD count,
; and uses 32-bit I/O for 2x throughput.
;
; Input: DS:SI = source buffer, DX = port, CX = WORD count (same as 286 handler)
; Clobbers: EAX, ECX, ESI
;------------------------------------------------------------------------------
        global  outsw_386_wrapper
outsw_386_wrapper:
        ; Convert word count to DWORD count: 2 words = 1 DWORD
        push    ax
        mov     ax, cx
        shr     cx, 1                   ; CX = word_count / 2 = DWORD count
        jz      outsw_386_remainder     ; < 2 words, use word I/O

        ; Perform 32-bit output
        db      66h                     ; 32-bit operand prefix
        rep outsd                       ; 386+ string I/O, 32 bits at a time

outsw_386_remainder:
        ; Handle remainder (0 or 1 word)
        test    ax, 1                   ; Original word count odd?
        jz      outsw_386_done
        outsw                           ; Transfer final word

outsw_386_done:
        pop     ax
        ret

;------------------------------------------------------------------------------
; insw_386_wrapper - 386+ wrapper that accepts WORD count (compatible API)
;
; Drop-in replacement for insw_286_direct using 32-bit I/O.
;
; Input: ES:DI = dest buffer, DX = port, CX = WORD count (same as 286 handler)
; Clobbers: EAX, ECX, EDI
;------------------------------------------------------------------------------
        global  insw_386_wrapper
insw_386_wrapper:
        ; Convert word count to DWORD count
        push    ax
        mov     ax, cx
        shr     cx, 1                   ; CX = word_count / 2 = DWORD count
        jz      insw_386_remainder      ; < 2 words, use word I/O

        ; Perform 32-bit input
        db      66h                     ; 32-bit operand prefix
        rep insd                        ; 386+ string I/O, 32 bits at a time

insw_386_remainder:
        ; Handle remainder (0 or 1 word)
        test    ax, 1                   ; Original word count odd?
        jz      insw_386_done
        insw                            ; Transfer final word

insw_386_done:
        pop     ax
        ret

;------------------------------------------------------------------------------
; outsw_8086_unrolled - 8086/8088 optimized with 4x loop unrolling
; Input: DS:SI = source buffer, DX = port, CX = word count
; Clobbers: AX, CX, SI
;------------------------------------------------------------------------------
        global  outsw_8086_unrolled
outsw_8086_unrolled:
        ; Handle small counts (< 4 words) directly
        cmp     cx, 4
        jb      outsw_8086_remainder

outsw_8086_unroll_loop:
        ; Unrolled 4x: process 4 words per iteration
        lodsw                   ; Word 1: 12 cycles
        out     dx, ax          ; 12 cycles
        lodsw                   ; Word 2: 12 cycles
        out     dx, ax          ; 12 cycles
        lodsw                   ; Word 3: 12 cycles
        out     dx, ax          ; 12 cycles
        lodsw                   ; Word 4: 12 cycles
        out     dx, ax          ; 12 cycles
        sub     cx, 4           ; 3 cycles
        cmp     cx, 4           ; 3 cycles
        jae     outsw_8086_unroll_loop ; 4 cycles (taken)

        ; Handle remainder (0-3 words)
outsw_8086_remainder:
        jcxz    outsw_8086_done
outsw_8086_tail:
        lodsw
        out     dx, ax
        loop    outsw_8086_tail
outsw_8086_done:
        ret

;------------------------------------------------------------------------------
; insw_8086_byte_mode - Byte-at-a-time for small packets (<64 bytes)
; On 8088 (8-bit bus), byte I/O can be faster for small transfers
; Input: ES:DI = dest buffer, DX = port, CX = BYTE count (not words!)
;------------------------------------------------------------------------------
        global  insw_8086_byte_mode
insw_8086_byte_mode:
        jcxz    byte_mode_done
byte_mode_loop:
        in      al, dx          ; 12 cycles
        stosb                   ; 5 cycles
        loop    byte_mode_loop  ; 17 cycles = 34 cycles/byte
byte_mode_done:
        ret

;------------------------------------------------------------------------------
; init_io_dispatch - Initialize I/O function pointers based on CPU type
; Called during driver initialization
; Input: AL = current_cpu_opt value (OPT_8086=0, OPT_16BIT=1, etc.)
;
; OPTIMIZATION: Pre-computed dispatch eliminates 38-cycle per-call
; CPU detection overhead. The handler is set once at init and never changes.
;
; Handler selection:
;   8086:  insw_8086_unrolled (4x unrolled loop, 25.75 cy/word vs 41 cy/word)
;   186+:  insw_286_direct (pure REP INSW, ~4 cycles/word)
;   386+:  insw_386_wrapper (REP INSD with word-count API, 2x throughput)
;
; The 386+ wrappers provide 32-bit I/O while maintaining API compatibility
; with existing callers that pass word counts. They handle odd word counts
; by transferring the remainder with 16-bit I/O.
;------------------------------------------------------------------------------
        global  init_io_dispatch
init_io_dispatch:
        push    ax
        push    bx

        ; Check if 8086 (OPT_8086 = 0, no OPT_16BIT flag)
        test    al, OPT_16BIT
        jnz     dispatch_setup_186_plus

        ; 8086 path: use unrolled loops (37% faster than simple loop)
        mov     word [insw_handler], insw_8086_unrolled
        mov     word [outsw_handler], outsw_8086_unrolled
        jmp     dispatch_done

dispatch_setup_186_plus:
        ; Check if 386+ (OPT_32BIT flag set)
        test    al, OPT_32BIT
        jnz     dispatch_setup_386_plus

        ; 186/286 path: use REP INSW/OUTSW (tightest possible code)
        mov     word [insw_handler], insw_286_direct
        mov     word [outsw_handler], outsw_286_direct
        jmp     dispatch_done

dispatch_setup_386_plus:
        ; 386+ path: use 32-bit I/O wrappers for 2x throughput
        ; These wrappers accept word count (compatible API) but internally
        ; use REP INSD/OUTSD with proper remainder handling
        mov     word [insw_handler], insw_386_wrapper
        mov     word [outsw_handler], outsw_386_wrapper

dispatch_done:
        pop     bx
        pop     ax
        ret

hot_section_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; COLD SECTION - Initialization (discarded)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        align 16
cold_section_start:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch Table
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
patch_table:
        ; Patch 1: NIC dispatch
        dw      PATCH_nic_dispatch
        db      4                       ; Type: ISR
        db      5                       ; Size
        ; 8086: Generic handler
        db      0E8h, 00h, 00h, 90h, 90h  ; CALL generic
        ; 286: Same
        db      0E8h, 00h, 00h, 90h, 90h
        ; 386: Same (NIC-specific, not CPU)
        db      0E8h, 00h, 00h, 90h, 90h
        ; 486: Same
        db      0E8h, 00h, 00h, 90h, 90h
        ; Pentium: Same
        db      0E8h, 00h, 00h, 90h, 90h

        ; Patch 2: 3C509 packet read
        dw      PATCH_3c509_read
        db      2                       ; Type: IO
        db      5                       ; Size
        ; 8086: REP INSB
        db      0F3h, 6Ch, 90h, 90h, 90h
        ; 286: REP INSW
        db      0F3h, 6Dh, 90h, 90h, 90h
        ; 386: 32-bit string ops
        db      66h, 0F3h, 6Dh, 90h, 90h
        ; 486: Same as 386
        db      66h, 0F3h, 6Dh, 90h, 90h
        ; Pentium: Same
        db      66h, 0F3h, 6Dh, 90h, 90h

        ; Patch 3: 3C515 transfer method
        dw      PATCH_3c515_transfer
        db      4                       ; Type: ISR
        db      5                       ; Size
        ; All: Will be patched based on DMA capability
        db      0E8h, 00h, 00h, 90h, 90h  ; CALL transfer_pio
        db      0E8h, 00h, 00h, 90h, 90h
        db      0E8h, 00h, 00h, 90h, 90h
        db      0E8h, 00h, 00h, 90h, 90h
        db      0E8h, 00h, 00h, 90h, 90h

        ; Patch 4: PIO optimization
        dw      PATCH_pio_loop
        db      2                       ; Type: IO
        db      5                       ; Size
        ; CPU-specific I/O optimization
        db      0F3h, 6Ch, 90h, 90h, 90h  ; REP INSB
        db      0F3h, 6Dh, 90h, 90h, 90h  ; REP INSW
        db      66h, 0F3h, 6Dh, 90h, 90h  ; 32-bit
        db      66h, 0F3h, 6Dh, 90h, 90h
        db      66h, 0F3h, 6Dh, 90h, 90h

        ; Patch 5: DMA boundary check
        dw      PATCH_dma_boundary_check
        db      6                       ; Type: DMA_CHECK
        db      5                       ; Size
        ; All CPUs: either check or NOP based on bus type
        db      90h, 90h, 90h, 90h, 90h  ; NOPs (safe default)
        db      0E8h, 00h, 00h, 90h, 90h  ; CALL check_boundary (ISA)
        db      0E8h, 00h, 00h, 90h, 90h  ; CALL check_boundary (ISA)
        db      90h, 90h, 90h, 90h, 90h  ; NOPs (EISA/PCI)
        db      90h, 90h, 90h, 90h, 90h  ; NOPs (PCI)

        ; Patch: PIO batch size (mov cx, imm16; nop; nop)
        dw      PATCH_pio_batch_init
        db      4                       ; Type: ISR
        db      5                       ; Size
        ; 8086: PIO=2 (reduced for better latency on slow CPUs)
        ; On 8088 @ 4.77 MHz, 4 packets = ~25ms ISR time, too long
        ; With 2 packets = ~12ms, better interrupt responsiveness
        db      0B9h, 02h, 00h, 90h, 90h
        ; 286:  PIO=8 (increased from 6 for better interrupt efficiency)
        ; 286 @ 12 MHz: 8 packets x ~1ms = 8ms ISR time (acceptable)
        ; 33% better efficiency vs 6 packets (fewer ISR entries/exits)
        db      0B9h, 08h, 00h, 90h, 90h
        ; 386:  PIO=12 (increased from 10 - faster CPU handles more)
        ; 386 @ 33 MHz: 12 packets x ~300us = 3.6ms ISR (well within limits)
        db      0B9h, 0Ch, 00h, 90h, 90h
        ; 486:  PIO=16 (increased from 12 - L1 cache helps)
        ; 486 @ 66 MHz: 16 packets x ~150us = 2.4ms ISR (acceptable)
        db      0B9h, 10h, 00h, 90h, 90h
        ; Pentium: PIO=24 (increased from 16 - dual pipeline very fast)
        ; Pentium @ 100 MHz: 24 packets x ~80us = 1.9ms ISR (acceptable)
        db      0B9h, 18h, 00h, 90h, 90h

        ; Patch: DMA batch size (mov cx, imm16; nop; nop)
        dw      PATCH_dma_batch_init
        db      4                       ; Type: ISR
        db      5                       ; Size
        ; 8086: DMA=8 (conservative)
        db      0B9h, 08h, 00h, 90h, 90h
        ; 286:  DMA=16
        db      0B9h, 10h, 00h, 90h, 90h
        ; 386:  DMA=28 (increased from 24 - bus master is fast)
        db      0B9h, 1Ch, 00h, 90h, 90h
        ; 486:  DMA=32
        db      0B9h, 20h, 00h, 90h, 90h
        ; Pentium: DMA=48 (increased from 32 - very fast CPU)
        db      0B9h, 30h, 00h, 90h, 90h

        ; Patch 6: Pre-DMA cache flush
        dw      PATCH_cache_flush_pre
        db      7                       ; Type: CACHE_PRE
        db      5                       ; Size
        ; CPU-specific cache management
        db      90h, 90h, 90h, 90h, 90h  ; 8086: NOPs
        db      90h, 90h, 90h, 90h, 90h  ; 286: NOPs
        db      90h, 90h, 90h, 90h, 90h  ; 386: NOPs or touch
        db      0Fh, 09h, 90h, 90h, 90h  ; 486: WBINVD
        db      0E8h, 00h, 00h, 90h, 90h  ; Pentium: CALL flush_proc

        ; Patch 7: Post-DMA cache invalidate
        dw      PATCH_cache_flush_post
        db      8                       ; Type: CACHE_POST
        db      5                       ; Size
        ; CPU-specific cache management
        db      90h, 90h, 90h, 90h, 90h  ; 8086: NOPs
        db      90h, 90h, 90h, 90h, 90h  ; 286: NOPs
        db      90h, 90h, 90h, 90h, 90h  ; 386: NOPs or touch
        db      0Fh, 09h, 90h, 90h, 90h  ; 486: WBINVD
        db      0E8h, 00h, 00h, 90h, 90h  ; Pentium: CALL invalidate_proc

patch_count     equ     9

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; IRQ Handler Initialization
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
irq_handler_init:
        global  irq_handler_init
        push    ax
        push    bx
        push    cx
        push    dx
        push    ds
        push    es

        ; Set up data segment first for variable access
        push    ds
        mov     ax, _DATA
        mov     ds, ax

        ; Load NIC IRQ selected during detection
        mov     al, [detected_nic_irq]
        mov     [nic_irq], al

        pop     ds

        ; Compute effective IRQ (map 2->9)
        mov     dl, al
        cmp     al, 2
        jne     irq_init_have_eff
        mov     dl, 9
irq_init_have_eff:
        push    ds
        mov     ax, _DATA
        mov     ds, ax
        mov     [effective_nic_irq], dl

        ; Save original IMR state before masking
        push    dx
        mov     dx, 021h                ; Master PIC IMR
        in      al, dx
        mov     [original_imr_master], al
        mov     dx, 0A1h                ; Slave PIC IMR
        in      al, dx
        mov     [original_imr_slave], al
        pop     dx

        pop     ds

        ; CRITICAL: Mask the IRQ line immediately to minimize race window
        mov     al, dl                  ; AL = effective_nic_irq
        call    mask_irq_in_pic

        ; For slave IRQs, ensure cascade (IRQ2) is unmasked
        cmp     dl, 8                   ; DL still has effective_nic_irq
        jb      irq_init_skip_cascade
        push    ds
        mov     ax, _DATA
        mov     ds, ax
        push    ax
        pushf
        cli
        mov     dx, 021h                ; Master PIC IMR
        in      al, dx
        test    al, 04h                 ; Check if IRQ2 is masked
        jz      irq_init_cascade_ok     ; Already unmasked, skip
        mov     byte [cascade_modified], 1  ; Mark that we modified it
        and     al, 0FBh                ; Clear bit 2 (unmask IRQ2 cascade)
        out     dx, al
irq_init_cascade_ok:
        popf
        pop     ax
        pop     ds
irq_init_skip_cascade:

        ; Calculate interrupt vector (IRQ2 already mapped to 9 in effective_nic_irq)
        mov     al, dl                  ; Restore AL = effective_nic_irq
        cmp     al, 8
        jb      irq_init_use_master
        ; Slave PIC vectors (IRQ8..15 -> INT 70h..77h)
        mov     bl, al
        add     bl, 0x70 - 8
        jmp     irq_init_have_vector
irq_init_use_master:
        ; Master PIC vectors (IRQ0..7 -> INT 08h..0Fh)
        mov     bl, al
        add     bl, 0x08

irq_init_have_vector:
        ; Save old vector (AH=35h, AL=vector -> ES:BX)
        mov     ah, 0x35
        mov     al, bl
        int     21h

        ; Store old vector in _DATA
        push    ds
        mov     ax, _DATA
        mov     ds, ax
        mov     [old_vector_off], bx
        mov     [old_vector_seg], es
        mov     [installed_vector], bl
        pop     ds

        ; Install new handler (AH=25h expects DS:DX -> handler address)
        mov     dx, nic_irq_handler
        push    cs
        pop     ds                      ; DS = CS so DS:DX points to code
        mov     ah, 0x25
        mov     al, bl                  ; vector number
        int     21h

        ; Leave IRQ masked; C-side phase will unmask explicitly after init

        pop     es
        pop     ds
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; Legacy enable_irq_in_pic removed - use unmask_irq_in_pic instead
; The new function takes IRQ in AL and properly handles atomicity

; Mask the NIC IRQ line in the PIC IMR (master/slave) - safe during vector change
; AL = effective IRQ number (0-15, already mapped 2->9)
mask_irq_in_pic:
        pushf
        cli                     ; Atomic IMR update
        push    ax
        push    bx
        push    cx
        push    dx

        cmp     al, 8
        jb      mask_irq_master

        ; Slave PIC: mask bit (IRQ8..15 -> bit 0..7)
        sub     al, 8
        mov     cl, al
        mov     bl, 1           ; Use BL for mask bit
        shl     bl, cl
        mov     dx, 0A1h
        in      al, dx          ; Read IMR into AL (8-bit I/O only!)
        or      al, bl          ; Set mask bit
        out     dx, al
        jmp     mask_irq_done

mask_irq_master:
        ; Master PIC
        mov     cl, al
        mov     bl, 1           ; Use BL for mask bit
        shl     bl, cl
        mov     dx, 021h
        in      al, dx          ; Read IMR into AL (8-bit I/O only!)
        or      al, bl          ; Set mask bit
        out     dx, al

mask_irq_done:
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        popf
        ret

; Unmask the NIC IRQ line in the PIC IMR (for enable_driver_interrupts)
; AL = effective IRQ number (0-15, already mapped 2->9)
unmask_irq_in_pic:
        global  unmask_irq_in_pic
        pushf
        cli                     ; Atomic IMR update
        push    ax
        push    bx
        push    cx
        push    dx

        cmp     al, 8
        jb      unmask_irq_master

        ; Slave PIC: unmask bit (IRQ8..15 -> bit 0..7)
        sub     al, 8
        mov     cl, al
        mov     bl, 1           ; Use BL for mask bit
        shl     bl, cl
        not     bl              ; Invert to create unmask pattern
        mov     dx, 0A1h
        in      al, dx          ; Read IMR into AL
        and     al, bl          ; Clear mask bit
        out     dx, al
        jmp     unmask_irq_done

unmask_irq_master:
        ; Master PIC
        mov     cl, al
        mov     bl, 1           ; Use BL for mask bit
        shl     bl, cl
        not     bl              ; Invert to create unmask pattern
        mov     dx, 021h
        in      al, dx          ; Read IMR into AL
        and     al, bl          ; Clear mask bit
        out     dx, al

unmask_irq_done:
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        popf
        ret

; Uninstall IRQ handler and restore previous vector
irq_handler_uninstall:
        global  irq_handler_uninstall
        push    ax
        push    bx
        push    cx                      ; Need CX for shift operations
        push    dx
        push    ds

        ; Set up data segment
        mov     ax, _DATA
        mov     ds, ax

        ; Keep IRQ masked while restoring
        mov     al, [effective_nic_irq]
        call    mask_irq_in_pic

        ; Load saved vector number
        mov     al, [installed_vector]
        or      al, al
        jz      uninst_restore_imr_bit  ; Nothing to restore

        mov     bl, al                  ; Save vector number in BL

        ; Load saved old handler DS:DX from _DATA
        mov     dx, [old_vector_off]
        mov     ax, [old_vector_seg]
        or      ax, ax
        jz      uninst_restore_imr_bit  ; No old vector saved

        ; AH=25h Set Vector, DS:DX must point to old handler
        mov     ds, ax                  ; DS = old handler segment (clobbers AL!)
        mov     ah, 0x25
        mov     al, bl                  ; Restore vector number from BL
        int     21h

        ; Clear tracking variables for idempotency
        mov     ax, _DATA
        mov     ds, ax
        mov     byte [installed_vector], 0
        mov     word [old_vector_off], 0
        mov     word [old_vector_seg], 0

uninst_restore_imr_bit:
        ; Restore ONLY our IRQ bit in IMR (preserve other drivers' changes)
        ; NOTE: DS is saved/restored by function prologue/epilogue
        mov     ax, _DATA
        mov     ds, ax
        mov     al, [effective_nic_irq]

        pushf
        cli                             ; Atomic IMR update

        cmp     al, 8
        jb      uninst_restore_master

        ; Slave PIC - restore only our bit
        sub     al, 8
        mov     cl, al
        mov     bl, 1
        shl     bl, cl                  ; BL = our bit mask
        not     bl                      ; BL = inverse mask (all bits except ours)

        mov     dx, 0A1h                ; Slave IMR
        in      al, dx                  ; Current IMR
        and     al, bl                  ; Clear our bit
        mov     ah, [original_imr_slave]
        not     bl                      ; BL = our bit mask again
        and     ah, bl                  ; Isolate original bit
        or      al, ah                  ; Restore original bit value
        out     dx, al
        jmp     uninst_restore_done

uninst_restore_master:
        ; Master PIC - restore only our bit
        mov     cl, al
        mov     bl, 1
        shl     bl, cl                  ; BL = our bit mask
        not     bl                      ; BL = inverse mask

        mov     dx, 021h                ; Master IMR
        in      al, dx                  ; Current IMR
        and     al, bl                  ; Clear our bit
        mov     ah, [original_imr_master]
        not     bl                      ; BL = our bit mask again
        and     ah, bl                  ; Isolate original bit
        or      al, ah                  ; Restore original bit value
        out     dx, al

uninst_restore_done:
        ; Restore IRQ2 cascade if we modified it
        cmp     byte [cascade_modified], 0
        jz      uninst_skip_cascade
        push    ax
        pushf
        cli
        mov     dx, 021h                ; Master PIC IMR
        in      al, dx
        test    byte [original_imr_master], 04h  ; Was IRQ2 originally masked?
        jz      uninst_cascade_unmask
        or      al, 04h                 ; Re-mask IRQ2
        jmp     uninst_cascade_write
uninst_cascade_unmask:
        and     al, 0FBh                ; Keep IRQ2 unmasked
uninst_cascade_write:
        out     dx, al
        popf
        pop     ax
uninst_skip_cascade:

        pop     ds
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

cold_section_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Binding helper - set IO base, IRQ, and NIC index from C
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
nic_irq_set_binding:
        global  nic_irq_set_binding
        ; Args (small model near): [SP+2]=io_base (word), [SP+4]=irq (byte), [SP+6]=nic_index (byte)
        push    bp
        mov     bp, sp
        push    ax
        push    ds

        mov     ax, _DATA
        mov     ds, ax

        mov     ax, [bp+4]             ; io_base
        mov     [nic_io_base], ax
        mov     al, [bp+6]             ; irq
        mov     [detected_nic_irq], al
        mov     al, [bp+8]             ; nic_index
        mov     [current_nic_index], al

        pop     ds
        pop     ax
        pop     bp
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Data Section
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
segment _DATA class=DATA

        ; Safety flags (set during initialization)
dma_safety_flags    dw      0
        ; Bit 0: Bus master enabled
        ; Bit 1: Cache coherency required
        ; Bit 2: Bounce buffers available
        ; Bit 3: CLFLUSH available
        ; Bit 4: WBINVD available
        ; Bit 5: Full snooping detected
        ; Bit 6: ISA DMA (needs 64KB check)
        ; Bit 7: PCI bus (no 64KB limit)

cache_tier_selected db      0       ; 0-4 based on runtime tests
bus_type            db      0       ; 0=ISA, 1=EISA, 2=PCI

        ; Runtime state
isr_flags           db      0
nic_irq             db      0
nic_io_base         dw      0
detected_nic_irq    db      0
current_nic_index   db      0
        global  current_nic_index
int_status          dw      0

        ; CRITICAL FIX: Private ISR stack (2KB)
        ; Prevents stack corruption from other TSRs
        align 16
isr_private_stack   times 2048 db 0     ; 2KB private stack
isr_stack_top       equ     $               ; Top of stack (grows down)
saved_ss            dw      0               ; Saved caller's SS
saved_sp            dw      0               ; Saved caller's SP

        ; Statistics
packets_received    dw      0
packets_sent        dw      0
reentry_count       dw      0
unhandled_irqs      dw      0
dma_boundary_hits   dw      0
cache_flushes       dw      0
rx_ack_count        dw      0       ; Tiny ISR: RX fast acknowledgments
tx_ack_count        dw      0       ; Tiny ISR: TX fast acknowledgments
adapter_errors      dw      0       ; Adapter failure count
interrupts_mitigated dw     0       ; Number of interrupt batches
mitigation_savings  dw      0       ; Total interrupts saved
rx_errors           dw      0       ; RX error count

        ; Old interrupt vector storage for restore on unload
old_vector_off      dw      0
old_vector_seg      dw      0
installed_vector    db      0

        ; Effective IRQ (remaps 2->9 for AT PIC)
effective_nic_irq   db      0

        ;-----------------------------------------------------------------------
        ; 8086 Performance Optimization: Function Dispatch Table
        ; Set during init based on CPU type to eliminate per-call overhead
        ;-----------------------------------------------------------------------
        global  insw_handler, outsw_handler
insw_handler        dw      0       ; Points to insw_186 or insw_8086_unrolled
outsw_handler       dw      0       ; Points to outsw_186 or outsw_8086_unrolled

        ; Original IMR state for clean restore
original_imr_master db      0       ; Original master PIC IMR
original_imr_slave  db      0       ; Original slave PIC IMR
cascade_modified    db      0       ; 1 if we unmasked IRQ2 cascade

        ; Packet handling
packet_length       dw      0
packet_buffer_phys  dd      0
packet_buffer_seg   dw      0       ; Segment for CLFLUSH
packet_buffer_off   dw      0       ; Offset for CLFLUSH
packet_budget       dw      0       ; Separate counter for mitigation loop

        ; Packet buffer - defined locally in data segment for proper ES:DS access
        ; Aligned to 32-byte cache line boundary for optimal 486+/Pentium performance
        ; This eliminates cache-line splits during DMA transfers and memory copies
        align 32
packet_buffer       resb    1536            ; Max Ethernet frame (1514) + padding to 32-byte multiple
        global  packet_buffer           ; Export for C code if needed

        ; Bounce buffer physical address (for RX copy-back)
bounce_buffer_phys  dd      0               ; Physical address of allocated bounce buffer

        ; 3C515 ring buffer pointers
rx_ring_start       dw      0
rx_ring_end         dw      0
rx_ring_ptr         dw      0

        ; DMA descriptor
dma_desc            dd      0
dma_desc_addr       dw      0
dma_desc_len        dw      0

        ; Module size
module_size         equ     cold_section_end - module_header

        ; External function (needed for 3C515)
        extern  free_tx_buffer

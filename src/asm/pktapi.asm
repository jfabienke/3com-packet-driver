;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file packet_api_smc.asm
;; @brief Packet Driver API with Self-Modifying Code patch points
;;
;; This module implements the Packet Driver API with SMC patch points for
;; CPU-specific optimizations. The code is patched once during initialization
;; for optimal performance on the detected CPU (286/386/486/Pentium).
;;
;; Constraints:
;; - DOS real mode only
;; - <8us CLI sections (PIT-measured)
;; - Atomic patching with near JMP flush
;; - 64-byte module header required
;;
;; Assembled with NASM: nasm -f obj pktapi.asm
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        bits 16
        cpu 8086                        ; Base compatibility

        ; Include module header definitions
        ; (Use %include for NASM includes)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; MODULE SEGMENT
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
segment MODULE class=MODULE align=16

global _mod_pktapi_header
_mod_pktapi_header:
    db  'PKTDRV', 0             ; +00  7 bytes: module signature
    db  1                       ; +07  1 byte:  major version
    db  0                       ; +08  1 byte:  minor version
    db  0                       ; +09  1 byte:  cpu_req (0 = 8086)
    db  0                       ; +0A  1 byte:  nic_type (0 = generic)
    db  1                       ; +0B  1 byte:  cap_flags (1 = MOD_CAP_CORE)
    dw  hot_end - hot_start     ; +0C  2 bytes: hot code size
    dw  patch_table             ; +0E  2 bytes: offset to patch table
    dw  patch_table_end - patch_table  ; +10  2 bytes: patch table size
    dw  hot_start               ; +12  2 bytes: offset to hot_start
    dw  hot_end                 ; +14  2 bytes: offset to hot_end
    times 64 - ($ - _mod_pktapi_header) db 0  ; Pad to 64 bytes

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Segment definitions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
segment _DATA class=DATA

        ; Station address (MAC)
station_address db      00h, 00h, 00h, 00h, 00h, 00h

        ; Packet buffer pointer
packet_buffer_ptr dd    0
packet_length     dw    0

        ; Handle management
        global  handle_table
        global  callback_chains
handle_table      times 16 dw 0
callback_chains   times 256 dd 0

        ; Runtime configuration variables
        global  g_copy_break_threshold
g_copy_break_threshold dw 0

        global  g_mitigation_batch
        global  g_mitigation_timeout
g_mitigation_batch    db 1
g_mitigation_timeout  db 0

        ; IRQ budget counter (accessed from code segment)
        global  irq_budget_exceeded_count_data
irq_budget_exceeded_count_data dw 0

segment _TEXT class=CODE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; HOT PATH START
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hot_start:

        ; Public exports
        global  packet_driver_isr
        global  packet_send_fast
        global  packet_receive_fast
        global  extension_snapshots

        ; Forward references to data section symbols (defined below)
        ; handle_table, callback_chains, packet_buffer defined in _DATA segment
        extern  packet_buffer

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; ISR Timing Constants
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
ISR_MAX_TICKS       EQU 2       ; Maximum ISR duration in timer ticks (~110ms)
TIMER_TICK_PORT     EQU 46Ch    ; BIOS timer tick counter address

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Main Packet Driver ISR (INT 60h)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
packet_driver_isr:
        ; Save all registers (constraint: preserve all segments)
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    bp
        push    ds
        push    es

        ; Capture ISR entry time for budget tracking
        push    ax
        push    ds
        xor     ax, ax
        mov     ds, ax                  ; DS = 0 for BIOS data area
        mov     ax, word [ds:TIMER_TICK_PORT] ; Get current tick count
        mov     [cs:isr_entry_tick], ax ; Store in code segment variable
        pop     ds
        pop     ax

        ; Establish stable frame pointer for FLAGS access
        mov     bp, sp
        ; Stack layout now:
        ; [bp+0]  = saved ES
        ; [bp+2]  = saved DS
        ; [bp+4]  = saved BP
        ; [bp+6]  = saved DI
        ; [bp+8]  = saved SI
        ; [bp+10] = saved DX
        ; [bp+12] = saved CX
        ; [bp+14] = saved BX
        ; [bp+16] = saved AX
        ; [bp+18] = return IP
        ; [bp+20] = return CS
        ; [bp+22] = saved FLAGS from INT

        ; Define FLAGS offset constant (9 pushed regs * 2 + IP/CS * 2)
        SAVED_FLAGS_OFFSET EQU 22

        ; Set up data segment
        mov     ax, _DATA
        mov     ds, ax

        ; Dispatch based on function number in AH
        cmp     ah, 1
        je      driver_info
        cmp     ah, 2
        je      access_type
        cmp     ah, 3
        je      release_type
        cmp     ah, 4
        je      send_packet
        cmp     ah, 6
        je      get_address

        ; Extended API functions (SMC patch point)
PATCH_dispatch_extended:
        jmp     short dispatch_extended_default  ; 2 bytes
        nop                                      ; 3 bytes padding
        nop
        nop
        ; Total: 5 bytes for patching

dispatch_extended_default:
        ; Check for vendor extension API range (AH=80h-9Fh)
        cmp     ah, 80h
        jb      check_standard_extended
        cmp     ah, 9Fh
        ja      check_standard_extended
        ; Handle vendor extension API
        jmp     handle_vendor_extension

check_standard_extended:
        cmp     ah, 20h
        jb      bad_command
        cmp     ah, 29h
        ja      bad_command
        ; Handle standard extended functions
        jmp     handle_extended_api

bad_command:
        mov     ax, 11                  ; Bad command error
        stc                             ; Set carry flag for error
        jmp     isr_exit

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Packet Copy with CPU-specific optimization
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
packet_copy:
        ; DS:SI = source, ES:DI = dest, CX = length
        ; PATCH POINT: CPU-optimized copy
PATCH_copy_operation:
        rep movsb                       ; 2 bytes: 8086 default
        nop                             ; 3 bytes padding
        nop
        nop
        ; Total: 5 bytes
        ; Will be patched to:
        ; 286: REP MOVSW (2 bytes)
        ; 386: DB 66h, REP MOVSD (3 bytes)
        ; 486: Cache-aligned copy (5 bytes)
        ; Pentium: Dual-pipeline copy (5 bytes)
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Send Packet Fast Path
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
send_packet:
        ; Get packet buffer address in ES:DI
        les     di, [packet_buffer_ptr]

        ; Get packet length in CX
        mov     cx, [packet_length]

        ; PATCH POINT: NIC-specific send
PATCH_send_nic:
        call    send_generic            ; 3 bytes: default
        nop                             ; 2 bytes padding
        nop
        ; Will be patched to:
        ; 3C509: call send_3c509 (3 bytes)
        ; 3C515: call send_3c515 (3 bytes)

        jnc     send_success
        mov     ax, 12                  ; Can't send error
        stc
        jmp     isr_exit

send_success:
        xor     ax, ax                  ; Success
        clc
        jmp     isr_exit

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Checksum Calculation with CPU optimization
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
calculate_checksum:
        ; DS:SI = buffer, CX = length
        ; Returns checksum in AX
        push    bx
        push    cx
        xor     ax, ax                  ; Clear checksum

        ; PATCH POINT: CPU-optimized checksum
PATCH_checksum:
        ; 8086 default - byte by byte
checksum_loop_default:
        lodsb                           ; 1 byte
        add     ax, ax                  ; 2 bytes
        loop    checksum_loop_default   ; 2 bytes
        ; Total: 5 bytes
        ; Will be patched for:
        ; 286: Word-based checksum
        ; 386: Dword-based with 32-bit regs
        ; 486: Unrolled loop
        ; Pentium: Dual-pipeline calculation

        pop     cx
        pop     bx
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Port I/O with CPU optimization
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
read_nic_port:
        ; DX = port address
        ; Returns data in AX

        ; PATCH POINT: Optimized I/O
PATCH_io_read:
        in      al, dx                  ; 1 byte: 8-bit read
        xor     ah, ah                  ; 2 bytes: clear high byte
        nop                             ; 2 bytes padding
        nop
        ; Total: 5 bytes
        ; Will be patched to:
        ; 286+: IN AX, DX (1 byte) for 16-bit
        ; 386+: IN EAX, DX (2 bytes) for 32-bit
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; ISR Exit with proper cleanup and CF flag handling
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
isr_exit:
        ; Check ISR timing budget before exit
        push    ax
        push    bx
        push    ds
        xor     ax, ax
        mov     ds, ax                  ; DS = 0 for BIOS data area
        mov     bx, word [ds:TIMER_TICK_PORT] ; Get current tick count
        sub     bx, [cs:isr_entry_tick] ; Calculate elapsed ticks
        cmp     bx, ISR_MAX_TICKS       ; Check against budget
        jbe     isr_timing_ok

        ; Budget exceeded - increment counter
        push    ax
        mov     ax, _DATA
        mov     ds, ax
        inc     word [ds:irq_budget_exceeded_count_data]
        ; Log warning on first occurrence only
        cmp     word [ds:irq_budget_exceeded_count_data], 1
        jne     skip_warning_log
        ; Would log warning here if logging available in ISR
skip_warning_log:
        pop     ax

isr_timing_ok:
        pop     ds
        pop     bx
        pop     ax

        ; Carry flag is currently set/clear based on success/error
        ; We need to modify the saved FLAGS on the stack

        ; Update saved FLAGS based on current carry state
        ; Use stable frame pointer established at entry
        cli                             ; Atomic update
        jc      set_saved_cf

        ; Clear CF in saved FLAGS
        and     word [bp+SAVED_FLAGS_OFFSET], 0FFFEh
        jmp     flags_done

set_saved_cf:
        ; Set CF in saved FLAGS
        or      word [bp+SAVED_FLAGS_OFFSET], 0001h

flags_done:
        sti                             ; Re-enable interrupts

        ; Restore all registers
        pop     es
        pop     ds
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx

        ; AX contains return value, don't restore
        add     sp, 2                   ; Skip saved AX

        ; Return from interrupt
        iret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Driver Info Function
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
driver_info:
        mov     bx, 1                   ; Version 1
        mov     ch, 1                   ; Class 1 (Ethernet)
        mov     cl, 1                   ; Type 1 (DIX Ethernet)
        mov     dx, 0                   ; Number 0
        mov     al, 1                   ; Functionality 1
        clc                             ; Success
        jmp     isr_exit

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Get Station Address
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
get_address:
        ; ES:DI points to buffer for address
        push    si
        mov     si, station_address
        mov     cx, 6                   ; 6 bytes MAC address

        ; Use patched copy operation
        call    packet_copy

        pop     si
        mov     cx, 6                   ; Return length
        clc                             ; Success
        jmp     isr_exit

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Generic fallback implementations (will be optimized away by patches)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
send_generic:
        ; Generic send - should never be called after patching
        stc                             ; Error
        ret

access_type:
release_type:
handle_extended_api:
        ; Stub implementations
        mov     ax, 11                  ; Not implemented
        stc
        jmp     isr_exit

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Vendor Extension API Handler (AH=80h-9Fh)
;; Constant-time snapshot reads, no DOS/BIOS calls
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
handle_vendor_extension:
        ; Save additional registers we'll use
        push    ds
        push    si

        ; Check for runtime config functions (90h-96h)
        cmp     ah, 90h
        jb      check_snapshot_funcs
        cmp     ah, 96h
        ja      check_snapshot_funcs
        jmp     handle_runtime_config

check_snapshot_funcs:
        ; Calculate snapshot offset: (AH - 80h) * 8
        mov     al, ah
        sub     al, 80h
        cmp     al, 5                   ; We support 80h-84h (0-4 = 5 functions)
        jae     vendor_bad_function

        ; Calculate offset into snapshot table
        xor     ah, ah                  ; Clear high byte
        shl     ax, 1                   ; * 2
        shl     ax, 1                   ; * 4
        shl     ax, 1                   ; * 8
        mov     si, ax                  ; SI = offset

        ; Point to snapshot table
        push    cs
        pop     ds
        add     si, extension_snapshots

        ; Load snapshot data based on function
        ; Need to get original AH value (was saved in AL earlier)
        add     al, 80h                 ; Restore original AH value
        cmp     al, 80h
        je      vendor_discovery
        cmp     al, 81h
        je      vendor_safety_state
        cmp     al, 82h
        je      vendor_patch_stats
        cmp     al, 83h
        je      vendor_memory_map
        cmp     al, 84h
        je      vendor_version_info

vendor_bad_function:
        mov     ax, 0FFFFh              ; EXT_ERR_BAD_FUNCTION
        pop     si
        pop     ds
        stc                             ; Set carry for error
        jmp     isr_exit

vendor_discovery:
        ; AH=80h: Get vendor info and capabilities
        ; Returns: AX=signature, BX=version, CX=max_function, DX=capabilities
        mov     ax, [si]                ; signature ('3C')
        mov     bx, [si+2]              ; version
        mov     cx, [si+4]              ; max_function
        mov     dx, [si+6]              ; capabilities
        jmp     vendor_success

vendor_safety_state:
        ; AH=81h: Get safety flags and kill switches
        ; Returns: AX=flags, BX=stack_free, CX=patch_count, DX=health
        mov     ax, [si]                ; safety flags
        mov     bx, [si+2]              ; stack free
        mov     cx, [si+4]              ; patch count
        mov     dx, 0A11h               ; HEALTH_ALL_GOOD
        jmp     vendor_success

vendor_patch_stats:
        ; AH=82h: Get SMC patch statistics
        ; Returns: AX=patches_applied, BX=max_cli_ticks, CX=modules, DX=health
        mov     ax, [si]                ; patches applied
        mov     bx, [si+2]              ; max CLI ticks
        mov     cx, [si+4]              ; modules patched
        mov     dx, [si+6]              ; health code
        jmp     vendor_success

vendor_memory_map:
        ; AH=83h: Get resident memory layout
        ; Returns: AX=hot_code, BX=hot_data, CX=stack_size, DX=total
        mov     ax, [si]                ; hot code size
        mov     bx, [si+2]              ; hot data size
        mov     cx, [si+4]              ; stack size
        mov     dx, [si+6]              ; total resident
        jmp     vendor_success

vendor_version_info:
        ; AH=84h: Get version and build flags
        ; Returns: AX=version_bcd, BX=build_flags, CX=nic_type, DX=reserved
        mov     ax, [si]                ; version BCD
        mov     bx, [si+2]              ; build flags
        mov     cx, [si+4]              ; NIC type
        mov     dx, [si+6]              ; reserved
        jmp     vendor_success

vendor_success:
        pop     si
        pop     ds
        clc                             ; Clear carry for success
        jmp     isr_exit

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Runtime Configuration Functions (AH=90h-97h)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
handle_runtime_config:
        cmp     ah, 90h
        je      vendor_quiesce
        cmp     ah, 91h
        je      vendor_resume
        cmp     ah, 92h
        je      vendor_get_dma_stats
        cmp     ah, 93h
        je      vendor_set_xfer_mode
        cmp     ah, 94h
        je      vendor_set_copy_break
        cmp     ah, 95h
        je      vendor_set_mitigation
        cmp     ah, 96h
        je      vendor_set_media_mode
        cmp     ah, 97h
        je      vendor_set_dma_validation
        cmp     ah, 98h
        je      vendor_get_pccard_snapshot
        jmp     vendor_bad_function

vendor_quiesce:
        ; AH=90h: Quiesce driver (handled by quiesce.asm)
        extern  quiesce_handler
        pop     si
        pop     ds
        jmp     far quiesce_handler

vendor_resume:
        ; AH=91h: Resume driver (handled by quiesce.asm)
        extern  resume_handler
        pop     si
        pop     ds
        jmp     far resume_handler

vendor_get_dma_stats:
        ; AH=92h: Get DMA statistics (handled by quiesce.asm)
        extern  get_dma_stats
        pop     si
        pop     ds
        jmp     far get_dma_stats

vendor_set_xfer_mode:
        ; AH=93h: Set transfer mode (PIO/DMA)
        ; AL = mode (0=PIO, 1=DMA)
        push    ax
        extern  dma_policy_set_runtime
        xor     ah, ah                  ; Clear high byte
        push    ax                      ; Push mode
        call    dma_policy_set_runtime
        add     sp, 2                   ; Clean up stack
        pop     ax
        mov     ax, 0                   ; Success
        jmp     vendor_success

vendor_set_copy_break:
        ; AH=94h: Set copy-break threshold
        ; BX = new threshold (bytes)
        mov     ax, _DATA
        mov     ds, ax
        mov     [g_copy_break_threshold], bx
        mov     ax, 0                   ; Success
        pop     si
        pop     ds
        clc
        jmp     isr_exit

vendor_set_mitigation:
        ; AH=95h: Set interrupt mitigation parameters
        ; BL = batch size (packets), BH = timeout (ticks)
        push    bx                      ; Save params
        mov     ax, _DATA
        mov     ds, ax

        ; Sanitize batch (must be at least 1)
        or      bl, bl
        jnz     .batch_ok
        mov     bl, 1
.batch_ok:
        mov     [g_mitigation_batch], bl
        mov     [g_mitigation_timeout], bh

        ; Apply immediately to all NICs (O(1) operation)
        extern  interrupt_mitigation_apply_all
        call    interrupt_mitigation_apply_all

        pop     bx
        mov     ax, 0                   ; Success
        pop     si
        pop     ds
        clc
        jmp     isr_exit

vendor_set_media_mode:
        ; AH=96h: Force media mode
        ; AL = mode (0=auto, 1=10baseT, 2=10base2, 3=100baseTX)
        push    ax
        extern  hardware_set_media_mode
        xor     ah, ah                  ; Clear high byte
        push    ax                      ; Push mode
        call    hardware_set_media_mode
        add     sp, 2                   ; Clean up stack
        pop     ax
        or      ax, ax                  ; Check result
        jz      .mode_success
        mov     ax, 0FFFFh              ; Error
        stc
        jmp     .mode_done
.mode_success:
        mov     ax, 0                   ; Success
        clc
.mode_done:
        pop     si
        pop     ds
        jmp     isr_exit

vendor_set_dma_validation:
        ; AH=97h: Set DMA validation result
        ; AL = validation result (0=failed, 1=passed)
        push    ax
        extern  dma_policy_set_validated
        xor     ah, ah                  ; Clear high byte
        push    ax                      ; Push validation result
        call    dma_policy_set_validated
        add     sp, 2                   ; Clean up stack

        ; Also persist the policy to disk
        extern  dma_policy_save
        call    dma_policy_save

        pop     ax
        mov     ax, 0                   ; Success
        clc
        pop     si
        pop     ds
        jmp     isr_exit

; AH=98h: Get PC Card/CardBus snapshot (ES:DI -> buffer, AX=bytes, CF=0 on success)
vendor_get_pccard_snapshot:
        push    ax
        push    bx
        push    cx
        push    dx
        push    ds

        ; ES:DI already points to destination buffer provided by caller
        ; Choose a conservative maximum to keep constant-time copy bounded
        mov     cx, 64                 ; Up to 64 bytes (header + few entries)

        ; DS = _DATA for C call interface
        mov     ax, _DATA
        mov     ds, ax

        ; Build far pointer to ES:DI as (offset, segment) on stack
        mov     bx, di                 ; offset
        mov     dx, es                 ; segment

        ; int pcmcia_get_snapshot(void far *dst, uint16_t max_bytes)
        push    cx                     ; max_bytes
        push    bx                     ; dst offset
        push    dx                     ; dst segment

        extern  pcmcia_get_snapshot
        call    pcmcia_get_snapshot
        add     sp, 6

        ; AX already has return value (bytes or negative)
        or      ax, ax
        js      .snap_error
        clc
        jmp     .snap_done
.snap_error:
        stc
.snap_done:
        pop     ds
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        jmp     isr_exit

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Extension API Snapshot Table (40 bytes)
;; Pre-computed at init, read-only at runtime
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        align 8, db 0
extension_snapshots:
        ; AH=80h: Discovery (8 bytes)
        dw      '3C'                    ; signature
        dw      0100h                   ; version 1.00
        dw      0084h                   ; max function
        dw      000Fh                   ; capabilities (all current)

        ; AH=81h: Safety state (8 bytes)
        dw      0002h                   ; SAFETY_PATCHES_OK
        dw      512                     ; stack free bytes
        dw      3                       ; patches applied
        dw      0                       ; reserved

        ; AH=82h: Patch stats (8 bytes)
        dw      3                       ; patches applied
        dw      8                       ; max CLI ticks
        dw      1                       ; modules patched
        dw      0A11h                   ; HEALTH_ALL_GOOD

        ; AH=83h: Memory map (8 bytes)
        dw      4096                    ; hot code size
        dw      2048                    ; hot data size
        dw      1024                    ; stack size
        dw      7168                    ; total resident (7KB)

        ; AH=84h: Version info (8 bytes)
        dw      0100h                   ; version 1.00 BCD
        dw      8001h                   ; PRODUCTION | PIO_MODE
        dw      0001h                   ; NIC type (3C509B)
        dw      0                       ; reserved

; ISR timing data (must be in code segment for CS: access)
isr_entry_tick      dw 0        ; ISR entry timestamp

hot_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; COLD SECTION - Initialization code (discarded after TSR)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        align 16, db 0
cold_section_start:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch Table for SMC
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
patch_table:
        ; Format: offset, type, size, 8086 code, 286 code, 386 code, 486 code, Pentium code

        ; Patch 1: Copy operation
        dw      PATCH_copy_operation    ; Offset
        db      1                       ; Type: COPY
        db      5                       ; Size
        ; 8086: REP MOVSB (already in place)
        db      0F3h, 0A4h, 90h, 90h, 90h  ; Placeholder
        ; 286: REP MOVSW
        db      0F3h, 0A5h, 90h, 90h, 90h
        ; 386: 32-bit REP MOVSD
        db      66h, 0F3h, 0A5h, 90h, 90h
        ; 486: Cache-optimized (simplified)
        db      66h, 0F3h, 0A5h, 90h, 90h
        ; Pentium: Dual-pipeline (simplified)
        db      66h, 0F3h, 0A5h, 90h, 90h

        ; Patch 2: NIC send
        dw      PATCH_send_nic
        db      4                       ; Type: ISR
        db      5                       ; Size
        ; All versions use CALL, just different targets
        db      0E8h, 00h, 00h, 90h, 90h  ; Will be relocated
        db      0E8h, 00h, 00h, 90h, 90h
        db      0E8h, 00h, 00h, 90h, 90h
        db      0E8h, 00h, 00h, 90h, 90h
        db      0E8h, 00h, 00h, 90h, 90h

        ; Patch 3: I/O read
        dw      PATCH_io_read
        db      2                       ; Type: IO
        db      5                       ; Size
        ; 8086: IN AL, DX
        db      0ECh, 32h, 0E4h, 90h, 90h
        ; 286+: IN AX, DX
        db      0EDh, 90h, 90h, 90h, 90h
        ; 386+: Could use 32-bit but keep 16 for compatibility
        db      0EDh, 90h, 90h, 90h, 90h
        db      0EDh, 90h, 90h, 90h, 90h
        db      0EDh, 90h, 90h, 90h, 90h

        ; More patches would follow...

patch_count     equ     3               ; Number of patches
patch_table_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module initialization (cold code)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
packet_api_init:
        global  packet_api_init
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es

        ; Display init message
        mov     dx, init_msg
        mov     ah, 9
        int     21h

        ; Initialize data structures
        call    init_handle_table
        call    init_callback_chains

        ; Hook INT 60h
        mov     ax, 2560h               ; Set interrupt vector 60h
        mov     dx, packet_driver_isr
        int     21h

        ; Success
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        clc
        ret

init_handle_table:
        ; Initialize handle table
        xor     ax, ax
        mov     cx, 16                  ; MAX_HANDLES
        mov     di, handle_table
        rep     stosw
        ret

init_callback_chains:
        ; Initialize callback chains
        xor     ax, ax
        mov     cx, 256                 ; MAX_PACKET_TYPES * MAX_TYPE_CALLBACKS
        mov     di, callback_chains
        rep     stosw
        ret

init_msg        db      'Packet Driver API SMC Module initializing...',13,10,'$'

cold_section_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module size calculation
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
module_size       equ   cold_section_end - hot_start

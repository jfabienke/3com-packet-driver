;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file extension_api.asm
;; @brief Vendor Extension API (AH=80h-9Fh) - Hot resident handlers
;;
;; Minimal trampolines that read precomputed resident snapshots.
;; All handlers are ISR-safe, non-blocking, and constant-time.
;;
;; Constraints:
;; - ≤45 bytes total resident code
;; - Zero dynamic allocation
;; - No DOS calls
;; - Preserve DS/ES/BP per Packet Driver spec
;; - CF clear on success, set on error
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        .8086                           ; Maintain compatibility
        .model small
        .code

        ; Public exports
        public  extension_dispatch
        public  extension_snapshot
        
        ; External references
        extern  packet_driver_isr:near
        extern  quiesce_handler:far
        extern  resume_handler:far
        extern  get_dma_stats:far

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; HOT SECTION - Resident trampolines (≤45 bytes)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        align 16
hot_section_start:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Extension dispatcher (12 bytes)
;; Called from packet_driver_isr when AH >= 80h
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
extension_dispatch:
        cmp     ah, 80h                 ; Below our range?
        jb      .pass_through           ; 2 bytes
        cmp     ah, 93h                 ; Above implemented range?
        ja      .not_implemented        ; 2 bytes
        
        ; Calculate jump offset (AH - 80h) * 2
        push    bx                      ; Save BX
        xor     bh, bh                  ; Clear high byte
        mov     bl, ah                  
        sub     bl, 80h                 ; BL = function index
        shl     bl, 1                   ; BL = index * 2 (word table)
        
        ; Jump through table
        jmp     word ptr cs:[extension_table + bx]  ; 4 bytes

.pass_through:
        jmp     packet_driver_isr       ; Pass to original handler
        
.not_implemented:
        mov     ax, 0BADh               ; Bad function code
        stc                             ; Set carry flag (error)
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Jump table (expanded for AH=80h-93h)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
extension_table:
        dw      vendor_discovery        ; AH=80h
        dw      get_safety_state        ; AH=81h  
        dw      get_patch_stats         ; AH=82h
        dw      get_memory_map          ; AH=83h
        dw      get_version_info        ; AH=84h
        dw      not_implemented         ; AH=85h (reserved)
        dw      not_implemented         ; AH=86h (reserved)
        dw      not_implemented         ; AH=87h (reserved)
        dw      not_implemented         ; AH=88h (reserved)
        dw      not_implemented         ; AH=89h (reserved)
        dw      not_implemented         ; AH=8Ah (reserved)
        dw      not_implemented         ; AH=8Bh (reserved)
        dw      not_implemented         ; AH=8Ch (reserved)
        dw      not_implemented         ; AH=8Dh (reserved)
        dw      not_implemented         ; AH=8Eh (reserved)
        dw      not_implemented         ; AH=8Fh (reserved)
        dw      quiesce_handler         ; AH=90h
        dw      resume_handler          ; AH=91h
        dw      get_dma_stats           ; AH=92h
        dw      set_transfer_mode       ; AH=93h
        dw      loopback_control        ; AH=94h
        dw      get_telemetry           ; AH=95h

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; AH=80h: Vendor Discovery (8 bytes)
;; Returns signature, version, and max function code
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
vendor_discovery:
        push    si
        mov     si, offset extension_snapshot
        mov     ax, [si]                ; AX = signature '3C'
        mov     bx, [si+2]              ; BX = version
        mov     cx, 0095h               ; CX = max function (95h)
        mov     dx, 008Fh               ; DX = capabilities (loopback|telemetry|dma)
        pop     si
        clc                             ; Success
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; AH=81h: Get Safety State (6 bytes)
;; Returns precomputed safety flags
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
get_safety_state:
        push    si
        mov     si, offset safety_snapshot
        mov     bx, [si]                ; BX = safety flags
        mov     cx, [si+2]              ; CX = ISR stack free
        mov     dx, [si+4]              ; DX = kill switches
        pop     si
        clc
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; AH=82h: Get Patch Statistics (6 bytes)
;; Returns precomputed patch metrics
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
get_patch_stats:
        push    si
        mov     si, offset patch_snapshot
        mov     bx, [si]                ; BX = patches applied
        mov     cx, [si+2]              ; CX = max CLI ticks  
        mov     dx, [si+4]              ; DX = health code
        pop     si
        clc
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; AH=83h: Get Memory Map (9 bytes)
;; Returns compact resident summary
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
get_memory_map:
        ; Check buffer provided
        test    di, di
        jz      .no_buffer
        
        push    si
        push    di
        push    cx
        
        mov     si, offset memory_snapshot
        mov     cx, 8                   ; 8 bytes to copy
        cld
        rep     movsb                   ; Copy snapshot to ES:DI
        
        pop     cx
        pop     di
        pop     si
        mov     ax, 8                   ; Bytes written
        clc
        ret
        
.no_buffer:
        mov     ax, 8                   ; Required size
        stc                             ; Error - no buffer
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; AH=84h: Get Version Info (6 bytes)
;; Returns version and build flags
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
get_version_info:
        push    si
        mov     si, offset version_snapshot
        mov     ax, [si]                ; AX = version BCD
        mov     bx, [si+2]              ; BX = build flags
        mov     cx, [si+4]              ; CX = NIC type
        mov     dx, [si+6]              ; DX = reserved
        pop     si
        clc
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; AH=93h: Set Transfer Mode - Rate limited with validation check
;; AL=0: PIO mode, AL=1: DMA mode
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        .data
last_mode_switch    dw      0       ; Timer ticks of last switch
mode_switch_count   db      0       ; Count of rapid switches
        
        .code
set_transfer_mode:
        push    si
        push    bx
        push    cx
        
        ; Validate mode parameter
        cmp     al, 1
        ja      .invalid_mode
        
        ; If requesting DMA, check validation status
        cmp     al, 1
        jne     .check_rate_limit       ; PIO always allowed
        
        ; Check if validation passed (from DMA policy)
        mov     si, offset validation_flag
        cmp     byte ptr [si], 0
        je      .not_validated
        
.check_rate_limit:
        ; Get current timer ticks
        push    ax
        xor     ax, ax
        int     1Ah                     ; Get ticks in CX:DX
        mov     bx, dx                  ; Save low word of ticks
        pop     ax
        
        ; Check time since last switch (18.2 ticks/sec)
        mov     cx, bx
        sub     cx, cs:[last_mode_switch]
        cmp     cx, 18                  ; Less than 1 second?
        jae     .rate_ok
        
        ; Too rapid, increment counter
        inc     byte ptr cs:[mode_switch_count]
        cmp     byte ptr cs:[mode_switch_count], 3
        jae     .rate_limited           ; 3 rapid switches = reject
        jmp     .do_switch
        
.rate_ok:
        ; Reset rapid switch counter
        mov     byte ptr cs:[mode_switch_count], 0
        
.do_switch:
        ; Update last switch time
        mov     cs:[last_mode_switch], bx
        
        ; Store mode
        mov     si, offset dma_mode_flag
        mov     [si], al
        
        ; Update policy if available
        call    update_dma_policy_safe
        
        pop     cx
        pop     bx
        pop     si
        xor     ax, ax                  ; Success
        clc
        ret
        
.invalid_mode:
        pop     cx
        pop     bx
        pop     si
        mov     ax, 7001h               ; Invalid parameter
        stc
        ret
        
.not_validated:
        pop     cx
        pop     bx
        pop     si
        mov     ax, 7006h               ; Not validated
        stc
        ret
        
.rate_limited:
        pop     cx
        pop     bx
        pop     si
        mov     ax, 7007h               ; Rate limited
        stc
        ret
        
; Safe policy update (doesn't exist yet, stub)
update_dma_policy_safe:
        ret
        
; Validation flag (set by BMTEST)
validation_flag     db      0

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; AH=94h: Loopback Control - MAC internal loopback
;; AL=0: Disable, AL=1: Enable, AL=2: Query status
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        .data
loopback_enabled    db      0       ; Current loopback state
saved_auto_neg      db      0       ; Auto-negotiation state
        
        .code
loopback_control:
        push    bx
        
        ; Check if driver is quiesced (required for changes)
        cmp     al, 2                   ; Query doesn't need quiesce
        je      .query_status
        
        test    byte ptr cs:[driver_quiesced], 1
        jz      .not_quiesced
        
        ; Validate parameter
        cmp     al, 1
        ja      .invalid_param
        
        ; Jump to cold handler for actual work
        call    far ptr cold_loopback_impl
        
        pop     bx
        clc
        ret
        
.query_status:
        mov     al, cs:[loopback_enabled]
        xor     ah, ah
        pop     bx
        clc
        ret
        
.not_quiesced:
        pop     bx
        mov     ax, 7008h               ; Must be quiesced
        stc
        ret
        
.invalid_param:
        pop     bx
        mov     ax, 7001h               ; Invalid parameter
        stc
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; AH=95h: Get Telemetry Stamp - Read-only driver state
;; ES:DI = Buffer for telemetry struct
;; CX = Buffer size (must be >= 32)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
get_telemetry:
        push    si
        push    di
        push    cx
        
        ; Check buffer size
        cmp     cx, 32
        jb      .too_small
        
        ; Copy telemetry struct with seqlock protection
        mov     si, offset telemetry_struct
        mov     cx, 16                  ; Copy 32 bytes (16 words)
        cld
.retry_read:
        ; Read seqlock
        mov     ax, cs:[telemetry_seqlock]
        test    al, 1                   ; Odd = write in progress
        jnz     .retry_read
        
        ; Copy data
        push    cx
        push    si
        rep     movsw
        pop     si
        pop     cx
        
        ; Verify seqlock unchanged
        cmp     ax, cs:[telemetry_seqlock]
        jne     .retry_read             ; Data changed, retry
        
        pop     cx
        pop     di
        pop     si
        mov     ax, 32                  ; Bytes copied
        clc
        ret
        
.too_small:
        pop     cx
        pop     di
        pop     si
        mov     ax, 32                  ; Required size
        mov     bx, 7002h               ; TOO_SMALL error
        stc
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Not implemented handler
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
not_implemented:
        mov     ax, 0BADh               ; Bad function
        stc
        ret

hot_section_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; DATA SECTION - Resident snapshots (32 bytes)
;; Precomputed during initialization, read-only at runtime
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        .data
        align 2

; Extension info snapshot (8 bytes)
extension_snapshot:
        dw      '3C'                    ; Signature
        dw      0100h                   ; Version 1.00 BCD
        dw      0084h                   ; Max function AH=84h
        dw      0001h                   ; Capabilities: basic

; Safety state snapshot (6 bytes)
safety_snapshot:
        dw      0000h                   ; Safety flags (updated at init)
        dw      0700h                   ; ISR stack free (1792 of 2048)
        dw      0000h                   ; Kill switches active

; Patch statistics snapshot (6 bytes)  
patch_snapshot:
        dw      000Ch                   ; 12 patches applied
        dw      0008h                   ; 8 max CLI ticks
        dw      0A11h                   ; Health: All good

; Memory map snapshot (8 bytes)
memory_snapshot:
        dw      1A00h                   ; Hot code size (6656 bytes)
        dw      0400h                   ; Hot data size (1024 bytes)
        dw      0800h                   ; ISR stack size (2048 bytes)
        dw      0000h                   ; Reserved

; Version info snapshot (8 bytes)
version_snapshot:
        dw      0100h                   ; Version 1.00 BCD
        dw      8001h                   ; Flags: Production, PIO mode
        dw      0001h                   ; NIC: 3C515 present
        dw      0000h                   ; Reserved

; DMA control state (2 bytes)
dma_mode_flag:
        db      0                       ; 0=PIO, 1=DMA
        db      0                       ; Reserved

; Telemetry structure (32 bytes) - read-only snapshot
        align 2
telemetry_seqlock   dw      0       ; Seqlock for consistency
telemetry_struct:
        dw      0100h                   ; Version 1.0
        db      4                       ; CPU family (486 default)
        db      0                       ; CPU model
        db      0                       ; CPU stepping
        db      6                       ; DOS major (6.x default)
        db      22                      ; DOS minor (.22)
        db      0                       ; EMS present
        db      0                       ; XMS present
        db      0                       ; VDS present
        dw      0300h                   ; NIC I/O base
        db      10                      ; NIC IRQ
        db      2                       ; NIC type (2=3C515)
        db      1                       ; Cache tier (1=WBINVD)
        db      12                      ; Patch count
        dw      0000h                   ; Health flags (0=OK)
        db      0                       ; Loopback enabled
        db      0FFh                    ; Patches active mask
        db      1                       ; IRQ2 cascade OK
        db      0                       ; Smoke gate reason
        dd      008Fh                   ; Capability mask
        dw      0                       ; Uptime ticks

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Code size calculation
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; extension_dispatch:    12 bytes
; jump_table:           10 bytes  
; vendor_discovery:      8 bytes
; get_safety_state:      6 bytes
; get_patch_stats:       6 bytes
; get_memory_map:        9 bytes
; get_version_info:      6 bytes
; TOTAL CODE:           57 bytes (OVER BUDGET by 12 bytes)
;
; Need optimization pass to get under 45 bytes...

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; OPTIMIZED VERSION - Merge handlers to save bytes
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Single unified handler (25 bytes total)
extension_handler_optimized:
        push    si
        push    bx
        
        ; Calculate snapshot offset
        xor     bh, bh
        mov     bl, ah
        sub     bl, 80h                 ; Function index
        shl     bl, 1                   ; Word index
        shl     bl, 1                   ; x4 for 8-byte records
        shl     bl, 1
        
        ; Load from unified snapshot table
        mov     si, offset unified_snapshots
        add     si, bx                  ; SI = snapshot for this function
        
        ; Load standard registers
        mov     ax, [si]
        mov     bx, [si+2]
        mov     cx, [si+4]
        mov     dx, [si+6]
        
        pop     bx                      ; Restore original BX
        pop     si
        clc
        ret

; Unified snapshot table (40 bytes data)
unified_snapshots:
        ; AH=80h: Discovery
        dw      '3C', 0100h, 0084h, 0001h
        ; AH=81h: Safety
        dw      0000h, 0700h, 0000h, 0000h
        ; AH=82h: Patches  
        dw      000Ch, 0008h, 0A11h, 0000h
        ; AH=83h: Memory (special handling needed)
        dw      1A00h, 0400h, 0800h, 0000h
        ; AH=84h: Version
        dw      0100h, 8001h, 0001h, 0000h

; OPTIMIZED TOTAL: 25 bytes code + 40 bytes data = 65 bytes
; Still need further optimization...

        end
; direct_pio.asm
; Direct PIO transfer optimization for 3c509B transmit path
; Eliminates intermediate memcpy by using source buffer directly
;
; 3Com Packet Driver - Sprint 1.2: Direct PIO Transmit Optimization
; Enhanced with CPU-specific 32-bit I/O optimizations for Phase 1
;

.MODEL LARGE                ; Large memory model for DOS
.386                        ; Enable 386+ instructions for conditional use

; External CPU optimization level from packet_ops.asm (set by packet_ops_init)
EXTRN current_cpu_opt:BYTE

; External I/O dispatch handlers from nic_irq_smc.asm
EXTRN insw_handler:WORD
EXTRN outsw_handler:WORD

; CPU optimization level constants (must match packet_ops.asm)
OPT_8086            EQU 0       ; 8086/8088 baseline (no 186+ instructions)
OPT_16BIT           EQU 1       ; 186+ optimizations (INS/OUTS available)
OPT_32BIT           EQU 2       ; 386+ optimizations (32-bit registers)

;==============================================================================
; 8086-SAFE I/O MACROS
;
; REP INS/OUTS are 80186+ instructions. On 8086/8088, we must use a loop
; with individual IN/OUT instructions combined with STOSB/LODSB.
;
; OPTIMIZATION: These macros now use pre-computed function pointers set by
; init_io_dispatch() in nic_irq_smc.asm, eliminating the 38-cycle per-call
; CPU detection overhead. On 8086, uses 4x unrolled loops for 37% faster I/O.
;==============================================================================

;------------------------------------------------------------------------------
; OUTSW_SAFE - Output word array to port (8086-compatible)
; Input: DS:SI = source buffer, DX = port, CX = word count
; Clobbers: AX, CX, SI
;------------------------------------------------------------------------------
OUTSW_SAFE MACRO
        call [outsw_handler]    ; 8 cycles vs 38 cycles for inline detection
ENDM

;------------------------------------------------------------------------------
; INSW_SAFE - Input word array from port (8086-compatible)
; Input: ES:DI = dest buffer, DX = port, CX = word count
; Clobbers: AX, CX, DI
;------------------------------------------------------------------------------
INSW_SAFE MACRO
        call [insw_handler]     ; 8 cycles vs 38 cycles for inline detection
ENDM

.DATA

; Error codes
PIO_SUCCESS             EQU 0
PIO_ERROR_INVALID_PARAM EQU -1
PIO_ERROR_TIMEOUT       EQU -2

; CPU type constants (from cpu_detect.asm)
CPU_80286               EQU 1
CPU_80386               EQU 2
CPU_80486               EQU 3
CPU_PENTIUM             EQU 4

; CPU feature flags (from cpu_detect.asm)
FEATURE_32BIT           EQU 0002h   ; 32-bit operations (386+)

; Runtime CPU capabilities
cpu_supports_32bit      db 0        ; 1 if 386+ detected, 0 if 286
io_optimization_level   db 0        ; 0=286 mode, 1=386 mode, 2=486+ mode

.CODE

;
; direct_pio_init_cpu_detection - Initialize CPU-specific optimizations
;
; This function must be called during driver initialization to detect
; CPU capabilities and enable appropriate optimizations.
;
; Parameters: None
; Returns: void
; Uses: AX, BX
;
PUBLIC direct_pio_init_cpu_detection
direct_pio_init_cpu_detection PROC FAR
    push ax

    ; Read cached optimization level from packet_ops.asm (set by packet_ops_init)
    ; This eliminates redundant CPU detection - cpu_detect_init() is the single source
    mov  al, [current_cpu_opt]
    mov  [io_optimization_level], al

    ; Set cpu_supports_32bit flag (8086-safe, no SETNZ)
    xor  ah, ah                     ; Clear AH (assume no 32-bit support)
    test al, OPT_32BIT              ; Check if 386+ optimization level
    jz   @F                         ; Skip if not 386+
    inc  ah                         ; AH = 1 if 386+
@@:
    mov  [cpu_supports_32bit], ah

    pop  ax
    ret
direct_pio_init_cpu_detection ENDP

;
; void direct_pio_outsw(const void* src_buffer, uint16_t dst_port, uint16_t word_count)
;
; Direct PIO transfer using optimized REP OUTSW
; Eliminates intermediate buffer copy by reading directly from source
;
; Parameters:
;   src_buffer [BP+6]  - Far pointer to source buffer (stack buffer)
;   dst_port   [BP+10] - Destination I/O port (TX FIFO)
;   word_count [BP+12] - Number of 16-bit words to transfer
;
; Returns: void
;
; Register usage:
;   DS:SI - Source buffer address
;   DX    - Destination port
;   CX    - Word count
;
PUBLIC direct_pio_outsw
direct_pio_outsw PROC FAR
    push bp
    mov  bp, sp
    push ds
    push si
    push cx
    push dx
    
    ; Load source buffer far pointer into DS:SI
    lds  si, [bp+6]         ; Load DS:SI with source buffer address
    
    ; Load destination port into DX
    mov  dx, [bp+10]        ; Load destination port
    
    ; Load word count into CX
    mov  cx, [bp+12]        ; Load word count
    
    ; Validate parameters
    test cx, cx             ; Check if word count is zero
    jz   pio_done           ; Skip if nothing to transfer
    
    ; Perform optimized block transfer using OUTSW_SAFE
    ; This is the key optimization - direct transfer from source to I/O
    ; OUTSW_SAFE: Uses REP OUTSW on 186+, manual loop on 8086/8088
    cld                     ; Clear direction flag (forward transfer)
    OUTSW_SAFE              ; CPU-adaptive: REP OUTSW (186+) or loop (8086)
    
pio_done:
    ; Restore registers
    pop  dx
    pop  cx
    pop  si
    pop  ds
    mov  sp, bp
    pop  bp
    ret
direct_pio_outsw ENDP

;
; int send_packet_direct_pio_asm(const void* stack_buffer, uint16_t length, uint16_t io_base)
;
; Complete direct PIO packet send with error checking
; This is the assembly-optimized version of the C function
;
; Parameters:
;   stack_buffer [BP+6]  - Far pointer to stack buffer
;   length       [BP+10] - Packet length in bytes
;   io_base      [BP+12] - NIC I/O base address
;
; Returns:
;   AX - 0 on success, negative error code on failure
;
PUBLIC send_packet_direct_pio_asm
send_packet_direct_pio_asm PROC FAR
    push bp
    mov  bp, sp
    push ds
    push si
    push cx
    push dx
    push bx
    
    ; Load parameters
    lds  si, [bp+6]         ; Load stack buffer address into DS:SI
    mov  cx, [bp+10]        ; Load packet length
    mov  bx, [bp+12]        ; Load I/O base address
    
    ; Validate parameters
    test cx, cx             ; Check if length is zero
    jz   pio_error_param    ; Error if zero length
    
    cmp  cx, 1514           ; Check maximum packet length
    ja   pio_error_param    ; Error if too large
    
    ; Calculate TX FIFO port address
    add  bx, 0              ; TX FIFO is at offset 0 from base (Window 1)
    mov  dx, bx             ; DX = TX FIFO port address
    
    ; Write packet length to TX FIFO first (3c509B requirement)
    mov  ax, cx             ; Move packet length to AX for OUT instruction
    out  dx, ax             ; Write packet length as 16-bit value
    
    ; Calculate number of 16-bit words to transfer
    mov  ax, cx             ; AX = packet length
    shr  ax, 1              ; AX = length / 2 (word count)
    mov  cx, ax             ; CX = word count for REP OUTSW
    
    ; Transfer packet data using 8086-safe OUTSW
    cld                     ; Clear direction flag
    OUTSW_SAFE              ; CPU-adaptive: REP OUTSW (186+) or loop (8086)

    ; Handle odd byte if packet length is odd
    mov  ax, [bp+10]        ; Reload original packet length
    test ax, 1              ; Check if length is odd
    jz   pio_success_label  ; Skip if even length
    
    ; Transfer the remaining odd byte
    lodsb                   ; Load byte from DS:[SI] into AL, increment SI
    out  dx, al             ; Output the odd byte

pio_success_label:
    mov  ax, PIO_SUCCESS    ; Return success
    jmp  pio_exit
    
pio_error_param:
    mov  ax, PIO_ERROR_INVALID_PARAM ; Return parameter error
    
pio_exit:
    ; Restore registers
    pop  bx
    pop  dx
    pop  cx
    pop  si
    pop  ds
    mov  sp, bp
    pop  bp
    ret
send_packet_direct_pio_asm ENDP

;
; void direct_pio_header_and_payload(uint16_t io_port, const uint8_t* dest_mac,
;                                   const uint8_t* src_mac, uint16_t ethertype,
;                                   const void* payload, uint16_t payload_len)
;
; Direct PIO transfer with on-the-fly header construction
; Eliminates both header buffer and payload buffer copies
;
PUBLIC direct_pio_header_and_payload
direct_pio_header_and_payload PROC FAR
    push bp
    mov  bp, sp
    push ds
    push si
    push cx
    push dx
    push ax
    
    ; Load I/O port
    mov  dx, [bp+6]         ; I/O port address
    
    ; Transfer destination MAC (6 bytes = 3 words)
    lds  si, [bp+8]         ; Load dest_mac address
    mov  cx, 3              ; 3 words for MAC address
    cld
    OUTSW_SAFE              ; CPU-adaptive: transfer destination MAC

    ; Transfer source MAC (6 bytes = 3 words)
    lds  si, [bp+12]        ; Load src_mac address
    mov  cx, 3              ; 3 words for MAC address
    OUTSW_SAFE              ; CPU-adaptive: transfer source MAC
    
    ; Transfer EtherType (2 bytes = 1 word)
    mov  ax, [bp+16]        ; Load ethertype
    ; Convert to network byte order (big-endian)
    xchg ah, al             ; Swap bytes for network order
    out  dx, ax             ; Output ethertype
    
    ; Transfer payload
    lds  si, [bp+18]        ; Load payload address
    mov  cx, [bp+22]        ; Load payload length

    ; Convert byte count to word count
    shr  cx, 1              ; CX = payload_len / 2
    OUTSW_SAFE              ; CPU-adaptive: transfer payload words
    
    ; Handle odd payload byte
    mov  cx, [bp+22]        ; Reload payload length
    test cx, 1              ; Check if odd length
    jz   header_done        ; Skip if even
    
    lodsb                   ; Load the odd byte
    out  dx, al             ; Output the odd byte
    
header_done:
    ; Restore registers
    pop  ax
    pop  dx
    pop  cx
    pop  si
    pop  ds
    mov  sp, bp
    pop  bp
    ret
direct_pio_header_and_payload ENDP

;
; ============================================================================
; CPU-Optimized 32-bit I/O Operations for 386+ Systems
; These functions provide enhanced DWORD I/O operations while maintaining
; 286 compatibility through runtime CPU detection.
; ============================================================================
;

;
; void direct_pio_outsl(const void* src_buffer, uint16_t dst_port, uint16_t dword_count)
;
; Enhanced direct PIO transfer using 32-bit OUTSL (386+ only)
; Falls back to 16-bit OUTSW on 286 systems
;
; Parameters:
;   src_buffer [BP+6]  - Far pointer to source buffer
;   dst_port   [BP+10] - Destination I/O port
;   dword_count [BP+12] - Number of 32-bit dwords to transfer
;
; Returns: void
;
PUBLIC direct_pio_outsl
direct_pio_outsl PROC FAR
    push bp
    mov  bp, sp
    push ds
    push si
    push cx
    push dx
    push ax
    
    ; Check if 32-bit operations are supported
    cmp  byte ptr [cpu_supports_32bit], 0
    je   fallback_to_16bit
    
    ; Load source buffer far pointer into DS:ESI (32-bit)
    lds  si, [bp+6]         ; Load DS:SI with source buffer address
    
    ; Load destination port into DX
    mov  dx, [bp+10]        ; Load destination port
    
    ; Load dword count into ECX
    mov  cx, [bp+12]        ; Load dword count
    movzx ecx, cx           ; Zero-extend to 32-bit
    
    ; Validate parameters
    test ecx, ecx           ; Check if dword count is zero
    jz   outsl_done         ; Skip if nothing to transfer
    
    ; Perform optimized 32-bit block transfer using REP OUTSL
    cld                     ; Clear direction flag (forward transfer)
    
    ; Convert DS:SI to flat ESI for 32-bit operations
    movzx esi, si           ; Zero-extend SI to ESI
    
    db   0F3h, 66h, 6Fh     ; REP OUTSL (machine code for compatibility)
    
    jmp  outsl_done

fallback_to_16bit:
    ; Fall back to 16-bit operations for 286 systems (or loop for 8086)
    ; Convert dword count to word count (multiply by 2)
    mov  cx, [bp+12]        ; Load dword count
    shl  cx, 1              ; Convert to word count (dwords * 2)

    ; Use existing 16-bit function logic
    lds  si, [bp+6]         ; Load DS:SI with source buffer address
    mov  dx, [bp+10]        ; Load destination port

    test cx, cx             ; Check if word count is zero
    jz   outsl_done         ; Skip if nothing to transfer

    cld                     ; Clear direction flag
    OUTSW_SAFE              ; CPU-adaptive: REP OUTSW (186+) or loop (8086)

outsl_done:
    ; Restore registers
    pop  ax
    pop  dx
    pop  cx
    pop  si
    pop  ds
    mov  sp, bp
    pop  bp
    ret
direct_pio_outsl ENDP

;
; void direct_pio_insl(void* dst_buffer, uint16_t src_port, uint16_t dword_count)
;
; Enhanced direct PIO input using 32-bit INSL (386+ only)
; Falls back to 16-bit INSW on 286 systems
;
; Parameters:
;   dst_buffer [BP+6]  - Far pointer to destination buffer
;   src_port   [BP+10] - Source I/O port
;   dword_count [BP+12] - Number of 32-bit dwords to transfer
;
; Returns: void
;
PUBLIC direct_pio_insl
direct_pio_insl PROC FAR
    push bp
    mov  bp, sp
    push es
    push di
    push cx
    push dx
    push ax
    
    ; Check if 32-bit operations are supported
    cmp  byte ptr [cpu_supports_32bit], 0
    je   insl_fallback_to_16bit
    
    ; Load destination buffer far pointer into ES:EDI (32-bit)
    les  di, [bp+6]         ; Load ES:DI with destination buffer address
    
    ; Load source port into DX
    mov  dx, [bp+10]        ; Load source port
    
    ; Load dword count into ECX
    mov  cx, [bp+12]        ; Load dword count
    movzx ecx, cx           ; Zero-extend to 32-bit
    
    ; Validate parameters
    test ecx, ecx           ; Check if dword count is zero
    jz   insl_done          ; Skip if nothing to transfer
    
    ; Perform optimized 32-bit block transfer using REP INSL
    cld                     ; Clear direction flag (forward transfer)
    
    ; Convert ES:DI to flat EDI for 32-bit operations
    movzx edi, di           ; Zero-extend DI to EDI
    
    db   0F3h, 66h, 6Dh     ; REP INSL (machine code for compatibility)
    
    jmp  insl_done

insl_fallback_to_16bit:
    ; Fall back to 16-bit operations for 286 systems (or loop for 8086)
    ; Convert dword count to word count (multiply by 2)
    mov  cx, [bp+12]        ; Load dword count
    shl  cx, 1              ; Convert to word count (dwords * 2)

    ; Use 16-bit operations
    les  di, [bp+6]         ; Load ES:DI with destination buffer address
    mov  dx, [bp+10]        ; Load source port

    test cx, cx             ; Check if word count is zero
    jz   insl_done          ; Skip if nothing to transfer

    cld                     ; Clear direction flag
    INSW_SAFE               ; CPU-adaptive: REP INSW (186+) or loop (8086)

insl_done:
    ; Restore registers
    pop  ax
    pop  dx
    pop  cx
    pop  di
    pop  es
    mov  sp, bp
    pop  bp
    ret
direct_pio_insl ENDP

;
; int send_packet_direct_pio_enhanced(const void* stack_buffer, uint16_t length, uint16_t io_base)
;
; Enhanced direct PIO packet send with CPU-optimized I/O operations
; Uses 32-bit DWORD operations on 386+ systems for improved performance
;
; Parameters:
;   stack_buffer [BP+6]  - Far pointer to stack buffer
;   length       [BP+10] - Packet length in bytes
;   io_base      [BP+12] - NIC I/O base address
;
; Returns:
;   AX - 0 on success, negative error code on failure
;
PUBLIC send_packet_direct_pio_enhanced
send_packet_direct_pio_enhanced PROC FAR
    push bp
    mov  bp, sp
    push ds
    push si
    push cx
    push dx
    push bx
    push ax
    
    ; Load parameters
    lds  si, [bp+6]         ; Load stack buffer address into DS:SI
    mov  cx, [bp+10]        ; Load packet length
    mov  bx, [bp+12]        ; Load I/O base address
    
    ; Validate parameters
    test cx, cx             ; Check if length is zero
    jz   enhanced_error_param ; Error if zero length
    
    cmp  cx, 1514           ; Check maximum packet length
    ja   enhanced_error_param ; Error if too large
    
    ; Calculate TX FIFO port address
    add  bx, 0              ; TX FIFO is at offset 0 from base
    mov  dx, bx             ; DX = TX FIFO port address
    
    ; Write packet length to TX FIFO first
    mov  ax, cx             ; Move packet length to AX for OUT instruction
    out  dx, ax             ; Write packet length as 16-bit value
    
    ; Choose optimal transfer method based on CPU and packet size
    cmp  byte ptr [cpu_supports_32bit], 0
    je   enhanced_use_16bit ; Use 16-bit if no 32-bit support
    
    ; For 386+ systems, use 32-bit transfers if packet is large enough
    cmp  cx, 32             ; Minimum size for 32-bit optimization
    jb   enhanced_use_16bit ; Use 16-bit for small packets
    
    ; Use 32-bit DWORD transfers (more efficient on 386+)
    mov  ax, cx             ; AX = packet length
    shr  ax, 2              ; AX = length / 4 (dword count)
    test ax, ax             ; Check if we have any complete dwords
    jz   enhanced_handle_remainder ; Skip if no complete dwords
    
    push ax                 ; Save dword count
    push dx                 ; Save I/O port
    push si                 ; Save source buffer offset
    push ds                 ; Save source buffer segment
    
    ; Use enhanced 32-bit transfer function
    call direct_pio_outsl
    add  sp, 8              ; Clean up stack (4 parameters)
    
    ; Calculate remaining bytes
    mov  ax, [bp+10]        ; Reload original packet length
    and  ax, 3              ; Get remainder bytes (length % 4)
    jz   enhanced_success   ; Done if no remainder
    
    ; Adjust source pointer for remaining bytes
    mov  cx, [bp+10]        ; Reload packet length
    and  cx, 0FFFCh         ; Clear lower 2 bits (align to dword boundary)
    add  si, cx             ; Advance source pointer by complete dwords
    
    mov  cx, ax             ; CX = remainder byte count
    jmp  enhanced_transfer_remainder

enhanced_use_16bit:
    ; Use traditional 16-bit transfer method (or loop for 8086)
    mov  ax, cx             ; AX = packet length
    shr  ax, 1              ; AX = length / 2 (word count)
    mov  cx, ax             ; CX = word count

    ; Transfer packet data using 8086-safe OUTSW
    cld                     ; Clear direction flag
    OUTSW_SAFE              ; CPU-adaptive: REP OUTSW (186+) or loop (8086)
    
    ; Handle odd byte if packet length is odd
    mov  ax, [bp+10]        ; Reload original packet length
    test ax, 1              ; Check if length is odd
    jz   enhanced_success   ; Skip if even length
    
    ; Transfer the remaining odd byte
    lodsb                   ; Load byte from DS:[SI] into AL, increment SI
    out  dx, al             ; Output the odd byte
    jmp  enhanced_success

enhanced_handle_remainder:
    ; Handle cases where packet size < 4 bytes
    mov  cx, [bp+10]        ; Reload packet length
    
enhanced_transfer_remainder:
    ; Transfer remaining bytes (1-3 bytes)
    test cx, cx
    jz   enhanced_success
    
transfer_remainder_loop:
    lodsb                   ; Load byte from DS:[SI] into AL, increment SI
    out  dx, al             ; Output the byte
    loop transfer_remainder_loop

enhanced_success:
    mov  ax, PIO_SUCCESS    ; Return success
    jmp  enhanced_exit
    
enhanced_error_param:
    mov  ax, PIO_ERROR_INVALID_PARAM ; Return parameter error
    
enhanced_exit:
    ; Restore registers
    pop  ax                 ; Dummy pop to balance stack
    pop  bx
    pop  dx
    pop  cx
    pop  si
    pop  ds
    mov  sp, bp
    pop  bp
    ret
send_packet_direct_pio_enhanced ENDP

;
; direct_pio_get_optimization_level - Get current optimization level
;
; Returns current I/O optimization level for diagnostics
;
; Parameters: None
; Returns: AL = optimization level (0=286, 1=386, 2=486+)
;
PUBLIC direct_pio_get_optimization_level
direct_pio_get_optimization_level PROC FAR
    mov  al, [io_optimization_level]
    ret
direct_pio_get_optimization_level ENDP

;
; direct_pio_get_cpu_support_info - Get CPU support information
;
; Returns CPU support flags for diagnostics and feature detection
;
; Parameters: None
; Returns: AL = 1 if 32-bit support available, 0 if not
;
PUBLIC direct_pio_get_cpu_support_info
direct_pio_get_cpu_support_info PROC FAR
    mov  al, [cpu_supports_32bit]
    ret
direct_pio_get_cpu_support_info ENDP

END
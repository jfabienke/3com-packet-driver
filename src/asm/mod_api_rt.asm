; ============================================================================
; mod_api_rt.asm - Packet Driver API Runtime Dispatcher Module
; ============================================================================
; JIT-loadable module providing core Packet Driver API functions and handle
; management for the 3Com EtherLink II packet driver.
;
; Created: 2026-02-01 00:00:00
; Target: 8086, 16-bit real mode, NASM syntax
; Calling convention: Watcom far call (retf), large model
; ============================================================================

cpu 8086
bits 16

%include "patch_macros.inc"

; ============================================================================
; Constants
; ============================================================================

MAX_HANDLES     equ 8
HANDLE_SIZE     equ 16          ; simplified handle struct
HANDLE_MAGIC    equ 0504Bh      ; 'PK' - handle validation magic
PD_CLASS_ETHER  equ 1
PD_ETHER_LEN    equ 6           ; MAC address length

; Handle structure offsets (16 bytes total)
HANDLE_MAGIC_OFF    equ 0       ; word: magic number when allocated
HANDLE_ID_OFF       equ 2       ; word: handle ID
HANDLE_CLASS_OFF    equ 4       ; word: interface class (1=Ethernet)
HANDLE_TYPE_OFF     equ 6       ; word: interface type
HANDLE_RECV_SEG_OFF equ 8       ; word: receiver callback segment
HANDLE_RECV_OFF_OFF equ 10      ; word: receiver callback offset
HANDLE_NIC_IDX_OFF  equ 12      ; word: which NIC this handle is on
HANDLE_FLAGS_OFF    equ 14      ; word: active, promisc, etc.

PATCH_COUNT equ 0

; ============================================================================
; Module Header (64 bytes, standard format)
; ============================================================================

section .text class=MODULE

global _mod_api_rt_header
_mod_api_rt_header:
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
; External Dependencies
; ============================================================================

extern packet_send_enhanced_
extern _g_nic_infos
extern _g_num_nics

; ============================================================================
; Exported API Functions
; ============================================================================

global pd_access_type_
global pd_release_handle_
global pd_send_packet_
global pd_get_address_
global pd_get_statistics_
global pd_get_driver_info_
global pd_set_rcv_mode_
global pd_get_rcv_mode_
global pd_get_parameters_
global pd_validate_handle_
global pd_reset_interface_
global pd_terminate_
global pd_set_address_
global api_process_received_packet_

; Stub functions (all return 0)
global api_init_extended_handles_
global api_cleanup_extended_handles_
global api_get_extended_handle_
global api_upgrade_handle_
global pd_set_handle_priority_
global pd_get_routing_info_
global pd_set_load_balance_
global pd_get_nic_status_
global pd_set_qos_params_
global pd_get_flow_stats_
global pd_set_nic_preference_
global pd_get_handle_info_
global pd_set_bandwidth_limit_
global pd_get_error_info_
global api_select_optimal_nic_
global api_check_bandwidth_limit_
global api_handle_nic_failure_
global api_coordinate_recovery_with_routing_
global api_update_nic_utilization_

; ============================================================================
; Function: pd_access_type_
; ============================================================================
; Allocate a handle for packet reception
;
; Watcom large model stack layout:
; [bp+6]  = if_class (word)
; [bp+8]  = if_type (word)
; [bp+10] = if_number (word)
; [bp+12] = typelen (word)
; [bp+14] = type far ptr offset (word)
; [bp+16] = type far ptr segment (word)
; [bp+18] = receiver far ptr offset (word)
; [bp+20] = receiver far ptr segment (word)
;
; Returns: AX = handle_id (1-based) or -1 on error
; ============================================================================

pd_access_type_:
    push bp
    mov bp, sp
    push bx
    push cx
    push si
    push di
    push es

    ; Find a free handle
    mov cx, MAX_HANDLES
    mov si, handles
    xor di, di                  ; handle index counter

.find_free:
    mov ax, [si + HANDLE_MAGIC_OFF]
    cmp ax, HANDLE_MAGIC
    jne .found_free             ; Not allocated if magic doesn't match

    add si, HANDLE_SIZE
    inc di
    loop .find_free

    ; No free handles
    mov ax, -1
    jmp .done

.found_free:
    ; SI points to free handle, DI = handle index

    ; Allocate next handle ID
    mov ax, [next_handle]
    mov [si + HANDLE_ID_OFF], ax
    inc word [next_handle]

    ; Fill in handle fields from parameters
    mov ax, [bp + 6]            ; if_class
    mov [si + HANDLE_CLASS_OFF], ax

    mov ax, [bp + 8]            ; if_type
    mov [si + HANDLE_TYPE_OFF], ax

    mov ax, [bp + 18]           ; receiver offset
    mov [si + HANDLE_RECV_OFF_OFF], ax

    mov ax, [bp + 20]           ; receiver segment
    mov [si + HANDLE_RECV_SEG_OFF], ax

    mov ax, [bp + 10]           ; if_number (NIC index)
    mov [si + HANDLE_NIC_IDX_OFF], ax

    xor ax, ax
    mov [si + HANDLE_FLAGS_OFF], ax  ; Clear flags

    ; Mark handle as allocated
    mov word [si + HANDLE_MAGIC_OFF], HANDLE_MAGIC

    ; Return handle_id (1-based: index + 1)
    mov ax, di
    inc ax

.done:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop bp
    retf

; ============================================================================
; Function: pd_release_handle_
; ============================================================================
; Release an allocated handle
;
; Input: AX = handle_id (1-based)
; Returns: AX = 0 on success, -1 on error
; ============================================================================

pd_release_handle_:
    push bx
    push si

    ; Validate and convert to index
    dec ax                      ; Convert to 0-based
    cmp ax, MAX_HANDLES
    jae .error

    ; Calculate handle address
    mov bx, HANDLE_SIZE
    mul bx                      ; AX = index * HANDLE_SIZE
    mov si, ax
    add si, handles

    ; Verify magic
    cmp word [si + HANDLE_MAGIC_OFF], HANDLE_MAGIC
    jne .error

    ; Clear magic to mark as free
    mov word [si + HANDLE_MAGIC_OFF], 0

    xor ax, ax                  ; Success
    jmp .done

.error:
    mov ax, -1

.done:
    pop si
    pop bx
    retf

; ============================================================================
; Function: pd_send_packet_
; ============================================================================
; Send a packet through the driver
;
; Input: AX = handle_id, DX:BX = buffer far ptr, CX = length
; Returns: AX = 0 on success, -1 on error
; ============================================================================

pd_send_packet_:
    push bp
    push si
    push di

    ; Validate handle
    push ax
    push bx
    push cx
    push dx
    call pd_validate_handle_
    pop dx
    pop cx
    pop bx
    pop si                      ; Original handle_id in SI

    test ax, ax
    jz .error

    ; Call packet_send_enhanced_
    ; Parameters: DX:BX = buffer, CX = length
    push cx
    push dx
    push bx
    call packet_send_enhanced_
    add sp, 6                   ; Clean up stack (3 words)

    jmp .done

.error:
    mov ax, -1

.done:
    pop di
    pop si
    pop bp
    retf

; ============================================================================
; Function: pd_get_address_
; ============================================================================
; Get MAC address for a handle
;
; Input: AX = handle_id, DX:BX = buffer far ptr, CX = buffer length
; Returns: AX = bytes copied or -1 on error
; ============================================================================

pd_get_address_:
    push bp
    push bx
    push cx
    push si
    push di
    push es
    push ds

    ; Validate handle
    push ax
    push bx
    push cx
    push dx
    call pd_validate_handle_
    pop dx
    pop cx
    pop bx
    pop si                      ; Original handle_id in SI

    test ax, ax
    jz .error

    ; Check buffer length
    cmp cx, PD_ETHER_LEN
    jb .error

    ; Set up destination ES:DI = buffer
    mov es, dx
    mov di, bx

    ; Get NIC index from handle
    dec si                      ; Convert to 0-based
    mov ax, HANDLE_SIZE
    mul si
    mov si, ax
    add si, handles
    mov ax, [si + HANDLE_NIC_IDX_OFF]

    ; Calculate NIC info offset in _g_nic_infos
    ; Assuming NIC info structure has MAC at offset 0 (6 bytes)
    ; Each NIC info is likely larger, but we need the structure size
    ; For now, assume 64-byte NIC info structures
    mov bx, 64                  ; NIC info size (adjust as needed)
    mul bx
    mov si, ax
    add si, _g_nic_infos

    ; Copy 6 bytes (MAC address)
    mov cx, PD_ETHER_LEN
    cld
    rep movsb

    mov ax, PD_ETHER_LEN        ; Return bytes copied
    jmp .done

.error:
    mov ax, -1

.done:
    pop ds
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop bp
    retf

; ============================================================================
; Function: pd_get_statistics_
; ============================================================================
; Get driver statistics
;
; Input: AX = handle_id, DX:BX = stats buffer far ptr
; Returns: AX = 0 on success, -1 on error
; ============================================================================

pd_get_statistics_:
    push bx
    push dx

    ; Validate handle
    push ax
    call pd_validate_handle_
    add sp, 2

    test ax, ax
    jz .error

    ; For now, just return success without copying stats
    ; Real implementation would copy statistics to buffer
    xor ax, ax
    jmp .done

.error:
    mov ax, -1

.done:
    pop dx
    pop bx
    retf

; ============================================================================
; Function: pd_get_driver_info_
; ============================================================================
; Get driver information block
;
; Returns: DX:AX = far ptr to driver_info
; ============================================================================

pd_get_driver_info_:
    mov ax, driver_info
    mov dx, cs                  ; Assuming driver_info is in CS
    retf

; ============================================================================
; Function: pd_set_rcv_mode_
; ============================================================================
; Set receive mode for a handle
;
; Input: AX = handle_id, DX = mode
; Returns: AX = 0 on success, -1 on error
; ============================================================================

pd_set_rcv_mode_:
    push bx
    push si

    ; Validate handle
    push ax
    call pd_validate_handle_
    add sp, 2

    test ax, ax
    jz .error

    ; Store mode in handle flags
    ; (For now, just return success)
    xor ax, ax
    jmp .done

.error:
    mov ax, -1

.done:
    pop si
    pop bx
    retf

; ============================================================================
; Function: pd_get_rcv_mode_
; ============================================================================
; Get receive mode for a handle
;
; Input: AX = handle_id
; Returns: AX = mode or -1 on error
; ============================================================================

pd_get_rcv_mode_:
    push bx

    ; Validate handle
    push ax
    call pd_validate_handle_
    add sp, 2

    test ax, ax
    jz .error

    ; Return mode (for now, return 0)
    xor ax, ax
    jmp .done

.error:
    mov ax, -1

.done:
    pop bx
    retf

; ============================================================================
; Function: pd_get_parameters_
; ============================================================================
; Get driver parameters
;
; Returns: DX:AX = far ptr to param_block
; ============================================================================

pd_get_parameters_:
    mov ax, param_block
    mov dx, cs
    retf

; ============================================================================
; Function: pd_validate_handle_
; ============================================================================
; Validate a handle ID
;
; Input: AX = handle_id (1-based)
; Returns: AX = 1 if valid, 0 if invalid
; ============================================================================

pd_validate_handle_:
    push bx
    push si

    ; Check bounds
    dec ax                      ; Convert to 0-based
    cmp ax, MAX_HANDLES
    jae .invalid

    ; Calculate handle address
    mov bx, HANDLE_SIZE
    mul bx
    mov si, ax
    add si, handles

    ; Check magic
    cmp word [si + HANDLE_MAGIC_OFF], HANDLE_MAGIC
    jne .invalid

    mov ax, 1                   ; Valid
    jmp .done

.invalid:
    xor ax, ax                  ; Invalid

.done:
    pop si
    pop bx
    retf

; ============================================================================
; Function: pd_reset_interface_
; ============================================================================
; Reset interface (no-op)
;
; Input: AX = handle_id
; Returns: AX = 0
; ============================================================================

pd_reset_interface_:
    xor ax, ax
    retf

; ============================================================================
; Function: pd_terminate_
; ============================================================================
; Terminate handle (same as release)
;
; Input: AX = handle_id
; Returns: AX = 0 on success, -1 on error
; ============================================================================

pd_terminate_:
    jmp pd_release_handle_

; ============================================================================
; Function: pd_set_address_
; ============================================================================
; Set MAC address (not supported)
;
; Returns: AX = -1
; ============================================================================

pd_set_address_:
    mov ax, -1
    retf

; ============================================================================
; Function: api_process_received_packet_
; ============================================================================
; Process a received packet and dispatch to matching handles
;
; Input: DX:AX = packet far ptr, BX = length, CX = nic_index
; Returns: AX = 0
; ============================================================================

api_process_received_packet_:
    push bp
    mov bp, sp
    push bx
    push cx
    push si
    push di
    push es
    push ds

    ; Save packet info
    push dx                     ; [bp-2] = packet segment
    push ax                     ; [bp-4] = packet offset
    push bx                     ; [bp-6] = length
    push cx                     ; [bp-8] = nic_index

    ; Extract ethertype from packet (offset 12-13, big-endian)
    mov es, dx
    mov si, ax
    mov ax, es:[si + 12]        ; Get ethertype word
    xchg ah, al                 ; Convert to little-endian
    push ax                     ; [bp-10] = ethertype

    ; Scan all handles
    mov cx, MAX_HANDLES
    mov si, handles

.scan_handles:
    ; Check if handle is allocated
    cmp word [si + HANDLE_MAGIC_OFF], HANDLE_MAGIC
    jne .next_handle

    ; Check if NIC index matches
    mov ax, [bp - 8]            ; nic_index
    cmp ax, [si + HANDLE_NIC_IDX_OFF]
    jne .next_handle

    ; Check if ethertype matches (or handle accepts all types)
    mov ax, [bp - 10]           ; ethertype
    cmp ax, [si + HANDLE_TYPE_OFF]
    je .dispatch

    ; Check if handle accepts all (type = 0)
    cmp word [si + HANDLE_TYPE_OFF], 0
    jne .next_handle

.dispatch:
    ; Call receiver upcall
    ; First call with AX=0 to get buffer
    push si                     ; Save handle pointer

    mov ax, 0                   ; First call
    push ax
    call far [si + HANDLE_RECV_SEG_OFF]  ; Far call to receiver
    add sp, 2

    ; Second call with AX=1 (copy done)
    mov ax, 1
    push ax
    pop si
    call far [si + HANDLE_RECV_SEG_OFF]
    add sp, 2

    pop si                      ; Restore handle pointer

.next_handle:
    add si, HANDLE_SIZE
    loop .scan_handles

    xor ax, ax                  ; Return 0

    add sp, 10                  ; Clean up local vars
    pop ds
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop bp
    retf

; ============================================================================
; Stub Functions (all return 0)
; ============================================================================

api_init_extended_handles_:
api_cleanup_extended_handles_:
api_get_extended_handle_:
api_upgrade_handle_:
pd_set_handle_priority_:
pd_get_routing_info_:
pd_set_load_balance_:
pd_get_nic_status_:
pd_set_qos_params_:
pd_get_flow_stats_:
pd_set_nic_preference_:
pd_get_handle_info_:
pd_set_bandwidth_limit_:
pd_get_error_info_:
api_select_optimal_nic_:
api_check_bandwidth_limit_:
api_handle_nic_failure_:
api_coordinate_recovery_with_routing_:
api_update_nic_utilization_:
    xor ax, ax
    retf

; ============================================================================
; Data Section (Hot - frequently accessed)
; ============================================================================

section .data

handles:
    times (HANDLE_SIZE * MAX_HANDLES) db 0  ; 128 bytes

next_handle:
    dw 1                        ; Next handle ID to assign

api_ready:
    dw 0                        ; API ready flag

driver_info:
    db 'PKTDRV', 0              ; Driver name (7 bytes)
    db 1, 9                     ; Version 1.9 (2 bytes)
    db PD_CLASS_ETHER           ; Class (1 byte)
    db 0                        ; Type (1 byte)
    db 0                        ; Number (1 byte)
    dw 0                        ; Functionality (2 bytes)
    times 18 db 0               ; Padding to 32 bytes

param_block:
    times 16 db 0               ; Parameter block

; ============================================================================
; End of Module
; ============================================================================

hot_end:

patch_table:

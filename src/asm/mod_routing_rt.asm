;==============================================================================
; mod_routing_rt.asm - Multi-NIC Routing and Bridge Learning Module
;==============================================================================
; Date: 2026-02-01 14:30:00
; CPU: 8086
; Format: NASM 16-bit, Watcom far call convention
;
; Provides JIT-patchable routing logic for multi-NIC packet forwarding,
; MAC address learning, and bridging decisions.
;==============================================================================

%include "patch_macros.inc"

;------------------------------------------------------------------------------
; Constants
;------------------------------------------------------------------------------
MAC_TABLE_SIZE  equ 64      ; max MAC table entries
MAC_ENTRY_SIZE  equ 8       ; 6 bytes MAC + 1 byte port + 1 byte age
MAX_NICS        equ 8
ROUTE_DROP      equ 0
ROUTE_FORWARD   equ 1
ROUTE_BROADCAST equ 2
ROUTE_LOCAL     equ 3

;------------------------------------------------------------------------------
; Module Header (64 bytes)
;------------------------------------------------------------------------------
PATCH_COUNT equ 0

section .text class=MODULE

global _mod_routing_rt_header
_mod_routing_rt_header:
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

;------------------------------------------------------------------------------
; External Symbols
;------------------------------------------------------------------------------
extern hardware_send_packet_
extern _g_num_nics

;------------------------------------------------------------------------------
; Data Section (Hot Section)
;------------------------------------------------------------------------------
section .data

mac_table:
    times (MAC_ENTRY_SIZE * MAC_TABLE_SIZE) db 0  ; 512 bytes MAC table

routing_enabled:
    dw 0                            ; routing enabled flag

route_stats:
    times 24 db 0                   ; 6 dwords: fwd/bcast/drop/local/learn/age

;------------------------------------------------------------------------------
; Code Section
;------------------------------------------------------------------------------
section .text

;==============================================================================
; routing_is_enabled_ - Check if routing is enabled
;==============================================================================
; Input:  None
; Output: AX = routing_enabled flag
; Uses:   AX
;==============================================================================
global routing_is_enabled_
routing_is_enabled_:
    mov     ax, [routing_enabled]
    retf

;==============================================================================
; bridge_lookup_mac_ - Search MAC table for given MAC address
;==============================================================================
; Input:  DX:AX = far pointer to MAC address (6 bytes)
; Output: AX = index (0-63) if found, -1 if not found
; Uses:   AX, BX, CX, DX, SI, DI, ES
;==============================================================================
global bridge_lookup_mac_
bridge_lookup_mac_:
    push    bp
    mov     bp, sp
    push    es
    push    si
    push    di

    ; Save far pointer to search MAC
    mov     si, ax                  ; SI = offset of search MAC
    push    dx                      ; Save segment on stack

    ; Setup for table scan
    xor     bx, bx                  ; BX = table index (0-63)

.scan_loop:
    cmp     bx, MAC_TABLE_SIZE
    jae     .not_found              ; Beyond table size

    ; Calculate entry offset: BX * 8
    mov     ax, bx
    mov     cl, 3
    shl     ax, cl                  ; AX = BX * 8
    mov     di, ax
    add     di, mac_table           ; DI = entry offset

    ; Check if entry is valid (age > 0)
    mov     al, [di + 7]            ; AL = age
    or      al, al
    jz      .next_entry             ; Skip if expired

    ; Compare 6 bytes MAC address
    push    ds
    pop     es                      ; ES = DS (table segment)
    pop     dx                      ; Restore search MAC segment
    push    dx                      ; Keep it on stack
    push    ds
    mov     ds, dx                  ; DS:SI = search MAC
    mov     cx, 6
    cld
    repe    cmpsb
    pop     ds                      ; Restore DS

    je      .found                  ; Match!

    ; Restore SI for next iteration
    pop     dx
    push    dx
    mov     si, [bp - 4]            ; Restore original SI from stack slot

.next_entry:
    inc     bx
    jmp     .scan_loop

.found:
    pop     dx                      ; Clean up stack
    mov     ax, bx                  ; Return index
    jmp     .exit

.not_found:
    pop     dx                      ; Clean up stack
    mov     ax, -1                  ; Return -1

.exit:
    pop     di
    pop     si
    pop     es
    pop     bp
    retf

;==============================================================================
; bridge_learn_mac_ - Learn/update MAC address in table
;==============================================================================
; Input:  DX:AX = far pointer to source MAC (6 bytes)
;         BX = NIC port index
; Output: AX = 0 on success
; Uses:   All registers
;==============================================================================
global bridge_learn_mac_
bridge_learn_mac_:
    push    bp
    mov     bp, sp
    push    es
    push    si
    push    di

    ; Save parameters
    push    ax                      ; [bp-2] = MAC offset
    push    dx                      ; [bp-4] = MAC segment
    push    bx                      ; [bp-6] = port index

    ; First, search if MAC already exists
    mov     ax, [bp - 2]
    mov     dx, [bp - 4]
    call    bridge_lookup_mac_

    cmp     ax, -1
    jne     .update_existing        ; Found, update it

    ; Not found, find empty or LRU slot
    xor     bx, bx                  ; BX = scan index
    mov     cx, -1                  ; CX = best slot (-1 = none)
    mov     dl, 255                 ; DL = lowest age seen

.find_slot:
    cmp     bx, MAC_TABLE_SIZE
    jae     .slot_found

    ; Calculate entry offset
    mov     ax, bx
    push    cx
    mov     cl, 3
    shl     ax, cl
    pop     cx
    mov     di, ax
    add     di, mac_table

    ; Check age
    mov     al, [di + 7]
    or      al, al
    jz      .empty_slot             ; Empty slot (age=0), use it!

    ; Check if this is the lowest age (LRU)
    cmp     al, dl
    jae     .next_slot
    mov     dl, al                  ; New lowest age
    mov     cx, bx                  ; Remember this slot

.next_slot:
    inc     bx
    jmp     .find_slot

.empty_slot:
    mov     cx, bx                  ; Use this empty slot

.slot_found:
    cmp     cx, -1
    je      .exit_fail              ; No slot found (shouldn't happen)
    mov     ax, cx                  ; AX = slot to use

.update_existing:
    ; AX = slot index, write MAC entry
    push    ax                      ; Save slot index

    ; Calculate entry offset
    push    cx
    mov     cl, 3
    shl     ax, cl
    pop     cx
    mov     di, ax
    add     di, mac_table           ; DI = entry offset

    ; Copy MAC address (6 bytes)
    mov     si, [bp - 2]            ; SI = MAC offset
    mov     ax, [bp - 4]
    mov     es, ax                  ; ES:SI = source MAC
    push    ds
    push    es
    pop     ds                      ; DS:SI = source MAC
    push    di
    push    ss
    pop     es
    pop     di
    push    cs
    pop     es
    push    ds
    pop     es
    ; Simplified: just copy using lodsb/stosb
    pop     ds
    push    ds
    mov     ax, [bp - 4]
    push    ax
    pop     es
    mov     si, [bp - 2]
    push    ds
    pop     es
    push    ss
    pop     es
    push    cs
    pop     es
    ; Direct copy approach
    mov     ax, ds
    push    ax
    mov     ax, [bp - 4]
    mov     ds, ax
    mov     si, [bp - 2]
    mov     ax, ss
    mov     es, ax
    push    cs
    pop     es
    mov     cx, 6
    cld
    rep     movsb
    pop     ax
    mov     ds, ax

    ; Write port and age
    mov     ax, [bp - 6]            ; AL = port index
    mov     [di], al                ; [entry + 6] = port
    mov     byte [di + 1], 255      ; [entry + 7] = age = 255

    pop     ax                      ; Clean up slot index from stack
    xor     ax, ax                  ; Return 0 (success)
    jmp     .exit

.exit_fail:
    mov     ax, -1

.exit:
    pop     bx
    pop     dx
    pop     ax
    pop     di
    pop     si
    pop     es
    pop     bp
    retf

;==============================================================================
; routing_decide_ - Determine routing action for packet
;==============================================================================
; Input:  DX:AX = far pointer to destination MAC (6 bytes)
;         BX = source NIC index
; Output: AX = routing decision (ROUTE_*)
;         DX = destination port (if ROUTE_FORWARD)
; Uses:   AX, BX, CX, DX, SI, DI, ES
;==============================================================================
global routing_decide_
routing_decide_:
    push    bp
    mov     bp, sp
    push    es
    push    si
    push    di

    ; Check if routing is enabled
    cmp     word [routing_enabled], 0
    jne     .routing_on
    mov     ax, ROUTE_LOCAL
    jmp     .exit

.routing_on:
    ; Save parameters
    push    ax                      ; [bp-2] = MAC offset
    push    dx                      ; [bp-4] = MAC segment
    push    bx                      ; [bp-6] = source NIC

    ; Check for broadcast MAC (FF:FF:FF:FF:FF:FF)
    mov     es, dx
    mov     si, ax
    mov     cx, 6
    cld
.check_broadcast:
    mov     al, [es:si]
    cmp     al, 0xFF
    jne     .not_broadcast
    inc     si
    loop    .check_broadcast

    ; Is broadcast
    mov     ax, ROUTE_BROADCAST
    jmp     .cleanup_exit

.not_broadcast:
    ; Lookup destination MAC in bridge table
    mov     ax, [bp - 2]
    mov     dx, [bp - 4]
    call    bridge_lookup_mac_

    cmp     ax, -1
    je      .mac_not_found          ; Not in table, broadcast

    ; Found in table, check port
    ; Calculate entry offset to get port
    push    cx
    mov     cl, 3
    shl     ax, cl
    pop     cx
    mov     di, ax
    add     di, mac_table
    mov     al, [di + 6]            ; AL = port from table
    xor     ah, ah
    mov     dx, ax                  ; DX = destination port

    ; Compare with source NIC
    cmp     ax, [bp - 6]
    je      .same_port              ; Same port, drop

    ; Forward to different port
    mov     ax, ROUTE_FORWARD
    ; DX already has port
    jmp     .cleanup_exit

.same_port:
    mov     ax, ROUTE_DROP
    jmp     .cleanup_exit

.mac_not_found:
    mov     ax, ROUTE_BROADCAST

.cleanup_exit:
    pop     bx
    pop     dx
    add     sp, 2                   ; Clean MAC offset from stack

.exit:
    pop     di
    pop     si
    pop     es
    pop     bp
    retf

;==============================================================================
; route_packet_ - Route a packet based on routing decision
;==============================================================================
; Input:  DX:AX = far pointer to packet
;         BX = packet length
;         CX = source NIC index
; Output: AX = result
;         0 = deliver locally
;         -1 = drop
;         -2 = broadcast
;         >= 0 = forward to port (port index in AX)
; Uses:   All registers
;==============================================================================
global route_packet_
route_packet_:
    push    bp
    mov     bp, sp
    push    si
    push    di

    ; Save parameters
    push    ax                      ; [bp-2] = packet offset
    push    dx                      ; [bp-4] = packet segment
    push    bx                      ; [bp-6] = length
    push    cx                      ; [bp-8] = source NIC

    ; Call routing_decide_ with dest MAC (first 6 bytes of packet)
    mov     ax, [bp - 2]
    mov     dx, [bp - 4]
    mov     bx, [bp - 8]            ; source NIC
    call    routing_decide_

    ; AX = routing decision, DX = dest port (if ROUTE_FORWARD)
    cmp     ax, ROUTE_LOCAL
    je      .local
    cmp     ax, ROUTE_DROP
    je      .drop
    cmp     ax, ROUTE_FORWARD
    je      .forward
    cmp     ax, ROUTE_BROADCAST
    je      .broadcast

    ; Unknown, treat as local
.local:
    xor     ax, ax
    jmp     .exit

.drop:
    mov     ax, -1
    jmp     .exit

.forward:
    ; DX already has port index
    mov     ax, dx
    jmp     .exit

.broadcast:
    mov     ax, -2

.exit:
    pop     cx
    pop     bx
    pop     dx
    add     sp, 2
    pop     di
    pop     si
    pop     bp
    retf

;==============================================================================
; forward_packet_ - Forward packet to specific NIC
;==============================================================================
; Input:  DX:AX = far pointer to packet
;         BX = packet length
;         CX = destination NIC index
; Output: AX = result from hardware_send_packet_
; Uses:   All registers
;==============================================================================
global forward_packet_
forward_packet_:
    push    bp
    mov     bp, sp

    ; Call hardware_send_packet_(packet, length, nic_index)
    ; Watcom far call: push params right to left
    push    cx                      ; NIC index
    push    bx                      ; length
    push    dx                      ; packet segment
    push    ax                      ; packet offset
    call    far hardware_send_packet_
    add     sp, 8                   ; Clean up stack

    pop     bp
    retf

;==============================================================================
; broadcast_packet_ - Broadcast packet to all NICs except source
;==============================================================================
; Input:  DX:AX = far pointer to packet
;         BX = packet length
;         CX = source NIC index
; Output: AX = 0
; Uses:   All registers
;==============================================================================
global broadcast_packet_
broadcast_packet_:
    push    bp
    mov     bp, sp
    push    si

    ; Save parameters
    push    ax                      ; [bp-2] = packet offset
    push    dx                      ; [bp-4] = packet segment
    push    bx                      ; [bp-6] = length
    push    cx                      ; [bp-8] = source NIC

    xor     si, si                  ; SI = loop index (0 to g_num_nics-1)

.loop:
    mov     ax, [_g_num_nics]
    cmp     si, ax
    jae     .done                   ; Loop done

    ; Skip if this is source NIC
    cmp     si, [bp - 8]
    je      .next

    ; Send to this NIC
    push    si                      ; Save loop counter
    push    si                      ; NIC index param
    push    word [bp - 6]           ; length param
    push    word [bp - 4]           ; packet segment param
    push    word [bp - 2]           ; packet offset param
    call    far hardware_send_packet_
    add     sp, 8                   ; Clean up params
    pop     si                      ; Restore loop counter

.next:
    inc     si
    jmp     .loop

.done:
    xor     ax, ax                  ; Return 0

    pop     cx
    pop     bx
    pop     dx
    pop     ax
    pop     si
    pop     bp
    retf

;------------------------------------------------------------------------------
; Module End Marker
;------------------------------------------------------------------------------
hot_end:

patch_table:

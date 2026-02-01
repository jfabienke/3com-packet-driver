;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file pcipwr.asm
;; @brief PCI Power Management implementation for DOS packet driver
;;
;; Handles PCI power states, capability list walking, and device bring-up
;; from D3hot state. Critical for warm reboot scenarios and proper device
;; initialization.
;;
;; Calling convention: Watcom C 16-bit cdecl
;; - Arguments pushed right-to-left
;; - Caller cleans stack
;; - 16-bit return in AX
;; - Preserve BP, BX, SI, DI
;;
;; Converted from pcipwr.c
;;
;; 3Com Packet Driver - PCI Power Management Assembly Module
;; Created: 2026-02-01 00:00:00
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16
cpu 386

%include "csym.inc"

segment _TEXT class=CODE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Constants
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PCI_CAP_ID_PM           equ 0x01
PCI_PM_CAP              equ 0x02
PCI_PM_CTRL             equ 0x04
PCI_PM_CTRL_STATE_MASK  equ 0x0003
PCI_PM_CTRL_STATE_D0    equ 0x0000
PCI_PM_CTRL_STATE_D3HOT equ 0x0003
PCI_PM_CTRL_PME_ENABLE  equ 0x0100
PCI_PM_CTRL_PME_STATUS  equ 0x8000
PCI_PM_CAP_D1           equ 0x0200
PCI_PM_CAP_D2           equ 0x0400
PCI_STATUS              equ 0x06
PCI_STATUS_CAP_LIST     equ 0x0010
PCI_CAPABILITY_LIST     equ 0x34

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; External C functions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

extern pci_read_config_byte_
extern pci_read_config_word_
extern pci_write_config_word_
extern delay_ms_
extern pci_device_setup_

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Exported functions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

global pci_find_capability_
global pci_get_power_state_
global pci_set_power_state_
global pci_clear_pme_status_
global pci_power_on_device_
global pci_power_state_supported_

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; uint8_t pci_find_capability(bus, device, function, cap_id)
;;
;; Walk PCI capability linked list looking for specific capability ID.
;;
;; @param bus       [bp+4]  Bus number
;; @param device    [bp+6]  Device number
;; @param function  [bp+8]  Function number
;; @param cap_id    [bp+10] Capability ID to find
;; @return AL = config space offset of capability, or 0 if not found
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
pci_find_capability_:
    push    bp
    mov     bp, sp
    push    bx
    push    si
    push    di

    ;; Read PCI_STATUS to check for capability list support
    push    word PCI_STATUS         ; offset
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_read_config_word_
    add     sp, 8

    ;; Check CAP_LIST bit in status register
    test    ax, PCI_STATUS_CAP_LIST
    jz      .not_found

    ;; Read PCI_CAPABILITY_LIST pointer
    push    word PCI_CAPABILITY_LIST ; offset
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_read_config_byte_
    add     sp, 8

    and     al, 0xFC                ; Mask off reserved bits
    mov     bl, al                  ; BL = current position in cap list
    xor     si, si                  ; SI = iteration counter

.walk_loop:
    test    bl, bl                  ; pos == 0?
    jz      .not_found
    cmp     si, 48                  ; Max 48 iterations
    jge     .not_found

    ;; Read capability ID at pos + 0
    xor     ah, ah
    mov     al, bl
    push    ax                      ; offset = pos
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_read_config_byte_
    add     sp, 8

    ;; Check for invalid ID
    cmp     al, 0xFF
    je      .not_found

    ;; Check if this is the capability we want
    cmp     al, [bp+10]
    je      .found

    ;; Read next capability pointer at pos + 1
    xor     ah, ah
    mov     al, bl
    inc     al                      ; pos + 1
    push    ax                      ; offset = pos + 1
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_read_config_byte_
    add     sp, 8

    and     al, 0xFC                ; Mask off reserved bits
    mov     bl, al                  ; BL = next position
    inc     si                      ; iterations++
    jmp     .walk_loop

.found:
    mov     al, bl                  ; Return the offset where we found it
    xor     ah, ah
    jmp     .done

.not_found:
    xor     ax, ax                  ; Return 0

.done:
    pop     di
    pop     si
    pop     bx
    pop     bp
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; int pci_get_power_state(bus, device, function)
;;
;; Get current PCI power state of device.
;;
;; @param bus       [bp+4]  Bus number
;; @param device    [bp+6]  Device number
;; @param function  [bp+8]  Function number
;; @return AX = power state (0-3), or 0xFFFF (-1) on error
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
pci_get_power_state_:
    push    bp
    mov     bp, sp
    push    bx

    ;; Find PM capability
    push    word PCI_CAP_ID_PM      ; cap_id
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_find_capability_
    add     sp, 8

    test    al, al
    jz      .no_pm

    ;; AL = pm_offset, read PMCSR at pm_offset + PCI_PM_CTRL
    xor     ah, ah
    add     al, PCI_PM_CTRL
    mov     bx, ax                  ; BX = pm_offset + PCI_PM_CTRL

    push    bx                      ; offset
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_read_config_word_
    add     sp, 8

    and     ax, PCI_PM_CTRL_STATE_MASK  ; Mask to power state bits
    jmp     .gps_done

.no_pm:
    mov     ax, 0xFFFF              ; Return -1

.gps_done:
    pop     bx
    pop     bp
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; bool pci_set_power_state(bus, device, function, state)
;;
;; Set PCI device to specified power state with appropriate delays.
;;
;; @param bus       [bp+4]  Bus number
;; @param device    [bp+6]  Device number
;; @param function  [bp+8]  Function number
;; @param state     [bp+10] Target power state (0-3)
;; @return AX = 1 on success, 0 on failure
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
pci_set_power_state_:
    push    bp
    mov     bp, sp
    sub     sp, 6                   ; Local variables:
                                    ;   [bp-2] = pm_offset
                                    ;   [bp-4] = current_state (before transition)
                                    ;   [bp-6] = retry counter
    push    bx
    push    si
    push    di

    ;; Validate state <= 3
    mov     ax, [bp+10]
    cmp     ax, 3
    ja      .sps_fail

    ;; Find PM capability
    push    word PCI_CAP_ID_PM      ; cap_id
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_find_capability_
    add     sp, 8

    test    al, al
    jnz     .sps_have_pm

    ;; No PM capability - assume always D0, return success
    mov     ax, 1
    jmp     .sps_done

.sps_have_pm:
    xor     ah, ah
    mov     [bp-2], ax              ; Save pm_offset

    ;; Get current power state
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_get_power_state_
    add     sp, 6

    mov     [bp-4], ax              ; Save current_state

    ;; If already in target state, return success
    cmp     ax, [bp+10]
    jne     .sps_transition
    mov     ax, 1
    jmp     .sps_done

.sps_transition:
    ;; Read current PMCSR
    mov     ax, [bp-2]
    add     ax, PCI_PM_CTRL
    push    ax                      ; offset = pm_offset + PCI_PM_CTRL
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_read_config_word_
    add     sp, 8

    ;; Clear low 2 bits, OR in new state
    and     ax, ~PCI_PM_CTRL_STATE_MASK
    or      ax, [bp+10]
    mov     bx, ax                  ; BX = new PMCSR value

    ;; Write new PMCSR
    push    bx                      ; value
    mov     ax, [bp-2]
    add     ax, PCI_PM_CTRL
    push    ax                      ; offset
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_write_config_word_
    add     sp, 10

    test    ax, ax
    jz      .sps_fail               ; Write failed

    ;; Delay for state transition
    mov     ax, [bp+10]
    cmp     ax, PCI_PM_CTRL_STATE_D0
    jne     .sps_delay_1ms

    ;; Going to D0 - check where we came from
    cmp     word [bp-4], PCI_PM_CTRL_STATE_D3HOT
    jne     .sps_delay_1ms

    ;; D3hot -> D0: 10ms delay
    push    word 10
    call    delay_ms_
    add     sp, 2
    jmp     .sps_verify

.sps_delay_1ms:
    push    word 1
    call    delay_ms_
    add     sp, 2

.sps_verify:
    ;; Verify state change with retry loop (10 iterations)
    mov     word [bp-6], 0          ; retry = 0

.sps_retry_loop:
    cmp     word [bp-6], 10
    jge     .sps_fail

    ;; Get current power state
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_get_power_state_
    add     sp, 6

    cmp     ax, [bp+10]
    je      .sps_ok

    ;; Delay 1ms between retries
    push    word 1
    call    delay_ms_
    add     sp, 2

    inc     word [bp-6]
    jmp     .sps_retry_loop

.sps_ok:
    mov     ax, 1
    jmp     .sps_done

.sps_fail:
    xor     ax, ax

.sps_done:
    pop     di
    pop     si
    pop     bx
    add     sp, 6                   ; Free locals
    pop     bp
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; bool pci_clear_pme_status(bus, device, function)
;;
;; Clear PME status bit (write-1-to-clear) if set.
;;
;; @param bus       [bp+4]  Bus number
;; @param device    [bp+6]  Device number
;; @param function  [bp+8]  Function number
;; @return AX = 1 on success, 0 on failure
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
pci_clear_pme_status_:
    push    bp
    mov     bp, sp
    push    bx
    push    si

    ;; Find PM capability
    push    word PCI_CAP_ID_PM      ; cap_id
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_find_capability_
    add     sp, 8

    test    al, al
    jz      .cps_ok                 ; No PM = no PME to clear, return success

    xor     ah, ah
    mov     si, ax                  ; SI = pm_offset

    ;; Read PMCSR at pm_offset + PCI_PM_CTRL
    mov     ax, si
    add     ax, PCI_PM_CTRL
    push    ax                      ; offset
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_read_config_word_
    add     sp, 8

    ;; Check if PME_STATUS bit is set
    test    ax, PCI_PM_CTRL_PME_STATUS
    jz      .cps_ok                 ; Not set, nothing to do

    ;; OR in PME_STATUS to clear it (write-1-to-clear)
    or      ax, PCI_PM_CTRL_PME_STATUS
    mov     bx, ax

    ;; Write back
    push    bx                      ; value
    mov     ax, si
    add     ax, PCI_PM_CTRL
    push    ax                      ; offset
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_write_config_word_
    add     sp, 10

    test    ax, ax
    jz      .cps_fail

.cps_ok:
    mov     ax, 1
    jmp     .cps_done

.cps_fail:
    xor     ax, ax

.cps_done:
    pop     si
    pop     bx
    pop     bp
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; bool pci_power_on_device(bus, device, function)
;;
;; Complete power-on sequence: D0 transition, PME clear, device setup.
;;
;; @param bus       [bp+4]  Bus number
;; @param device    [bp+6]  Device number
;; @param function  [bp+8]  Function number
;; @return AX = 1 on success, 0 on failure
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
pci_power_on_device_:
    push    bp
    mov     bp, sp
    sub     sp, 2                   ; [bp-2] = pm_offset
    push    bx
    push    si
    push    di

    ;; Find PM capability
    push    word PCI_CAP_ID_PM      ; cap_id
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_find_capability_
    add     sp, 8

    test    al, al
    jz      .pod_no_pm              ; No PM capability, skip PM handling

    xor     ah, ah
    mov     [bp-2], ax              ; Save pm_offset

    ;; Get current power state
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_get_power_state_
    add     sp, 6

    ;; If current_state > 0, need to transition to D0
    cmp     ax, 0
    jle     .pod_clear_pme

    ;; Set power state to D0
    push    word PCI_PM_CTRL_STATE_D0  ; state = 0
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_set_power_state_
    add     sp, 8

    test    ax, ax
    jz      .pod_fail               ; Failed to set D0

.pod_clear_pme:
    ;; Clear PME status
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_clear_pme_status_
    add     sp, 6
    ;; Ignore return value (non-fatal)

    ;; Disable PME enable bit
    mov     ax, [bp-2]
    add     ax, PCI_PM_CTRL
    push    ax                      ; offset
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_read_config_word_
    add     sp, 8

    and     ax, ~PCI_PM_CTRL_PME_ENABLE  ; Clear PME_ENABLE bit
    mov     bx, ax

    push    bx                      ; value
    mov     ax, [bp-2]
    add     ax, PCI_PM_CTRL
    push    ax                      ; offset
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_write_config_word_
    add     sp, 10

.pod_no_pm:
    ;; Perform standard PCI device setup
    ;; pci_device_setup(bus, dev, func, enable_io=1, enable_mem=1, enable_master=1)
    push    word 1                  ; enable_master
    push    word 1                  ; enable_mem
    push    word 1                  ; enable_io
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_device_setup_
    add     sp, 12

    test    ax, ax
    jz      .pod_fail               ; pci_device_setup failed

    ;; Additional stabilization delay
    push    word 10
    call    delay_ms_
    add     sp, 2

    mov     ax, 1
    jmp     .pod_done

.pod_fail:
    xor     ax, ax

.pod_done:
    pop     di
    pop     si
    pop     bx
    add     sp, 2                   ; Free locals
    pop     bp
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; bool pci_power_state_supported(bus, device, function, state)
;;
;; Check if device supports a specific power state.
;;
;; @param bus       [bp+4]  Bus number
;; @param device    [bp+6]  Device number
;; @param function  [bp+8]  Function number
;; @param state     [bp+10] Power state to check (0-3)
;; @return AX = 1 if supported, 0 if not
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
pci_power_state_supported_:
    push    bp
    mov     bp, sp
    push    bx

    ;; D0 is always supported
    cmp     word [bp+10], 0
    je      .pss_yes

    ;; State > 3 is never supported
    cmp     word [bp+10], 3
    ja      .pss_no

    ;; Find PM capability
    push    word PCI_CAP_ID_PM      ; cap_id
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_find_capability_
    add     sp, 8

    test    al, al
    jz      .pss_no                 ; No PM capability

    ;; Read PM_CAP register at pm_offset + PCI_PM_CAP
    xor     ah, ah
    add     al, PCI_PM_CAP
    push    ax                      ; offset
    push    word [bp+8]             ; function
    push    word [bp+6]             ; device
    push    word [bp+4]             ; bus
    call    pci_read_config_word_
    add     sp, 8

    mov     bx, ax                  ; BX = PM capabilities word

    ;; Check based on requested state
    cmp     word [bp+10], 1
    je      .pss_check_d1
    cmp     word [bp+10], 2
    je      .pss_check_d2

    ;; State 3 (D3hot) is always supported if PM exists
    jmp     .pss_yes

.pss_check_d1:
    test    bx, PCI_PM_CAP_D1
    jnz     .pss_yes
    jmp     .pss_no

.pss_check_d2:
    test    bx, PCI_PM_CAP_D2
    jnz     .pss_yes
    ;; fall through to .pss_no

.pss_no:
    xor     ax, ax
    jmp     .pss_done

.pss_yes:
    mov     ax, 1

.pss_done:
    pop     bx
    pop     bp
    ret

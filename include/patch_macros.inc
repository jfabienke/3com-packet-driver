;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file patch_macros.inc
;; @brief Macros for creating safe SMC patch points (GPT-5 validated)
;;
;; Provides standardized macros for creating 5-byte NOP sleds that will be
;; patched with CPU-specific code during initialization.
;;
;; GPT-5 Requirements:
;; - Patch points must be safe to execute before patching (NOP sled)
;; - Exactly 5 bytes for atomic patching
;; - No memory corruption if executed unpached
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Standard 5-byte NOP sled for patch points
;;
;; Creates a safe patch point that does nothing if executed before patching.
;; Will be replaced with CPU-specific code during initialization.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro PATCH_POINT 1
%1:
    nop                         ; 0x90 - 1 byte
    nop                         ; 0x90 - 1 byte
    nop                         ; 0x90 - 1 byte
    nop                         ; 0x90 - 1 byte
    nop                         ; 0x90 - 1 byte
    ; Total: 5 bytes safe NOP sled
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch point with fallback call
;;
;; Creates a patch point that calls a fallback routine if not patched.
;; The CALL will be replaced with optimized inline code.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro PATCH_POINT_CALL 2
%1:
    call    %2                  ; 3 bytes: E8 xx xx (near call)
    nop                         ; 1 byte padding
    nop                         ; 1 byte padding
    ; Total: 5 bytes
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch point for I/O operations
;;
;; Default to safe single-byte I/O that will be optimized.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro PATCH_POINT_IO 2
%1:
    %2                          ; 1-2 bytes (IN/OUT instruction)
    nop                         ; Padding to 5 bytes
    nop
    nop
    nop
    ; Adjust NOPs based on default_op size
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Create patch table entry
;;
;; Generates a patch table entry for the linker.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro PATCH_TABLE_ENTRY 2
    dw      %1                  ; Offset to patch point
    db      %2                  ; Type (COPY/IO/ISR/etc)
    db      5                   ; Always 5 bytes
%endmacro

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch type constants
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
PATCH_TYPE_COPY         equ 1
PATCH_TYPE_IO           equ 2
PATCH_TYPE_CHECKSUM     equ 3
PATCH_TYPE_ISR          equ 4
PATCH_TYPE_BRANCH       equ 5
PATCH_TYPE_IMM16        equ 6       ; 16-bit immediate (IRQ, DMA ch, etc.)
PATCH_TYPE_IMM8         equ 7       ; 8-bit immediate
PATCH_TYPE_RELOC_NEAR   equ 8       ; Near CALL/JMP relocation (inter-module)
PATCH_TYPE_ENDIAN       equ 0Ah
PATCH_TYPE_NOP          equ 0FFh

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module capability flags (match MOD_CAP_* in modhdr.h)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
MOD_CAP_ISA_DMA         equ 0001h
MOD_CAP_BUSMASTER_DMA   equ 0002h
MOD_CAP_WBINVD          equ 0004h
MOD_CAP_CLFLUSH         equ 0008h
MOD_CAP_PCI_BUS         equ 0010h
MOD_CAP_BOUNCE_BUF      equ 0020h
MOD_CAP_VDS             equ 0040h
MOD_CAP_XMS             equ 0080h
MOD_CAP_DESC_RING       equ 0100h
MOD_CAP_SNOOP           equ 0200h

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; NIC type identifiers (match MOD_NIC_* in modhdr.h)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
MOD_NIC_ANY             equ 00h
MOD_NIC_3C509B          equ 01h
MOD_NIC_3C515           equ 02h
MOD_NIC_VORTEX          equ 03h
MOD_NIC_BOOMERANG       equ 04h
MOD_NIC_CYCLONE         equ 05h
MOD_NIC_TORNADO         equ 06h

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module header macro for NASM (64-byte standardized header)
;;
;; Usage: MODULE_HEADER name, cpu_req, nic_type, cap_flags
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%macro MODULE_HEADER 4
global _mod_%1_header
_mod_%1_header:
    db 'PKTDRV', 0             ; 7 bytes: signature
    db 1                        ; 1 byte: version_major
    db 0                        ; 1 byte: version_minor
    dw %1_hot_start             ; 2 bytes: hot section start offset
    dw %1_hot_end               ; 2 bytes: hot section end offset
    dw 0                        ; 2 bytes: cold_start (unused for modules)
    dw 0                        ; 2 bytes: cold_end (unused for modules)
    dw %1_patch_table           ; 2 bytes: patch table offset
    dw %1_PATCH_COUNT           ; 2 bytes: patch count
    dw (%1_hot_end - _mod_%1_header) ; 2 bytes: module_size
    dw (%1_hot_end - %1_hot_start)  ; 2 bytes: required_memory
    db %2                       ; 1 byte: cpu_requirements
    db %3                       ; 1 byte: nic_type
    dw %4                       ; 2 bytes: cap_flags
    times (64 - ($ - _mod_%1_header)) db 0  ; pad to 64 bytes
%endmacro

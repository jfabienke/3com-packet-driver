; data1.asm - Data Section 1 for 3c5x9pd.asm
;
; This file contains initialization data, lookup tables, and buffers for the 3Com
; 3C509 packet driver. Offsets are relative to the start of the .COM file (0x100).

; --- Constants ---
INT8_VECTOR_OFFSET      equ 0x20    ; Offset in IVT for INT 8 (timer)
INT9_VECTOR_OFFSET      equ 0x24    ; Offset in IVT for INT 9 (keyboard, if used)

; Driver Data Offsets (relative to CS:0x100)
DRIVER_NAME_OFFSET      equ 0x0102  ; Offset of "EtherLink 10 ISA" string
TEST_PATTERN_OFFSET     equ 0x0176  ; Offset of "TST" test buffer
JUMP_TABLE_PKT_OFFSET   equ 0x04ee  ; Offset of PKTDRVR jump table

; Packet Driver Function Codes (used in interrupt_handler)
PKTDRV_DRIVER_INFO      equ 0x01    ; Function 1: Get driver info
PKTDRV_SEND_PKT         equ 0x04    ; Function 4: Send packet

; --- Driver Header and Identification ---
; 0x0100: Initial data and driver name string
driver_header:
    db 0x00, 0x00                  ; Padding or version (unused?)
driver_name:
    db "EtherLink 10 ISA", 0       ; 18-byte string, null-terminated
                                   ; Referenced in sub_1d0c (mov dx, DRIVER_NAME_OFFSET)

; 0x0113: Configuration or signature bytes
config_signature:
    db 0x01, 0x0a, 0x0e, 0x06     ; Possible config: slot, IRQ, or EEPROM flags
    db 0xea, 0x05, 0x30, 0x00     ; 0x30 = default I/O base (300h)?
    db 0x01, 0x00, 0x01, 0x00     ; Additional flags or padding
    db 0x00, 0x00, 0x00, 0x00     ; Zero padding

; --- Uninitialized Data / BSS-like Section ---
; 0x0123: Zero-filled buffer (possibly reserved or BSS)
zero_buffer_1:
    times 72 db 0                  ; 72 bytes

; 0x016b: Small config block
small_config:
    db 0x00, 0x01, 0x0b           ; Flags or IRQ (0x0b = IRQ 3)?
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                                   ; 11 bytes, might be interrupt or slot config

; --- Test Pattern Buffer ---
; 0x0176: Repeating "TST" pattern (15 x 42 bytes + 20 bytes)
test_pattern:
    times 15 db "TSTSTSTSTSTSTSTSTSTSTSTSTSTSTSTSTSTSTSTS"  ; 15 * 42 = 630 bytes
    db "TSTSTSTSTSTSTSTS"          ; 20 bytes (corrected from 18, "TST" x 6 + "TS")
                                   ; Total: 650 bytes

; 0x0400: Post-test padding
post_test_padding:
    times 5 db 0                   ; 5 bytes of zeros

; --- Configuration or Lookup Table ---
; 0x0405: Possible I/O or IRQ configuration
config_table_1:
    db 0x00, 0x80, 0x01, 0x80, 0x05, 0x80, 0x00, 0x00
    db 0x03, 0x80, 0x08, 0x80, 0x03, 0x10, 0x01, 0xff
    db 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00
                                   ; 24 bytes, might be I/O base or IRQ mappings

; --- Zero-Filled Buffer ---
; 0x041d: Reserved space or uninitialized data
zero_buffer_2:
    times 40 db 0                  ; 40 bytes

; --- Miscellaneous Configuration or Pointers ---
; 0x0445: Mixed data, possibly pointers or buffer sizes
misc_config:
    db 0x00, 0x00, 0x00, 0xfe, 0x78, 0x50, 0x90, 0x00
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0xad, 0x02, 0x00
    db 0x00, 0xad, 0x02, 0x00, 0x00, 0x88, 0x13, 0x4c
    db 0x1d, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                                   ; 64 bytes

; --- Additional Configuration Table ---
; 0x0485: Partial configuration data
config_table_2:
    db 0xc1, 0x00, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00
    db 0x20, 0x00, 0x01, 0x00, 0x20, 0x00, 0x00, 0x04
    db 0x00, 0x00, 0x00           ; 19 bytes

; --- Lookup Table (Timing or Thresholds) ---
; 0x0498: Possible timing or IRQ-related values
lookup_table:
    db 0x40, 0x5b, 0x6f, 0x80, 0x8f, 0x9d, 0xa9, 0xb5
    db 0xc0, 0xca, 0xd4, 0xde, 0xe7, 0xef, 0xf8, 0x00
                                   ; 16 bytes, increasing values

; --- Zero-Filled Buffer with Small Data ---
; 0x04a8: Mostly zeros with embedded config data
zero_buffer_3:
    times 128 db 0                 ; 128 bytes of zeros
small_config_2:
    db 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                                   ; 32 bytes (8 + 8 + 8 + 8), total 160 bytes

; --- Small Configuration Block ---
; 0x0528: Buffer sizes or offsets
small_config_3:
    db 0x14, 0x00, 0xdb, 0x03, 0x00, 0x00, 0x50, 0x00
    db 0xef, 0x03, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00
    db 0x00, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                                   ; 24 bytes

; --- Zero-Filled Buffer ---
; 0x0540: Reserved space
zero_buffer_4:
    times 32 db 0                  ; 32 bytes

; --- Packet Driver Jump Table ---
; 0x0560: Jump table for PKTDRVR functions, referenced in interrupt_handler
; Offsets are absolute, adjusted by adding 0x5B0 (start of code.asm)
jump_table_pkt:
    dw 0x0988 + 0x5b0   ; handler_1:  0x0f38 (PKTDRV_DRIVER_INFO)
    dw 0x05b0 + 0x5b0   ; handler_2:  0x0b60 (PKTDRV_SEND_PKT)
    dw 0x05fd + 0x5b0   ; handler_3:  0x0bad
    dw 0x06c3 + 0x5b0   ; handler_4:  0x0c73
    dw 0x0ccc + 0x5b0   ; handler_5:  0x127c
    dw 0x06ed + 0x5b0   ; handler_6:  0x0c9d
    dw 0x076c + 0x5b0   ; handler_7:  0x0d1c
    dw 0x079a + 0x5b0   ; handler_8:  0x0d4a
    dw 0x0988 + 0x5b0   ; handler_9:  0x0f38 (duplicate/unused?)
    dw 0x0988 + 0x5b0   ; handler_10: 0x0f38 (duplicate/unused?)
    dw 0x07a5 + 0x5b0   ; handler_11: 0x0d55
    dw 0x07b0 + 0x5b0   ; handler_12: 0x0d60
    dw 0x0dc0 + 0x5b0   ; handler_13: 0x1370
    dw 0x1259 + 0x5b0   ; handler_14: 0x1809
    dw 0x0988 + 0x5b0   ; handler_15: 0x0f38 (duplicate/unused?)
    dw 0x0988 + 0x5b0   ; handler_16: 0x0f38 (duplicate/unused?)
    dw 0x0988 + 0x5b0   ; handler_17: 0x0f38 (duplicate/unused?)
    dw 0x0988 + 0x5b0   ; handler_18: 0x0f38 (duplicate/unused?)
    dw 0x0988 + 0x5b0   ; handler_19: 0x0f38 (duplicate/unused?)
    dw 0x0988 + 0x5b0   ; handler_20: 0x0f38 (duplicate/unused?)
    dw 0x07bb + 0x5b0   ; handler_21: 0x0d6b
    dw 0x080c + 0x5b0   ; handler_22: 0x0dbc
    dw 0x082a + 0x5b0   ; handler_23: 0x0dda
    dw 0x0863 + 0x5b0   ; handler_24: 0x0e13
    dw 0x0878 + 0x5b0   ; handler_25: 0x0e28
    dw 0x0891 + 0x5b0   ; handler_26: 0x0e41
    dw 0x08dc + 0x5b0   ; handler_27: 0x0e8c
    dw 0x08dc + 0x5b0   ; handler_28: 0x0e8c (duplicate/unused?)
; 50 bytes, 25 word-sized offsets (some duplicates)
; End at 0x04b2 (1202 bytes total)

; 3c5x9pd.asm - Packet Driver for 3Com EtherLink III (3C509) ISA Card
;
; To assemble:
;
;   nasm -f bin -o 3c5x9pd.com 3c5x9pd.asm
;
; Requires: data1.asm, code.asm, data2.asm in the same directory
;
; This is the main file for the packet driver, including data and code sections.
; Reconstructed from disassembly, aiming for functional equivalence verified
; through testing.
;
; Assembler: NASM (Netwide Assembler)
; Output Format: Flat binary (.COM file)
; Target Environment: DOS (Real Mode)
;
; --- Memory Map ---
; All addresses are hexadecimal offsets relative to CS:0100h (start of .COM file).
; +-------------------------+  <-- 0x0000 (Start of Segment - CS:0000)
; |  Program Segment Prefix |
; |        (PSP)            |  <-- 0x0100 (Start of .COM file, ORG 0x100)
; +-------------------------+  Size: 256 bytes (0x100) NOT included in .COM file on disk
; |                         |
; |      data1.asm          |  <-- 0x0100 (Start of data1.asm)
; |                         |
; |  driver_header          |  0x0100 (2 bytes)   -> 0x0100–0x0101
; |  driver_name            |  0x0102 (19 bytes)  -> 0x0102–0x0114
; |  config_signature       |  0x0113 (16 bytes)  -> 0x0113–0x0122
; |  zero_buffer_1          |  0x0123 (72 bytes)  -> 0x0123–0x016a
; |  small_config           |  0x016b (11 bytes)  -> 0x016b–0x0175
; |  test_pattern           |  0x0176 (650 bytes) -> 0x0176–0x03ff
; |  post_test_padding      |  0x0400 (5 bytes)   -> 0x0400–0x0404
; |  config_table_1         |  0x0405 (24 bytes)  -> 0x0405–0x041c
; |  zero_buffer_2          |  0x041d (40 bytes)  -> 0x041d–0x0444
; |  misc_config            |  0x0445 (64 bytes)  -> 0x0445–0x0484
; |  config_table_2         |  0x0485 (19 bytes)  -> 0x0485–0x0497
; |  lookup_table           |  0x0498 (16 bytes)  -> 0x0498–0x04a7
; |  zero_buffer_3          |  0x04a8 (128 bytes) -> 0x04a8–0x0527
; |  small_config_2         |  0x0528 (32 bytes, part of zero_buffer_3 total 160) -> 0x0528–0x0547
; |  small_config_3         |  0x0548 (24 bytes)  -> 0x0548–0x055f
; |  zero_buffer_4          |  0x0560 (32 bytes)  -> 0x0560–0x057f
; |  jump_table_pkt         |  0x0580 (50 bytes)  -> 0x0580–0x04b1
; |                         |
; +-------------------------+  End of data1.asm: 0x5af (1202 bytes total)
; |                         |
; |       code.asm          |  <-- 0x05b0 (Start of code.asm)
; |     (main_code)         |  End of code.asm: 0x2382
; |                         |
; +-------------------------+
; |                         |
; |      data2.asm          | <-- 0x2383 (Start of data2.asm)
; |                         |
; |  padding_post_code      |  0x2383 (12 bytes)  -> 0x2383–0x238e
; |  msg_param_error        |  0x238f (40 bytes)  -> 0x238f–0x23b6
; |  msg_press_any_key      |  0x23b7 (35 bytes)  -> 0x23b7–0x23db
; |  msg_using_sync_cycles  |  0x23dc (26 bytes)  -> 0x23dc–0x23f5
; |  box_top_border         |  0x23f6 (80 bytes)  -> 0x23f6–0x2445
; |  box_line_1             |  0x2446 (79 bytes)  -> 0x2446–0x2494
; |  msg_usage_boxed        |  0x2495 (170 bytes) -> 0x2495–0x253e
; |  box_line_2             |  0x253f (79 bytes)  -> 0x253f–0x258d
; |  msg_switches_boxed     |  0x258e (87 bytes)  -> 0x258e–0x25e4
; |  box_line_3             |  0x25e5 (79 bytes)  -> 0x25e5–0x2633
; |  msg_usage_boxed        |  0x2634 (87 bytes)  -> 0x2634–0x268a
; |  box_line_4             |  0x268b (79 bytes)  -> 0x268b–0x26d9
; |  msg_optional_params_boxed | 0x26da (88 bytes) -> 0x26da–0x2731
; |  box_line_5             |  0x2732 (79 bytes)  -> 0x2732–0x2780
; |  box_line_6             |  0x2781 (79 bytes)  -> 0x2781–0x27cf
; |  box_bottom_border      |  0x27d0 (79 bytes)  -> 0x27d0–0x281e
; |  msg_packet_driver_int  |  0x281f (26 bytes)  -> 0x281f–0x2838
; |  msg_slot               |  0x2839 (26 bytes)  -> 0x2839–0x2852
; |  msg_io_base            |  0x2853 (26 bytes)  -> 0x2853–0x286c
; |  msg_interrupt          |  0x286d (26 bytes)  -> 0x286d–0x2886
; |  msg_transceiver        |  0x2887 (26 bytes)  -> 0x2887–0x28a0
; |  msg_eth_addr           |  0x28a1 (26 bytes)  -> 0x28a1–0x28ba
; |  msg_bnc                |  0x28bb (19 bytes)  -> 0x28bb–0x28cd
; |  msg_twisted_pair       |  0x28ce (28 bytes)  -> 0x28ce–0x28e9
; |  msg_external           |  0x28ea (21 bytes)  -> 0x28ea–0x28fe
; |  fifo_transfer_routines |  0x28ff (1773 bytes)-> 0x28ff–0x2fd3
; |  irq_vector_table       |  0x2fd4 (102 bytes) -> 0x2fd4–0x3239
; |                         |
; +-------------------------+  End of data2.asm: 0x32ba
; | final byte              |  0x32bb (1 byte)
; +-------------------------+  End of File: 0x32bb (12,988 bytes total/12,732 bytes on disk)

section .text
org 0x0100

; Entry Point: Jump to initialization code (within code.asm)
jmp main_code

;------------------------------------------------------------------------
; Data Section 1 (Included from data1.asm)
;------------------------------------------------------------------------
%include 'data1.asm'

; Padding to align code.asm at 0x05b0 (optional, but good practice)
; This ensures that code.asm starts at the correct address, even if
; there are slight variations in how NASM handles the 'org' directive.
times 0x05b0 - ($ - $$) db 0

;------------------------------------------------------------------------
; Main Code Section (Included from code.asm)
;------------------------------------------------------------------------
main_code:
%include 'code.asm'

;------------------------------------------------------------------------
; Data Section 2 (Included from data2.asm)
;------------------------------------------------------------------------
%include 'data2.asm'

; Final byte (padding, often found in COM files)
db 0x00

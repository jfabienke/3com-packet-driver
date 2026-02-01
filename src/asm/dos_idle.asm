; dos_idle.asm - DOS idle-time background processing hook
;
; 3Com Packet Driver - Replaces src/c/dos_idle.c
; Last Updated: 2026-02-01 16:00:00 CET
;
; Called from INT 28h handler to process deferred work outside ISR context.
; Original C: 22 LOC, 1,621 bytes compiled. ASM: ~10 bytes.

segment dos_idle_TEXT class=CODE

extern packet_process_deferred_work_
extern pcmcia_poll_

; void dos_idle_background_processing(void)
global dos_idle_background_processing_
dos_idle_background_processing_:
    call far packet_process_deferred_work_
    call far pcmcia_poll_
    retf

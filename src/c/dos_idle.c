/**
 * @file dos_idle.c
 * @brief DOS idle-time background processing hook
 *
 * Provides the function called from the INT 28h handler to process
 * deferred work outside ISR context.
 */

#include "../include/packet_ops.h"
#include "../include/pcmcia.h"

/*
 * Called from assembly (src/asm/main.asm) during DOS idle (INT 28h).
 * Keep this minimal and non-blocking.
 */
void dos_idle_background_processing(void) {
    /* Process TX completions, VDS unlocks, and deferred RX packets */
    packet_process_deferred_work();

    /* Background PCMCIA/CardBus polling (non-blocking) */
    pcmcia_poll();
}

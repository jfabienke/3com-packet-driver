/**
 * @file pcmcia_snapshot.c
 * @brief Build PCMCIA/CardBus snapshot for Extension API AH=98h
 */

#include <string.h>
#include "common.h"
#include "logging.h"
#include "pcmcia.h"
#include "pcmsnap.h"

/* Share minimal state with manager via accessors; manager keeps static globals */

extern int pcmcia_manager_fill_snapshot(pcmcia_socket_info_t *entries,
                                        uint16_t max_entries,
                                        uint8_t *capabilities,
                                        uint8_t *count_out);

int pcmcia_get_snapshot(void far *dst, uint16_t max_bytes) {
    pcmcia_snapshot_header_t hdr;
    pcmcia_socket_info_t temp[4];
    char far *p;
    uint16_t bytes_left;
    uint16_t max_ent;
    uint8_t caps;
    uint8_t cnt;

    /* Initialize variables */
    caps = 0;
    cnt = 0;
    hdr.socket_count = 0;
    hdr.capabilities = 0;
    hdr.reserved = 0;

    if (!dst || max_bytes < sizeof(hdr)) {
        return -1;
    }

    /* Far pointer write */
    p = (char far *)dst;
    _fmemcpy(p, &hdr, sizeof(hdr));
    p = p + sizeof(hdr);

    /* Fill entries */
    bytes_left = max_bytes - sizeof(hdr);
    max_ent = bytes_left / sizeof(pcmcia_socket_info_t);
    memset(temp, 0, sizeof(temp));

    (void)pcmcia_manager_fill_snapshot(temp, (max_ent > 4) ? 4 : (uint8_t)max_ent, &caps, &cnt);
    ((pcmcia_snapshot_header_t far *)dst)->capabilities = caps;
    ((pcmcia_snapshot_header_t far *)dst)->socket_count = cnt;
    if (cnt > 0) {
        _fmemcpy(p, temp, cnt * sizeof(pcmcia_socket_info_t));
        return sizeof(hdr) + cnt * sizeof(pcmcia_socket_info_t);
    }
    return sizeof(hdr);
}

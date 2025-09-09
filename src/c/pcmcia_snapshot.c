/**
 * @file pcmcia_snapshot.c
 * @brief Build PCMCIA/CardBus snapshot for Extension API AH=98h
 */

#include <string.h>
#include "../include/common.h"
#include "../include/logging.h"
#include "../include/pcmcia.h"
#include "../include/pcmcia_snapshot.h"

/* Share minimal state with manager via accessors; manager keeps static globals */

extern int pcmcia_manager_fill_snapshot(pcmcia_socket_info_t *entries,
                                        uint16_t max_entries,
                                        uint8_t *capabilities,
                                        uint8_t *count_out);

int pcmcia_get_snapshot(void far *dst, uint16_t max_bytes) {
    pcmcia_snapshot_header_t hdr;
    uint8_t caps = 0;
    hdr.socket_count = 0;
    hdr.capabilities = 0;
    hdr.reserved = 0;

    if (!dst || max_bytes < sizeof(hdr)) {
        return -1;
    }

    /* Far pointer write */
    void far *p = dst;
    _fmemcpy(p, &hdr, sizeof(hdr));
    p = (char far *)p + sizeof(hdr);

    /* Fill entries */
    uint16_t bytes_left = max_bytes - sizeof(hdr);
    uint16_t max_entries = bytes_left / sizeof(pcmcia_socket_info_t);
    pcmcia_socket_info_t temp[4];
    memset(temp, 0, sizeof(temp));

    uint8_t cnt = 0;
    (void)pcmcia_manager_fill_snapshot(temp, (max_entries > 4) ? 4 : (uint8_t)max_entries, &caps, &cnt);
    ((pcmcia_snapshot_header_t far *)dst)->capabilities = caps;
    ((pcmcia_snapshot_header_t far *)dst)->socket_count = cnt;
    if (cnt > 0) {
        _fmemcpy(p, temp, cnt * sizeof(pcmcia_socket_info_t));
        return sizeof(hdr) + cnt * sizeof(pcmcia_socket_info_t);
    }
    return sizeof(hdr);
}

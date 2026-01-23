/**
 * @file pcmcia_ss.h
 * @brief Socket Services helper API (INT 1Ah)
 */

#ifndef _PCMCIA_SS_H_
#define _PCMCIA_SS_H_

#include <stdint.h>

/* Generic window parameter placeholder for SS_SET_WINDOW */
typedef struct {
    uint16_t offset;  /* attribute memory offset */
    uint16_t size;    /* bytes to map/copy */
    void far *buffer; /* destination buffer */
} ss_window_params_t;

int ss_available(void);
int ss_get_socket_count(int *adapters, int *sockets);
int ss_get_socket_status(uint16_t socket, uint8_t *status);
int ss_set_socket_params(uint16_t socket, void far *params, uint16_t attrs);
int ss_set_window_params(uint16_t socket, void far *params, uint16_t attrs);

/* Convenience: attempt to read CIS bytes into buffer (returns 0 on success). */
int ss_read_cis(uint16_t socket, uint16_t offset, void far *dst, uint16_t len);

#endif /* _PCMCIA_SS_H_ */


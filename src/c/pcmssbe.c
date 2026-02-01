/**
 * @file pcmcia_ss_backend.c
 * @brief Socket Services backend (INT 1Ah) - minimal detection stubs
 */

/* Minimal Socket Services backend using INT 1Ah.
 * Function interface (based on archived headers):
 * AX=function, BX=socket, ES:DI=buffer, CX=attributes; returns AX=status.
 */

#include <dos.h>
#include <string.h>
#include "logging.h"
#include "pcmss.h"

/* Socket Services function codes */
#define SS_GET_ADAPTER_COUNT    0x80
#define SS_GET_SOCKET_COUNT     0x81
#define SS_GET_SOCKET           0x84
#define SS_SET_SOCKET           0x83
#define SS_SET_WINDOW           0x89

/* Return codes */
#define SS_SUCCESS              0x00

static int ss_call(uint16_t function, uint16_t socket, void far *buffer, uint16_t attrs, uint16_t *ax_out)
{
    union REGS regs; struct SREGS sregs;
    memset(&regs, 0, sizeof(regs)); memset(&sregs, 0, sizeof(sregs));
    regs.x.ax = function;
    regs.x.bx = socket;
    regs.x.cx = attrs;
    if (buffer) {
        sregs.es = FP_SEG(buffer);
        regs.x.di = FP_OFF(buffer);
    }
    int86x(0x1A, &regs, &regs, &sregs);
    if (ax_out) *ax_out = regs.x.ax;
    return (regs.x.ax == SS_SUCCESS) ? 0 : -1;
}

/* Socket Services presence check (stub): returns 0 (not available) by default. */
int ss_available(void)
{
    uint16_t ax = 0; uint16_t adapters = 0;
    /* Query adapter count via SS; if call returns success, SS is present */
    return (ss_call(SS_GET_ADAPTER_COUNT, 0, (void far*)&adapters, 0, &ax) == 0 && adapters > 0);
}

int ss_get_socket_count(int *adapters, int *sockets)
{
    uint16_t ax;
    uint16_t adapter_count = 0, socket_count = 0;
    if (ss_call(SS_GET_ADAPTER_COUNT, 0, (void far*)&adapter_count, 0, &ax) != 0) {
        if (adapters) *adapters = 0;
        if (sockets) *sockets = 0;
        return -1;
    }
    if (ss_call(SS_GET_SOCKET_COUNT, 0, (void far*)&socket_count, 0, &ax) != 0) {
        if (adapters) *adapters = adapter_count;
        if (sockets) *sockets = 0;
        return -1;
    }
    if (adapters) *adapters = adapter_count;
    if (sockets) *sockets = socket_count;
    return 0;
}

int ss_get_socket_status(uint16_t socket, uint8_t *status)
{
    uint16_t ax; uint8_t far *p = status;
    if (!status) return -1;
    /* Attributes=0; buffer points to a byte receiving status */
    if (ss_call(SS_GET_SOCKET, socket, (void far*)p, 0, &ax) != 0) return -1;
    return 0;
}

int ss_set_socket_params(uint16_t socket, void far *params, uint16_t attrs)
{
    uint16_t ax; return ss_call(SS_SET_SOCKET, socket, params, attrs, &ax);
}

int ss_set_window_params(uint16_t socket, void far *params, uint16_t attrs)
{
    uint16_t ax; return ss_call(SS_SET_WINDOW, socket, params, attrs, &ax);
}

int ss_read_cis(uint16_t socket, uint16_t offset, void far *dst, uint16_t len)
{
    /* Build a minimal parameter block; real SS would take detailed window params. */
    ss_window_params_t wp;
    wp.offset = offset;
    wp.size = len;
    wp.buffer = dst;
    /* Attributes: set bit to indicate attribute memory (example flag 0x40 as in archive stub) */
    if (ss_set_window_params(socket, (void far*)&wp, 0x0040) != 0) {
        return -1;
    }
    /* On some SS, the call copies data to the provided buffer. */
    return 0;
}

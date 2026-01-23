/**
 * @file pcmcia_pe_backend.c
 * @brief Point Enabler (PCIC) backend - minimal helpers
 */

#include "../include/common.h"
#include "../include/logging.h"

/* PCIC register indices and bits (subset) */
#define PCIC_STATUS             0x01  /* Interface Status */
#define PCIC_POWER_CONTROL      0x02  /* Power Control */

/* Status bits */
#define PCIC_STATUS_CD1         0x01
#define PCIC_STATUS_CD2         0x02
#define PCIC_STATUS_READY       0x20
#define PCIC_STATUS_POWER       0x40

/* Power control bits */
#define PCIC_POWER_OFF          0x00
#define PCIC_POWER_VCC_5V       0x10

/* PCIC register helpers (match pcmcia_manager’s addressing) */
static inline void pe_write(uint16_t index_port, uint8_t socket, uint8_t reg, uint8_t val) {
    uint8_t idx = (uint8_t)((socket << 6) | (reg & 0x3F));
    outb(index_port, idx);
    outb(index_port + 1, val);
}

static inline uint8_t pe_read(uint16_t index_port, uint8_t socket, uint8_t reg) {
    uint8_t idx = (uint8_t)((socket << 6) | (reg & 0x3F));
    outb(index_port, idx);
    return inb(index_port + 1);
}

/* Best-effort check for card presence: read a status reg and look for sane bits */
int pe_get_card_present(uint16_t io_base, uint8_t socket)
{
    /* Use PCIC_STATUS: both CD bits low indicates card present on many PCICs */
    uint8_t st = pe_read(io_base, socket, PCIC_STATUS);
    int cd1 = (st & PCIC_STATUS_CD1) ? 1 : 0;
    int cd2 = (st & PCIC_STATUS_CD2) ? 1 : 0;
    return (cd1 == 0 && cd2 == 0) ? 1 : 0;
}

/* Power control stubs (would program PCIC power control registers) */
int pe_enable_power(uint16_t io_base, uint8_t socket)
{
    /* Select 5V Vcc for simplicity */
    pe_write(io_base, socket, PCIC_POWER_CONTROL, PCIC_POWER_VCC_5V);
    /* Optional: small delay could be inserted here */
    return 0;
}

int pe_disable_power(uint16_t io_base, uint8_t socket)
{
    pe_write(io_base, socket, PCIC_POWER_CONTROL, PCIC_POWER_OFF);
    return 0;
}

/* Map attribute memory and copy CIS bytes to dst buffer (best-effort stub).
 * Returns 0 on success. */
int pe_read_cis(uint16_t io_base, uint8_t socket, uint16_t offset, void far *dst, uint16_t len)
{
    /* Program a memory window (using registers as in archived stub) */
    /* Window 0 setup for attribute memory: start 0x0000, end 0x0FFF, offset 0 */
    pe_write(io_base, socket, 0x10, 0x00);  /* Mem win 0 start low */
    pe_write(io_base, socket, 0x11, 0x00);  /* Mem win 0 start high */
    pe_write(io_base, socket, 0x12, 0xFF);  /* Mem win 0 end low */
    pe_write(io_base, socket, 0x13, 0x0F);  /* Mem win 0 end high */
    pe_write(io_base, socket, 0x14, 0x00);  /* Mem win 0 offset low */
    pe_write(io_base, socket, 0x15, 0x00);  /* Mem win 0 offset high */
    /* Enable attribute memory window — controller-specific; use 0x40 bit as in archive stub */
    pe_write(io_base, socket, 0x06, 0x40);
    /* In DOS real mode without a memory mapping facility, we cannot actually deref windowed memory. */
    /* As a best-effort, zero the buffer to allow parser fallbacks. */
    if (dst && len) {
        _fmemset(dst, 0, len);
    }
    return 0;
}

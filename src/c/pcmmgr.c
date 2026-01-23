/**
 * @file pcmcia_manager.c
 * @brief Minimal integrated PCMCIA/CardBus detection & poll scaffolding
 *
 * Cold-only manager that detects:
 *  - 16-bit PCMCIA controllers (Intel 82365-compatible) via PCIC I/O probe
 *  - 32-bit CardBus bridges via PCI BIOS class scan
 *
 * Designed to be extended with Socket Services / Point Enabler backends and
 * hotplug ISR in future iterations. For now, this provides capability
 * detection and a safe background poll hook that can later drive attach/detach.
 */

#include <string.h>
#include "../include/common.h"
#include "../include/logging.h"
#include "../include/pcmcia.h"
#include "../include/pci_bios.h"
#include "../include/pcmcis.h"

/* ------------------------ PCIC (ISA) probe ------------------------ */

/* Common PCIC index/data base addresses to probe */
static const uint16_t k_pcic_index_ports[] = { 0x3E0, 0x3E2, 0x4E0, 0x4E2, 0 };

/* PCIC register addressing: index = (socket << 6) | reg */
static inline void pcic_write(uint16_t index_port, uint8_t socket, uint8_t reg, uint8_t val) {
    uint8_t idx = (uint8_t)((socket << 6) | (reg & 0x3F));
    outb(index_port, idx);
    outb(index_port + 1, val);
}

static inline uint8_t pcic_read(uint16_t index_port, uint8_t socket, uint8_t reg) {
    uint8_t idx = (uint8_t)((socket << 6) | (reg & 0x3F));
    outb(index_port, idx);
    return inb(index_port + 1);
}

static bool probe_pcic_controller(uint16_t *io_base_out, uint8_t *socket_count_out) {
    for (int i = 0; k_pcic_index_ports[i] != 0; i++) {
        uint16_t io = k_pcic_index_ports[i];
        /* Use scratch register 0x0E on socket 0 for a simple echo test */
        pcic_write(io, 0, 0x0E, 0xAA);
        uint8_t r1 = pcic_read(io, 0, 0x0E);
        pcic_write(io, 0, 0x0E, 0x55);
        uint8_t r2 = pcic_read(io, 0, 0x0E);
        if (r1 == 0xAA && r2 == 0x55) {
            /* Determine socket count by probing socket 0..3 status register (0x10 is common) */
            uint8_t sockets = 0;
            for (uint8_t s = 0; s < 4; s++) {
                /* If reads look sane (low bits not all set), assume socket exists */
                uint8_t status = pcic_read(io, s, 0x10);
                if ((status & 0x0F) == 0x00) {
                    sockets++;
                } else {
                    break;
                }
            }
            if (sockets == 0) sockets = 1; /* Conservative */
            if (io_base_out) *io_base_out = io;
            if (socket_count_out) *socket_count_out = sockets;
            return true;
        }
    }
    return false;
}

/* ------------------------ CardBus (PCI) probe --------------------- */

static bool probe_cardbus_bridge(void) {
    /* PCI class code: Base 0x06 (Bridge), Subclass 0x07 (CardBus), ProgIF 0x00 */
    const uint32_t class_code = (0x06UL << 16) | (0x07UL << 8) | 0x00UL;
    uint16_t idx = 0;
    uint8_t bus, dev, fn;
    while (pci_find_class(class_code, idx++, &bus, &dev, &fn)) {
        LOG_INFO("CardBus bridge detected at %02X:%02X.%u", bus, dev, fn);
        return true; /* At least one present */
    }
    return false;
}

/* ------------------------ Public API & snapshot fill -------------- */

static bool g_pcic_present = false;
static bool g_ss_present = false;
static bool g_cardbus_present = false;
static bool g_pcmcia_initialized = false;
static uint16_t g_pcic_io_base = 0;
static uint8_t g_pcic_sockets = 0;

typedef struct {
    uint8_t present;
    uint8_t card_present;
    uint8_t powered;
    uint16_t io_base;
    uint8_t irq;
    uint8_t type; /* 1=PCMCIA, 2=CardBus */
    uint8_t attached;  /* 1 if NIC attached */
    int8_t nic_index;  /* index in g_nics or -1 */
} pcmcia_sock_state_t;

static pcmcia_sock_state_t g_sock_state[4];
/* Event flag set by tiny ISR to trigger bottom-half scan */
volatile uint8_t pcmcia_event_flag = 0;

int pcmcia_init(void) {
    if (g_pcmcia_initialized) return (g_pcic_present ? g_pcic_sockets : 0);

    /* Prefer Socket Services when available */
    if (ss_available()) {
        int adapters = 0, sockets = 0;
        if (ss_get_socket_count(&adapters, &sockets) == 0 && sockets > 0) {
            g_ss_present = true;
            if (sockets > 4) sockets = 4;
            g_pcic_sockets = (uint8_t)sockets; /* reuse socket count */
            for (uint8_t s = 0; s < g_pcic_sockets; s++) {
                g_sock_state[s].present = 1;
                g_sock_state[s].type = 1; /* PCMCIA */
                g_sock_state[s].attached = 0;
                g_sock_state[s].nic_index = -1;
            }
            LOG_INFO("Socket Services detected: adapters=%d, sockets=%d", adapters, sockets);
        }
    } else {
        /* Probe 16-bit PCMCIA controller (PCIC) */
        g_pcic_present = probe_pcic_controller(&g_pcic_io_base, &g_pcic_sockets);
        if (g_pcic_present) {
            LOG_INFO("PCMCIA controller detected: IO=0x%04X, sockets=%u", g_pcic_io_base, g_pcic_sockets);
            if (g_pcic_sockets > 4) g_pcic_sockets = 4;
            for (uint8_t s = 0; s < g_pcic_sockets; s++) {
                g_sock_state[s].present = 1;
                g_sock_state[s].type = 1;
                g_sock_state[s].attached = 0;
                g_sock_state[s].nic_index = -1;
            }
        } else {
            LOG_DEBUG("No PCMCIA controller detected (PCIC probe)");
        }
    }

    /* Probe 32-bit CardBus via PCI BIOS */
    if (pci_bios_present()) {
        g_cardbus_present = probe_cardbus_bridge();
        if (!g_cardbus_present) {
            LOG_DEBUG("No CardBus bridges detected via PCI BIOS");
        }
    } else {
        LOG_DEBUG("PCI BIOS not present; skipping CardBus probe");
        g_cardbus_present = false;
    }

    g_pcmcia_initialized = true;
    /* Install tiny ISR on a conventional controller IRQ (default 10) */
    {
        extern void pcmcia_isr_install(unsigned char irq);
        pcmcia_isr_install(10);
        LOG_DEBUG("PCMCIA status ISR installed on IRQ %u", 10);
    }
    return (g_pcic_present ? g_pcic_sockets : 0);
}

void pcmcia_cleanup(void) {
    g_pcmcia_initialized = false;
    g_pcic_present = false;
    g_cardbus_present = false;
    g_pcic_io_base = 0;
    g_pcic_sockets = 0;
    /* Uninstall ISR if installed */
    {
        extern void pcmcia_isr_uninstall(void);
        pcmcia_isr_uninstall();
        LOG_DEBUG("PCMCIA status ISR uninstalled");
    }
}

/* Backend prototypes */
int ss_available(void);
int ss_get_socket_count(int *adapters, int *sockets);
int pe_get_card_present(uint16_t io_base, uint8_t socket);
int pe_enable_power(uint16_t io_base, uint8_t socket);
int pe_disable_power(uint16_t io_base, uint8_t socket);

void pcmcia_poll(void) {
    if (!g_pcmcia_initialized) return;
    if (!pcmcia_event_flag) return; /* No pending events */
    pcmcia_event_flag = 0;
    /* Prefer Socket Services when available */
    if (g_ss_present) {
        /* Query per-socket status via SS_GET_SOCKET and set basic fields */
        extern int ss_get_socket_status(uint16_t socket, uint8_t *status);
        extern int ss_read_cis(uint16_t socket, uint16_t offset, void far *dst, uint16_t len);
        for (uint8_t s = 0; s < g_pcic_sockets && s < 4; s++) {
            uint8_t st = 0;
            if (ss_get_socket_status(s, &st) == 0) {
                /* CARD_DETECT bit approximated as bit0 */
                g_sock_state[s].card_present = (st & 0x01) ? 1 : 0;
                if (g_sock_state[s].card_present) {
                    /* Minimal defaults until CIS parser fills real values */
                    g_sock_state[s].powered = 1;
                    /* Attempt minimal CIS parse by asking SS to map/copy CIS into buffer */
                    uint8_t cis_buf[128]; memset(cis_buf, 0, sizeof(cis_buf));
                    if (ss_read_cis(s, 0, (void far*)cis_buf, sizeof(cis_buf)) == 0) {
                        uint16_t io_tmp = 0; uint8_t irq_tmp = 0;
                        if (pcmcia_cis_parse_3com(cis_buf, sizeof(cis_buf), &io_tmp, &irq_tmp) == 0) {
                            g_sock_state[s].io_base = io_tmp;
                            g_sock_state[s].irq = irq_tmp;
                        }
                    }
                    if (g_sock_state[s].io_base == 0) g_sock_state[s].io_base = (uint16_t)(0x300 + (s * 0x20));
                    if (g_sock_state[s].irq == 0) g_sock_state[s].irq = (uint8_t)(s == 0 ? 10 : 11);
                    /* Attach if not yet attached and resources available */
                    if (!g_sock_state[s].attached && g_sock_state[s].io_base && g_sock_state[s].irq) {
                        extern int hardware_attach_pcmcia_nic(uint16_t io_base, uint8_t irq, uint8_t socket);
                        int idx = hardware_attach_pcmcia_nic(g_sock_state[s].io_base, g_sock_state[s].irq, s);
                        if (idx >= 0) { g_sock_state[s].attached = 1; g_sock_state[s].nic_index = (int8_t)idx; }
                    }
                }
                else if (g_sock_state[s].attached) {
                    /* Detach NIC if card removed */
                    extern int hardware_detach_nic_by_index(int index);
                    hardware_detach_nic_by_index(g_sock_state[s].nic_index);
                    g_sock_state[s].attached = 0; g_sock_state[s].nic_index = -1;
                    g_sock_state[s].powered = 0; g_sock_state[s].io_base = 0; g_sock_state[s].irq = 0;
                }
            }
        }
    } else if (g_pcic_present) {
        /* PE mode: read minimal presence and set conservative defaults */
        extern int pe_read_cis(uint16_t io_base, uint8_t socket, uint16_t offset, void far *dst, uint16_t len);
        for (uint8_t s = 0; s < g_pcic_sockets && s < 4; s++) {
            int present = pe_get_card_present(g_pcic_io_base, s);
            g_sock_state[s].card_present = present ? 1 : 0;
            if (present) {
                /* Enable power and assign default resources (placeholder) */
                pe_enable_power(g_pcic_io_base, s);
                g_sock_state[s].powered = 1;
                /* Attempt to read CIS via PE window programming */
                uint8_t cis_buf[128]; _fmemset(cis_buf, 0, sizeof(cis_buf));
                if (pe_read_cis(g_pcic_io_base, s, 0, (void far*)cis_buf, sizeof(cis_buf)) == 0) {
                    uint16_t io_tmp = 0; uint8_t irq_tmp = 0;
                    if (pcmcia_cis_parse_3com(cis_buf, sizeof(cis_buf), &io_tmp, &irq_tmp) == 0) {
                        g_sock_state[s].io_base = io_tmp;
                        g_sock_state[s].irq = irq_tmp;
                    }
                }
                if (g_sock_state[s].io_base == 0) g_sock_state[s].io_base = (uint16_t)(0x300 + (s * 0x20));
                if (g_sock_state[s].irq == 0) g_sock_state[s].irq = (uint8_t)(s == 0 ? 10 : 11);
                if (!g_sock_state[s].attached && g_sock_state[s].io_base && g_sock_state[s].irq) {
                    extern int hardware_attach_pcmcia_nic(uint16_t io_base, uint8_t irq, uint8_t socket);
                    int idx = hardware_attach_pcmcia_nic(g_sock_state[s].io_base, g_sock_state[s].irq, s);
                    if (idx >= 0) { g_sock_state[s].attached = 1; g_sock_state[s].nic_index = (int8_t)idx; }
                }
            }
            else if (g_sock_state[s].attached) {
                extern int hardware_detach_nic_by_index(int index);
                hardware_detach_nic_by_index(g_sock_state[s].nic_index);
                g_sock_state[s].attached = 0; g_sock_state[s].nic_index = -1;
                g_sock_state[s].powered = 0; g_sock_state[s].io_base = 0; g_sock_state[s].irq = 0;
            }
        }
    }
}

bool pcmcia_controller_present(void) { return g_pcic_present; }
bool cardbus_present(void) { return g_cardbus_present; }

int pcmcia_manager_fill_snapshot(pcmcia_socket_info_t *entries,
                                 uint16_t max_entries,
                                 uint8_t *capabilities,
                                 uint8_t *count_out) {
    if (!entries || !capabilities || !count_out) return -1;
    uint8_t caps = 0;
    if (g_pcic_present || g_ss_present) caps |= 0x01;
    if (g_cardbus_present) caps |= 0x02;
    *capabilities = caps;

    uint8_t cnt = 0;
    if (g_pcic_present) {
        uint8_t limit = (g_pcic_sockets < max_entries) ? g_pcic_sockets : (uint8_t)max_entries;
        for (uint8_t s = 0; s < limit; s++) {
            entries[cnt].socket_id = s;
            entries[cnt].present = g_sock_state[s].present;
            entries[cnt].card_present = g_sock_state[s].card_present;
            entries[cnt].powered = g_sock_state[s].powered;
            entries[cnt].io_base = g_sock_state[s].io_base;
            entries[cnt].irq = g_sock_state[s].irq;
            entries[cnt].type = g_sock_state[s].type;
            cnt++;
        }
    }
    /* Append a pseudo entry for CardBus if present and room remains */
    if (g_cardbus_present && cnt < max_entries) {
        entries[cnt].socket_id = cnt; /* index after PCMCIA sockets */
        entries[cnt].present = 1;
        entries[cnt].card_present = 0; /* unknown at this abstraction */
        entries[cnt].powered = 0;
        entries[cnt].io_base = 0;
        entries[cnt].irq = 0;
        entries[cnt].type = 2; /* CardBus */
        cnt++;
    }
    *count_out = cnt;
    return 0;
}

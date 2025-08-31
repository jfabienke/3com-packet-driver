/**
 * Register Dump Utility for 3Com NICs
 * 
 * This utility reads and displays all registers from 3C509B and 3C515 NICs.
 * Used for debugging hardware issues and validating QEMU emulation.
 */

#include <stdio.h>
#include <stdint.h>
#include <dos.h>
#include <conio.h>
#include <string.h>

/* I/O Access Macros */
#define inb(port)       inp(port)
#define inw(port)       inpw(port)
#define inl(port)       (inpw(port) | ((uint32_t)inpw((port)+2) << 16))
#define outb(port, val) outp(port, val)
#define outw(port, val) outpw(port, val)

/* Command Register (0x0E) Commands */
#define CMD_SELECT_WINDOW(w)    (0x0800 | (w))
#define CMD_TOTAL_RESET         0x0000

/* Window Names */
static const char *window_names[] = {
    "Config/EEPROM",    /* Window 0 */
    "Operating/FIFO",   /* Window 1 */
    "Station Address",  /* Window 2 */
    "Multicast",        /* Window 3 */
    "Diagnostics",      /* Window 4 */
    "Reserved",         /* Window 5 */
    "Statistics",       /* Window 6 */
    "Bus Master"        /* Window 7 */
};

/* Register Names by Window */
static const char *window0_regs[] = {
    "Reserved", "Reserved", "Config Ctrl", "Addr Config",
    "IRQ Config", "EEPROM Cmd", "EEPROM Data", "Reserved"
};

static const char *window1_regs[] = {
    "TX/RX FIFO", "TX/RX FIFO", "Reserved", "Reserved",
    "RX Status", "Timer/TX Status", "TX Free", "Command/Status"
};

static const char *window2_regs[] = {
    "MAC 0-1", "MAC 2-3", "MAC 4-5", "Reserved",
    "Reserved", "Reserved", "Reserved", "Command/Status"
};

static const char *window3_regs[] = {
    "Multicast 0-1", "Multicast 2-3", "Multicast 4-5", "Multicast 6-7",
    "Reserved", "Reserved", "Reserved", "Command/Status"
};

static const char *window4_regs[] = {
    "VCO Status", "Reserved", "FIFO Diag", "Net Diag",
    "Reserved", "Media Status", "Reserved", "Command/Status"
};

static const char *window6_regs[] = {
    "Carrier Errors", "Heartbeat Err", "Mult Colls", "Single Colls",
    "Late Colls", "RX Overruns", "TX Frames OK", "RX Frames OK",
    "TX Deferrals", "Reserved", "RX Bytes OK", "TX Bytes OK",
    "Reserved", "Bad SSD", "Command/Status", "Command/Status"
};

static const char *window7_regs[] = {
    "Master Addr Lo", "Master Addr Hi", "Reserved", "Master Len",
    "Reserved", "Reserved", "Master Status", "Command/Status"
};

/* Card Detection */
static int detect_3com_card(uint16_t io_base) {
    uint16_t status;
    
    /* Try to read status register */
    status = inw(io_base + 0x0E);
    
    /* Check for reasonable status value */
    if (status == 0xFFFF || status == 0x0000) {
        return 0;  /* Likely no card */
    }
    
    /* Select window 0 and read product ID from EEPROM */
    outw(io_base + 0x0E, CMD_SELECT_WINDOW(0));
    
    /* Issue EEPROM read for product ID (address 3) */
    outw(io_base + 0x0A, 0x80 | 0x03);
    
    /* Wait for EEPROM */
    for (int i = 0; i < 1000; i++) {
        if (!(inw(io_base + 0x0A) & 0x8000)) {
            break;
        }
    }
    
    /* Read product ID */
    uint16_t product_id = inw(io_base + 0x0C);
    
    if (product_id == 0x6D50) {
        return 1;  /* 3C509B */
    } else if (product_id == 0x5051) {
        return 2;  /* 3C515-TX */
    }
    
    return 0;  /* Unknown */
}

/* Read EEPROM */
static uint16_t read_eeprom(uint16_t io_base, uint8_t addr, int is_3c515) {
    uint16_t cmd_port = io_base + (is_3c515 ? 0x200A : 0x0A);
    uint16_t data_port = io_base + (is_3c515 ? 0x200C : 0x0C);
    
    /* Select window 0 */
    outw(io_base + 0x0E, CMD_SELECT_WINDOW(0));
    
    /* Issue read command */
    outw(cmd_port, 0x80 | (addr & 0x3F));
    
    /* Wait for completion */
    for (int i = 0; i < 1000; i++) {
        if (!(inw(cmd_port) & 0x8000)) {
            break;
        }
    }
    
    return inw(data_port);
}

/* Dump Window Registers */
static void dump_window(uint16_t io_base, int window) {
    const char **reg_names = NULL;
    int num_regs = 8;
    
    /* Select register names based on window */
    switch (window) {
        case 0: reg_names = window0_regs; break;
        case 1: reg_names = window1_regs; break;
        case 2: reg_names = window2_regs; break;
        case 3: reg_names = window3_regs; break;
        case 4: reg_names = window4_regs; break;
        case 6: reg_names = window6_regs; num_regs = 16; break;
        case 7: reg_names = window7_regs; break;
        default: return;  /* Skip reserved windows */
    }
    
    /* Select window */
    outw(io_base + 0x0E, CMD_SELECT_WINDOW(window));
    
    printf("\n=== Window %d: %s ===\n", window, window_names[window]);
    
    /* Read and display registers */
    for (int i = 0; i < num_regs; i += 2) {
        uint16_t val = inw(io_base + i);
        
        if (reg_names && strcmp(reg_names[i/2], "Reserved") != 0) {
            printf("  [%02X] %-16s: 0x%04X", i, reg_names[i/2], val);
            
            /* Decode some important values */
            if (window == 1 && i == 0x08) {  /* RX Status */
                if (val & 0x8000) printf(" (Incomplete)");
                if (val & 0x4000) printf(" (Error)");
                printf(" Length=%d", val & 0x7FF);
            } else if (window == 1 && i == 0x0C) {  /* TX Free */
                printf(" (%d bytes free)", val);
            } else if (window == 4 && i == 0x06) {  /* Net Diag */
                if (val & 0x0080) printf(" (Link OK)");
            }
            
            printf("\n");
        }
    }
}

/* Dump EEPROM Contents */
static void dump_eeprom(uint16_t io_base, int is_3c515) {
    printf("\n=== EEPROM Contents ===\n");
    
    for (int i = 0; i < 32; i++) {
        uint16_t val = read_eeprom(io_base, i, is_3c515);
        
        printf("  [%02X]: 0x%04X", i, val);
        
        /* Decode known EEPROM locations */
        switch (i) {
            case 0x00: printf(" (MAC 0-1: %02X:%02X)", val & 0xFF, val >> 8); break;
            case 0x01: printf(" (MAC 2-3: %02X:%02X)", val & 0xFF, val >> 8); break;
            case 0x02: printf(" (MAC 4-5: %02X:%02X)", val & 0xFF, val >> 8); break;
            case 0x03: printf(" (Product ID)"); break;
            case 0x07: printf(" (3Com ID)"); break;
            case 0x08: printf(" (Mfg Date)"); break;
            case 0x0D: printf(" (Software Config)"); break;
        }
        
        printf("\n");
        
        if ((i + 1) % 8 == 0) {
            printf("\n");
        }
    }
}

/* Dump Command/Status Register */
static void dump_status(uint16_t io_base) {
    uint16_t status = inw(io_base + 0x0E);
    
    printf("\n=== Command/Status Register (0x0E) ===\n");
    printf("  Raw Value: 0x%04X\n", status);
    printf("  Status Bits:\n");
    
    if (status & 0x0001) printf("    [0] INT_LATCH - Interrupt occurred\n");
    if (status & 0x0002) printf("    [1] ADAPTER_FAILURE - Hardware failure\n");
    if (status & 0x0004) printf("    [2] TX_COMPLETE - Transmission complete\n");
    if (status & 0x0008) printf("    [3] TX_AVAILABLE - TX FIFO has space\n");
    if (status & 0x0010) printf("    [4] RX_COMPLETE - Packet received\n");
    if (status & 0x0020) printf("    [5] RX_EARLY - Early RX\n");
    if (status & 0x0040) printf("    [6] INT_REQ - Interrupt requested\n");
    if (status & 0x0080) printf("    [7] STATS_FULL - Statistics updated\n");
    if (status & 0x0100) printf("    [8] DMA_DONE - DMA complete (3C515)\n");
    if (status & 0x0200) printf("    [9] DOWN_COMPLETE - TX DMA done (3C515)\n");
    if (status & 0x0400) printf("    [10] UP_COMPLETE - RX DMA done (3C515)\n");
    if (status & 0x0800) printf("    [11] DMA_IN_PROGRESS (3C515)\n");
    if (status & 0x1000) printf("    [12] CMD_IN_PROGRESS - Command busy\n");
    
    printf("  Current Window: %d\n", (status >> 13) & 0x07);
}

/* Dump 3C515 DMA Registers */
static void dump_3c515_dma(uint16_t io_base) {
    printf("\n=== 3C515 DMA Registers ===\n");
    
    /* Select window 7 */
    outw(io_base + 0x0E, CMD_SELECT_WINDOW(7));
    
    printf("  Master Address: 0x%08lX\n", inl(io_base + 0x00));
    printf("  Master Length:  0x%04X\n", inw(io_base + 0x06));
    printf("  Master Status:  0x%04X\n", inw(io_base + 0x0C));
    
    /* DMA registers at offset 0x400 (if accessible) */
    printf("\n  DMA Control Registers (Base + 0x400):\n");
    printf("    Down List Ptr:  0x%08lX\n", inl(io_base + 0x24));
    printf("    Up List Ptr:    0x%08lX\n", inl(io_base + 0x38));
    printf("    Down Pkt Status: 0x%08lX\n", inl(io_base + 0x20));
    printf("    Up Pkt Status:   0x%08lX\n", inl(io_base + 0x30));
}

/* Dump MII PHY Registers (3C515) */
static void dump_mii_phy(uint16_t io_base) {
    printf("\n=== MII PHY Registers ===\n");
    
    /* Select window 4 */
    outw(io_base + 0x0E, CMD_SELECT_WINDOW(4));
    
    for (int reg = 0; reg < 8; reg++) {
        /* Construct MII read command */
        uint32_t cmd = 0x60000000 | (0 << 23) | (reg << 18);
        
        /* Write command */
        outw(io_base + 0x0800, cmd & 0xFFFF);
        outw(io_base + 0x0802, cmd >> 16);
        
        /* Wait for completion */
        for (int i = 0; i < 1000; i++) {
            if (!(inl(io_base + 0x0800) & 0x10000000)) {
                break;
            }
        }
        
        /* Read result */
        uint16_t val = inw(io_base + 0x0800);
        
        printf("  PHY Reg %d: 0x%04X", reg, val);
        
        switch (reg) {
            case 0: printf(" (Control)"); break;
            case 1: printf(" (Status)"); break;
            case 2: printf(" (PHY ID1)"); break;
            case 3: printf(" (PHY ID2)"); break;
            case 4: printf(" (Advertise)"); break;
            case 5: printf(" (Link Partner)"); break;
        }
        
        printf("\n");
    }
}

/* Full Register Dump */
static void full_dump(uint16_t io_base, int card_type) {
    const char *card_name = (card_type == 1) ? "3C509B" : 
                           (card_type == 2) ? "3C515-TX" : "Unknown";
    
    printf("\n");
    printf("=====================================\n");
    printf(" 3Com %s Register Dump\n", card_name);
    printf(" I/O Base: 0x%03X\n", io_base);
    printf("=====================================\n");
    
    /* Dump status first */
    dump_status(io_base);
    
    /* Dump EEPROM */
    dump_eeprom(io_base, card_type == 2);
    
    /* Dump all windows */
    for (int win = 0; win < 8; win++) {
        if (win == 5) continue;  /* Skip reserved window */
        dump_window(io_base, win);
    }
    
    /* 3C515-specific dumps */
    if (card_type == 2) {
        dump_3c515_dma(io_base);
        dump_mii_phy(io_base);
    }
    
    /* Restore window 1 (operational) */
    outw(io_base + 0x0E, CMD_SELECT_WINDOW(1));
}

/* Export to file */
static void export_dump(uint16_t io_base, int card_type, const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        printf("Failed to create output file\n");
        return;
    }
    
    /* Redirect stdout to file */
    FILE *old_stdout = stdout;
    stdout = fp;
    
    /* Perform full dump */
    full_dump(io_base, card_type);
    
    /* Restore stdout */
    stdout = old_stdout;
    fclose(fp);
    
    printf("Register dump saved to %s\n", filename);
}

/* Main */
int main(int argc, char *argv[]) {
    uint16_t io_base = 0x300;  /* Default */
    int card_type;
    
    printf("3Com NIC Register Dump Utility\n");
    printf("==============================\n\n");
    
    /* Parse command line */
    if (argc > 1) {
        if (sscanf(argv[1], "%x", &io_base) != 1) {
            printf("Usage: %s [io_base_hex] [output_file]\n", argv[0]);
            printf("Example: %s 300 dump.txt\n", argv[0]);
            return 1;
        }
    }
    
    printf("Probing for 3Com NIC at 0x%03X...\n", io_base);
    
    /* Detect card type */
    card_type = detect_3com_card(io_base);
    
    if (card_type == 0) {
        printf("No 3Com NIC detected at 0x%03X\n", io_base);
        printf("\nTry common I/O addresses: 0x300, 0x310, 0x320, 0x330\n");
        return 1;
    }
    
    const char *card_name = (card_type == 1) ? "3C509B" : "3C515-TX";
    printf("Detected: %s\n", card_name);
    
    /* Save or display dump */
    if (argc > 2) {
        export_dump(io_base, card_type, argv[2]);
    } else {
        full_dump(io_base, card_type);
    }
    
    printf("\nPress any key to exit...\n");
    getch();
    
    return 0;
}
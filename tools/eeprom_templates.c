/**
 * EEPROM Template Generator for 3Com NICs
 * 
 * Generates EEPROM content templates for 3C509B and 3C515-TX NICs.
 * These templates are essential for QEMU emulation to provide valid
 * hardware configuration data.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* EEPROM Size Constants */
#define EEPROM_SIZE_3C509B  64  /* 64 words */
#define EEPROM_SIZE_3C515   64  /* 64 words */

/* EEPROM Address Map - Common */
#define EEPROM_NODE_ADDR_0      0x00  /* MAC bytes 0-1 */
#define EEPROM_NODE_ADDR_1      0x01  /* MAC bytes 2-3 */
#define EEPROM_NODE_ADDR_2      0x02  /* MAC bytes 4-5 */
#define EEPROM_PRODUCT_ID       0x03  /* Product ID */
#define EEPROM_MFG_DATE         0x08  /* Manufacturing date */
#define EEPROM_OEM_NODE_0       0x0A  /* OEM MAC 0-1 */
#define EEPROM_OEM_NODE_1       0x0B  /* OEM MAC 2-3 */
#define EEPROM_OEM_NODE_2       0x0C  /* OEM MAC 4-5 */
#define EEPROM_SOFTWARE_CONFIG  0x0D  /* Software configuration */
#define EEPROM_CHECKSUM         0x0F  /* Configuration checksum */
#define EEPROM_SW_INFO          0x14  /* Software information */
#define EEPROM_3COM_ID          0x07  /* 3Com identifier */

/* Product IDs */
#define PRODUCT_ID_3C509B       0x6D50
#define PRODUCT_ID_3C515TX      0x5051
#define COMPANY_ID_3COM         0x6D50

/* Configuration Bits */
#define CONFIG_10BASE_T         (0 << 14)
#define CONFIG_AUI              (1 << 14)
#define CONFIG_10BASE_2         (2 << 14)
#define CONFIG_AUTO_SELECT      (3 << 14)
#define CONFIG_100BASE_TX       (4 << 14)

/* EEPROM Template Structure */
typedef struct {
    uint16_t data[64];
    char description[64][64];
    uint8_t valid_mask[64];  /* Which addresses are valid */
} eeprom_template_t;

/* Generate MAC Address */
static void generate_mac(uint8_t mac[6], int sequential_id) {
    /* 3Com OUI: 00:50:04 */
    mac[0] = 0x00;
    mac[1] = 0x50;
    mac[2] = 0x04;
    
    /* Sequential last 3 bytes for testing */
    mac[3] = (sequential_id >> 16) & 0xFF;
    mac[4] = (sequential_id >> 8) & 0xFF;
    mac[5] = sequential_id & 0xFF;
}

/* Calculate EEPROM Checksum */
static uint16_t calculate_checksum(uint16_t *data, int start, int end) {
    uint32_t sum = 0;
    
    for (int i = start; i <= end; i++) {
        sum += data[i];
    }
    
    /* Return complement for zero sum */
    return (uint16_t)(0x10000 - (sum & 0xFFFF));
}

/* Initialize 3C509B EEPROM Template */
static void init_3c509b_template(eeprom_template_t *tmpl, uint8_t mac[6], int config) {
    memset(tmpl, 0, sizeof(eeprom_template_t));
    
    /* MAC Address */
    tmpl->data[0x00] = (mac[1] << 8) | mac[0];
    strcpy(tmpl->description[0x00], "MAC Address 0-1");
    tmpl->valid_mask[0x00] = 1;
    
    tmpl->data[0x01] = (mac[3] << 8) | mac[2];
    strcpy(tmpl->description[0x01], "MAC Address 2-3");
    tmpl->valid_mask[0x01] = 1;
    
    tmpl->data[0x02] = (mac[5] << 8) | mac[4];
    strcpy(tmpl->description[0x02], "MAC Address 4-5");
    tmpl->valid_mask[0x02] = 1;
    
    /* Product ID */
    tmpl->data[0x03] = PRODUCT_ID_3C509B;
    strcpy(tmpl->description[0x03], "Product ID (3C509B)");
    tmpl->valid_mask[0x03] = 1;
    
    /* Configuration */
    tmpl->data[0x04] = config | 0x0001;  /* Enable adapter */
    strcpy(tmpl->description[0x04], "Configuration");
    tmpl->valid_mask[0x04] = 1;
    
    /* I/O Base Address */
    tmpl->data[0x05] = 0x0300;  /* Default 0x300 */
    strcpy(tmpl->description[0x05], "I/O Base Address");
    tmpl->valid_mask[0x05] = 1;
    
    /* IRQ Configuration */
    tmpl->data[0x06] = 0x0A00;  /* IRQ 10 */
    strcpy(tmpl->description[0x06], "IRQ Configuration");
    tmpl->valid_mask[0x06] = 1;
    
    /* 3Com ID */
    tmpl->data[0x07] = COMPANY_ID_3COM;
    strcpy(tmpl->description[0x07], "3Com Company ID");
    tmpl->valid_mask[0x07] = 1;
    
    /* Manufacturing Date (current date) */
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    tmpl->data[0x08] = ((tm->tm_year - 100) << 9) | ((tm->tm_mon + 1) << 5) | tm->tm_mday;
    strcpy(tmpl->description[0x08], "Manufacturing Date");
    tmpl->valid_mask[0x08] = 1;
    
    /* OEM Node Address (same as primary) */
    tmpl->data[0x0A] = tmpl->data[0x00];
    strcpy(tmpl->description[0x0A], "OEM MAC 0-1");
    tmpl->valid_mask[0x0A] = 1;
    
    tmpl->data[0x0B] = tmpl->data[0x01];
    strcpy(tmpl->description[0x0B], "OEM MAC 2-3");
    tmpl->valid_mask[0x0B] = 1;
    
    tmpl->data[0x0C] = tmpl->data[0x02];
    strcpy(tmpl->description[0x0C], "OEM MAC 4-5");
    tmpl->valid_mask[0x0C] = 1;
    
    /* Software Configuration */
    tmpl->data[0x0D] = 0x0080;  /* Default config */
    strcpy(tmpl->description[0x0D], "Software Configuration");
    tmpl->valid_mask[0x0D] = 1;
    
    /* Capabilities */
    tmpl->data[0x0E] = 0x0040;  /* Support full duplex */
    strcpy(tmpl->description[0x0E], "Capabilities");
    tmpl->valid_mask[0x0E] = 1;
    
    /* Checksum */
    tmpl->data[0x0F] = calculate_checksum(tmpl->data, 0, 14);
    strcpy(tmpl->description[0x0F], "Configuration Checksum");
    tmpl->valid_mask[0x0F] = 1;
    
    /* Software Information */
    tmpl->data[0x14] = 0x1234;  /* Version info */
    strcpy(tmpl->description[0x14], "Software Information");
    tmpl->valid_mask[0x14] = 1;
    
    /* Internal Config */
    tmpl->data[0x15] = 0x0000;  /* Default */
    strcpy(tmpl->description[0x15], "Internal Configuration");
    tmpl->valid_mask[0x15] = 1;
}

/* Initialize 3C515-TX EEPROM Template */
static void init_3c515_template(eeprom_template_t *tmpl, uint8_t mac[6], int config) {
    memset(tmpl, 0, sizeof(eeprom_template_t));
    
    /* MAC Address */
    tmpl->data[0x00] = (mac[1] << 8) | mac[0];
    strcpy(tmpl->description[0x00], "MAC Address 0-1");
    tmpl->valid_mask[0x00] = 1;
    
    tmpl->data[0x01] = (mac[3] << 8) | mac[2];
    strcpy(tmpl->description[0x01], "MAC Address 2-3");
    tmpl->valid_mask[0x01] = 1;
    
    tmpl->data[0x02] = (mac[5] << 8) | mac[4];
    strcpy(tmpl->description[0x02], "MAC Address 4-5");
    tmpl->valid_mask[0x02] = 1;
    
    /* Product ID */
    tmpl->data[0x03] = PRODUCT_ID_3C515TX;
    strcpy(tmpl->description[0x03], "Product ID (3C515-TX)");
    tmpl->valid_mask[0x03] = 1;
    
    /* Configuration - 100BaseTX with bus master */
    tmpl->data[0x04] = CONFIG_100BASE_TX | 0x0021;  /* Enable adapter + bus master */
    strcpy(tmpl->description[0x04], "Configuration");
    tmpl->valid_mask[0x04] = 1;
    
    /* I/O Base Address */
    tmpl->data[0x05] = 0x0300;  /* Default 0x300 */
    strcpy(tmpl->description[0x05], "I/O Base Address");
    tmpl->valid_mask[0x05] = 1;
    
    /* IRQ Configuration */
    tmpl->data[0x06] = 0x0B00;  /* IRQ 11 */
    strcpy(tmpl->description[0x06], "IRQ Configuration");
    tmpl->valid_mask[0x06] = 1;
    
    /* 3Com ID */
    tmpl->data[0x07] = COMPANY_ID_3COM;
    strcpy(tmpl->description[0x07], "3Com Company ID");
    tmpl->valid_mask[0x07] = 1;
    
    /* Manufacturing Date */
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    tmpl->data[0x08] = ((tm->tm_year - 100) << 9) | ((tm->tm_mon + 1) << 5) | tm->tm_mday;
    strcpy(tmpl->description[0x08], "Manufacturing Date");
    tmpl->valid_mask[0x08] = 1;
    
    /* Subsystem Vendor ID */
    tmpl->data[0x09] = 0x10B7;  /* 3Com */
    strcpy(tmpl->description[0x09], "Subsystem Vendor ID");
    tmpl->valid_mask[0x09] = 1;
    
    /* OEM Node Address */
    tmpl->data[0x0A] = tmpl->data[0x00];
    strcpy(tmpl->description[0x0A], "OEM MAC 0-1");
    tmpl->valid_mask[0x0A] = 1;
    
    tmpl->data[0x0B] = tmpl->data[0x01];
    strcpy(tmpl->description[0x0B], "OEM MAC 2-3");
    tmpl->valid_mask[0x0B] = 1;
    
    tmpl->data[0x0C] = tmpl->data[0x02];
    strcpy(tmpl->description[0x0C], "OEM MAC 4-5");
    tmpl->valid_mask[0x0C] = 1;
    
    /* Software Configuration */
    tmpl->data[0x0D] = 0x00C0;  /* Enable MII, auto-negotiation */
    strcpy(tmpl->description[0x0D], "Software Configuration");
    tmpl->valid_mask[0x0D] = 1;
    
    /* Capabilities */
    tmpl->data[0x0E] = 0x00E0;  /* Full duplex, 100Mbps, bus master */
    strcpy(tmpl->description[0x0E], "Capabilities");
    tmpl->valid_mask[0x0E] = 1;
    
    /* Checksum */
    tmpl->data[0x0F] = calculate_checksum(tmpl->data, 0, 14);
    strcpy(tmpl->description[0x0F], "Configuration Checksum");
    tmpl->valid_mask[0x0F] = 1;
    
    /* Software Information */
    tmpl->data[0x14] = 0x5678;  /* Version info */
    strcpy(tmpl->description[0x14], "Software Information");
    tmpl->valid_mask[0x14] = 1;
    
    /* PHY Configuration */
    tmpl->data[0x15] = 0x0001;  /* PHY address 0 */
    strcpy(tmpl->description[0x15], "PHY Configuration");
    tmpl->valid_mask[0x15] = 1;
    
    /* DMA Configuration */
    tmpl->data[0x16] = 0x0010;  /* 16 descriptors */
    strcpy(tmpl->description[0x16], "DMA Configuration");
    tmpl->valid_mask[0x16] = 1;
}

/* Export EEPROM to Binary File */
static void export_binary(const eeprom_template_t *tmpl, const char *filename) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("Failed to create binary file");
        return;
    }
    
    /* Write all 64 words (128 bytes) */
    for (int i = 0; i < 64; i++) {
        /* Little-endian byte order */
        uint8_t bytes[2];
        bytes[0] = tmpl->data[i] & 0xFF;
        bytes[1] = (tmpl->data[i] >> 8) & 0xFF;
        fwrite(bytes, 1, 2, fp);
    }
    
    fclose(fp);
    printf("Exported binary EEPROM to %s (128 bytes)\n", filename);
}

/* Export EEPROM to C Header */
static void export_c_header(const eeprom_template_t *tmpl, const char *filename, const char *array_name) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("Failed to create header file");
        return;
    }
    
    fprintf(fp, "/* Auto-generated EEPROM template */\n\n");
    fprintf(fp, "#ifndef EEPROM_TEMPLATE_H\n");
    fprintf(fp, "#define EEPROM_TEMPLATE_H\n\n");
    fprintf(fp, "#include <stdint.h>\n\n");
    
    /* Export as word array */
    fprintf(fp, "static const uint16_t %s_words[64] = {\n", array_name);
    for (int i = 0; i < 64; i++) {
        if (tmpl->valid_mask[i]) {
            fprintf(fp, "    0x%04X,  /* [0x%02X] %s */\n", 
                    tmpl->data[i], i, tmpl->description[i]);
        } else {
            fprintf(fp, "    0x0000,  /* [0x%02X] Reserved */\n", i);
        }
    }
    fprintf(fp, "};\n\n");
    
    /* Export as byte array */
    fprintf(fp, "static const uint8_t %s_bytes[128] = {\n", array_name);
    for (int i = 0; i < 64; i++) {
        fprintf(fp, "    0x%02X, 0x%02X,  /* [0x%02X] ",
                tmpl->data[i] & 0xFF, (tmpl->data[i] >> 8) & 0xFF, i);
        
        if (tmpl->valid_mask[i]) {
            fprintf(fp, "%s */\n", tmpl->description[i]);
        } else {
            fprintf(fp, "Reserved */\n");
        }
    }
    fprintf(fp, "};\n\n");
    
    fprintf(fp, "#endif /* EEPROM_TEMPLATE_H */\n");
    fclose(fp);
    
    printf("Exported C header to %s\n", filename);
}

/* Export EEPROM to Intel HEX Format */
static void export_intel_hex(const eeprom_template_t *tmpl, const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("Failed to create hex file");
        return;
    }
    
    /* Write data records (16 bytes per line) */
    for (int addr = 0; addr < 128; addr += 16) {
        uint8_t checksum = 0;
        
        fprintf(fp, ":10%04X00", addr);  /* Record header */
        checksum += 0x10;  /* Byte count */
        checksum += (addr >> 8) & 0xFF;
        checksum += addr & 0xFF;
        checksum += 0x00;  /* Record type */
        
        /* Write 16 bytes of data */
        for (int i = 0; i < 16 && (addr + i) < 128; i++) {
            int word_idx = (addr + i) / 2;
            uint8_t byte_val;
            
            if ((addr + i) & 1) {
                byte_val = (tmpl->data[word_idx] >> 8) & 0xFF;
            } else {
                byte_val = tmpl->data[word_idx] & 0xFF;
            }
            
            fprintf(fp, "%02X", byte_val);
            checksum += byte_val;
        }
        
        /* Write checksum */
        fprintf(fp, "%02X\n", (uint8_t)(0x100 - checksum));
    }
    
    /* End of file record */
    fprintf(fp, ":00000001FF\n");
    fclose(fp);
    
    printf("Exported Intel HEX to %s\n", filename);
}

/* Print EEPROM Contents */
static void print_eeprom(const eeprom_template_t *tmpl, const char *title) {
    printf("\n=== %s ===\n", title);
    printf("Addr | Data  | Description\n");
    printf("-----|-------|---------------------------\n");
    
    for (int i = 0; i < 64; i++) {
        if (tmpl->valid_mask[i]) {
            printf("0x%02X | 0x%04X | %s\n", i, tmpl->data[i], tmpl->description[i]);
        }
    }
    
    /* Print MAC address */
    printf("\nMAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           tmpl->data[0] & 0xFF, (tmpl->data[0] >> 8) & 0xFF,
           tmpl->data[1] & 0xFF, (tmpl->data[1] >> 8) & 0xFF,
           tmpl->data[2] & 0xFF, (tmpl->data[2] >> 8) & 0xFF);
}

/* Generate Multiple Templates */
static void generate_templates(void) {
    eeprom_template_t tmpl;
    uint8_t mac[6];
    
    /* Generate 3C509B Templates */
    printf("\nGenerating 3C509B EEPROM Templates...\n");
    
    /* 10BaseT configuration */
    generate_mac(mac, 0x010203);
    init_3c509b_template(&tmpl, mac, CONFIG_10BASE_T);
    print_eeprom(&tmpl, "3C509B 10BaseT Template");
    export_binary(&tmpl, "eeprom_3c509b_10baset.bin");
    export_c_header(&tmpl, "eeprom_3c509b_10baset.h", "eeprom_3c509b_10baset");
    export_intel_hex(&tmpl, "eeprom_3c509b_10baset.hex");
    
    /* BNC (10Base2) configuration */
    generate_mac(mac, 0x020304);
    init_3c509b_template(&tmpl, mac, CONFIG_10BASE_2);
    print_eeprom(&tmpl, "3C509B 10Base2 (BNC) Template");
    export_binary(&tmpl, "eeprom_3c509b_bnc.bin");
    
    /* Auto-select configuration */
    generate_mac(mac, 0x030405);
    init_3c509b_template(&tmpl, mac, CONFIG_AUTO_SELECT);
    print_eeprom(&tmpl, "3C509B Auto-Select Template");
    export_binary(&tmpl, "eeprom_3c509b_auto.bin");
    
    /* Generate 3C515-TX Templates */
    printf("\nGenerating 3C515-TX EEPROM Templates...\n");
    
    /* 100BaseTX configuration */
    generate_mac(mac, 0x040506);
    init_3c515_template(&tmpl, mac, CONFIG_100BASE_TX);
    print_eeprom(&tmpl, "3C515-TX 100BaseTX Template");
    export_binary(&tmpl, "eeprom_3c515_100basetx.bin");
    export_c_header(&tmpl, "eeprom_3c515_100basetx.h", "eeprom_3c515_100basetx");
    export_intel_hex(&tmpl, "eeprom_3c515_100basetx.hex");
    
    /* Auto-negotiation configuration */
    generate_mac(mac, 0x050607);
    init_3c515_template(&tmpl, mac, CONFIG_AUTO_SELECT);
    print_eeprom(&tmpl, "3C515-TX Auto-Negotiation Template");
    export_binary(&tmpl, "eeprom_3c515_auto.bin");
}

/* Main */
int main(int argc, char *argv[]) {
    printf("3Com EEPROM Template Generator\n");
    printf("==============================\n");
    
    if (argc > 1 && strcmp(argv[1], "-custom") == 0) {
        /* Custom MAC address mode */
        if (argc < 8) {
            printf("Usage: %s -custom <mac0> <mac1> <mac2> <mac3> <mac4> <mac5>\n", argv[0]);
            printf("Example: %s -custom 00 50 04 01 02 03\n", argv[0]);
            return 1;
        }
        
        uint8_t mac[6];
        for (int i = 0; i < 6; i++) {
            mac[i] = (uint8_t)strtol(argv[i + 2], NULL, 16);
        }
        
        printf("Generating custom EEPROM with MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        
        eeprom_template_t tmpl;
        init_3c509b_template(&tmpl, mac, CONFIG_AUTO_SELECT);
        export_binary(&tmpl, "eeprom_custom.bin");
        export_c_header(&tmpl, "eeprom_custom.h", "eeprom_custom");
        
    } else {
        /* Generate standard templates */
        generate_templates();
    }
    
    printf("\nEEPROM templates generated successfully!\n");
    printf("\nFiles created:\n");
    printf("  *.bin - Binary EEPROM images (128 bytes)\n");
    printf("  *.h   - C header files with arrays\n");
    printf("  *.hex - Intel HEX format files\n");
    
    return 0;
}
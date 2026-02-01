/**
 * @file pci_bios.c
 * @brief PCI BIOS INT 1Ah wrapper functions for DOS real mode
 * 
 * Provides real-mode safe PCI configuration space access using INT 1Ah
 * BIOS services. This allows the existing BOOMTEX PCI enumeration logic
 * to work in DOS without requiring protected mode or direct I/O access.
 * 
 * Based on PCI BIOS Specification 2.1 and Ralf Brown's Interrupt List
 */

#include <dos.h>
#include <stdint.h>
#include <stdbool.h>
#include "pci_bios.h"
#include "logging.h"

/* PCI Configuration Mechanism #1 registers */
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

/* Fallback mode detection */
static bool mechanism_1_available = false;
static bool mechanism_1_checked = false;

/* PCI BIOS function codes for INT 1Ah */
#define PCI_FUNCTION_ID         0xB1
#define PCI_BIOS_PRESENT        0x01
#define PCI_FIND_DEVICE         0x02
#define PCI_FIND_CLASS          0x03
#define PCI_READ_CONFIG_BYTE    0x08
#define PCI_READ_CONFIG_WORD    0x09
#define PCI_READ_CONFIG_DWORD   0x0A
#define PCI_WRITE_CONFIG_BYTE   0x0B
#define PCI_WRITE_CONFIG_WORD   0x0C
#define PCI_WRITE_CONFIG_DWORD  0x0D
#define PCI_GET_IRQ_ROUTING     0x0E
#define PCI_SET_IRQ             0x0F

/* PCI BIOS return codes */
#define PCI_SUCCESSFUL          0x00
#define PCI_FUNC_NOT_SUPPORTED  0x81
#define PCI_BAD_VENDOR_ID       0x83
#define PCI_DEVICE_NOT_FOUND    0x86
#define PCI_BAD_REGISTER_NUMBER 0x87
#define PCI_SET_FAILED          0x88
#define PCI_BUFFER_TOO_SMALL    0x89

/* Static storage for PCI BIOS information */
static struct {
    bool present;
    uint8_t major_version;
    uint8_t minor_version;
    uint8_t last_bus;
    uint8_t hardware_mechanism;
} pci_bios_info = {0};

#ifdef __WATCOMC__
/*
 * Watcom C helpers for 32-bit register access in 16-bit mode.
 * The standard REGS union doesn't have ecx/edx fields in 16-bit mode,
 * so we need assembly helpers with operand size override prefix (0x66).
 */

/* Get 32-bit value from EDX register after an interrupt call */
uint32_t pci_get_edx_32(void);
#pragma aux pci_get_edx_32 = \
    ".386"                   \
    "mov eax, edx"           \
    "mov edx, eax"           \
    "shr edx, 16"            \
    value [ax dx]            \
    modify exact [ax dx];

/* Get 32-bit value from ECX register after an interrupt call */
uint32_t pci_get_ecx_32(void);
#pragma aux pci_get_ecx_32 = \
    ".386"                   \
    "mov eax, ecx"           \
    "mov edx, eax"           \
    "shr edx, 16"            \
    value [ax dx]            \
    modify exact [ax dx];

/* Set 32-bit ECX register before an interrupt call */
void pci_set_ecx_32(uint32_t value);
#pragma aux pci_set_ecx_32 = \
    ".386"                   \
    "shl edx, 16"            \
    "mov dx, ax"             \
    "mov ecx, edx"           \
    parm [ax dx]             \
    modify exact [cx];

/*
 * Extended INT 1Ah call for PCI BIOS with 32-bit register support.
 * This wrapper handles setting/getting 32-bit values for ECX and EDX.
 */
typedef struct {
    uint16_t ax_in;
    uint16_t bx_in;
    uint32_t ecx_in;
    uint16_t di_in;
    uint16_t si_in;
    uint16_t ax_out;
    uint16_t bx_out;
    uint32_t ecx_out;
    uint32_t edx_out;
    uint16_t cflag_out;
} pci_int1a_data_t;

/* Perform PCI BIOS INT 1Ah call with 32-bit ECX/EDX support */
void pci_int1a_call(pci_int1a_data_t far* data);
#pragma aux pci_int1a_call = \
    ".386"                   \
    "push ds"                \
    "push si"                \
    "push bp"                \
    "mov bp, ax"             \
    "mov ds, dx"             \
    "mov ax, [bp]"           \
    "mov bx, [bp+2]"         \
    "mov ecx, [bp+4]"        \
    "mov di, [bp+8]"         \
    "mov si, [bp+10]"        \
    "int 1Ah"                \
    "pushf"                  \
    "mov [bp+12], ax"        \
    "mov [bp+14], bx"        \
    "mov [bp+16], ecx"       \
    "mov [bp+20], edx"       \
    "pop ax"                 \
    "and ax, 1"              \
    "mov [bp+24], ax"        \
    "pop bp"                 \
    "pop si"                 \
    "pop ds"                 \
    parm [dx ax]             \
    modify [ax bx cx dx di si];

#endif /* __WATCOMC__ */

/**
 * @brief Check if PCI BIOS is present
 *
 * Uses INT 1Ah, AX=B101h to detect PCI BIOS presence and capabilities
 *
 * @return true if PCI BIOS is present, false otherwise
 */
bool pci_bios_present(void) {
#ifdef __WATCOMC__
    pci_int1a_data_t pci_data;

    /* Check if we've already detected the BIOS */
    if (pci_bios_info.present) {
        return true;
    }

    /* Clear structure */
    memset(&pci_data, 0, sizeof(pci_data));

    /* PCI BIOS Installation Check */
    pci_data.ax_in = (PCI_FUNCTION_ID << 8) | PCI_BIOS_PRESENT;
    pci_data.di_in = 0;  /* Clear EDI for PM entry point (we don't use it) */

    pci_int1a_call(&pci_data);

    /* Check if successful - EDX should contain 'PCI ' (0x20494350) */
    if ((pci_data.cflag_out == 0) && (pci_data.edx_out == 0x20494350)) {
        pci_bios_info.present = true;
        pci_bios_info.major_version = (pci_data.bx_out >> 8) & 0xFF;
        pci_bios_info.minor_version = pci_data.bx_out & 0xFF;
        pci_bios_info.last_bus = pci_data.ecx_out & 0xFF;
        pci_bios_info.hardware_mechanism = pci_data.ax_out & 0xFF;

        LOG_INFO("PCI BIOS v%d.%d detected, last bus=%d, mechanisms=0x%02X",
                 pci_bios_info.major_version, pci_bios_info.minor_version,
                 pci_bios_info.last_bus, pci_bios_info.hardware_mechanism);

        return true;
    }

    LOG_DEBUG("PCI BIOS not detected");
    return false;
#else
    union REGS regs;
    struct SREGS sregs;

    /* Check if we've already detected the BIOS */
    if (pci_bios_info.present) {
        return true;
    }

    /* Clear registers */
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));

    /* PCI BIOS Installation Check */
    regs.h.ah = PCI_FUNCTION_ID;
    regs.h.al = PCI_BIOS_PRESENT;
    regs.x.di = 0;  /* Clear EDI for PM entry point (we don't use it) */

    int86x(0x1A, &regs, &regs, &sregs);

    /* Check if successful */
    if ((regs.x.cflag == 0) && (regs.x.edx == 0x20494350)) { /* 'PCI ' */
        pci_bios_info.present = true;
        pci_bios_info.major_version = regs.h.bh;
        pci_bios_info.minor_version = regs.h.bl;
        pci_bios_info.last_bus = regs.h.cl;
        pci_bios_info.hardware_mechanism = regs.h.al;

        LOG_INFO("PCI BIOS v%d.%d detected, last bus=%d, mechanisms=0x%02X",
                 pci_bios_info.major_version, pci_bios_info.minor_version,
                 pci_bios_info.last_bus, pci_bios_info.hardware_mechanism);

        return true;
    }

    LOG_DEBUG("PCI BIOS not detected");
    return false;
#endif
}

/**
 * @brief Check if PCI Configuration Mechanism #1 is available
 * 
 * @return true if available, false if not
 */
static bool check_pci_mechanism_1(void) {
    uint32_t temp;
    
    if (mechanism_1_checked) {
        return mechanism_1_available;
    }
    
    mechanism_1_checked = true;
    
    LOG_DEBUG("Checking for PCI Configuration Mechanism #1");
    
    /* Save current CONFIG_ADDRESS value */
    temp = inpd(PCI_CONFIG_ADDRESS);
    
    /* Write test pattern and verify it can be read back */
    outpd(PCI_CONFIG_ADDRESS, 0x80000000);  /* Enable bit + test pattern */
    if (inpd(PCI_CONFIG_ADDRESS) != 0x80000000) {
        LOG_DEBUG("PCI Mechanism #1 not available (CONFIG_ADDRESS test failed)");
        outpd(PCI_CONFIG_ADDRESS, temp);  /* Restore */
        return false;
    }
    
    /* Test with different pattern */
    outpd(PCI_CONFIG_ADDRESS, 0x80000004);
    if (inpd(PCI_CONFIG_ADDRESS) != 0x80000004) {
        LOG_DEBUG("PCI Mechanism #1 not available (pattern test failed)");
        outpd(PCI_CONFIG_ADDRESS, temp);  /* Restore */
        return false;
    }
    
    /* Restore original value */
    outpd(PCI_CONFIG_ADDRESS, temp);
    
    mechanism_1_available = true;
    LOG_INFO("PCI Configuration Mechanism #1 available as fallback");
    return true;
}

/**
 * @brief Build PCI configuration address for Mechanism #1
 */
static uint32_t pci_build_config_addr(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return 0x80000000 |  /* Enable bit */
           ((uint32_t)bus << 16) |
           ((uint32_t)(device & 0x1F) << 11) |
           ((uint32_t)(function & 0x07) << 8) |
           (offset & 0xFC);  /* Align to 4-byte boundary */
}

/**
 * @brief Read PCI config byte using Mechanism #1
 */
static uint8_t mechanism_1_read_config_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t addr;
    uint8_t value;
    
    addr = pci_build_config_addr(bus, device, function, offset);
    
    _disable();  /* Critical section */
    outpd(PCI_CONFIG_ADDRESS, addr);
    value = inp(PCI_CONFIG_DATA + (offset & 3));
    _enable();
    
    return value;
}

/**
 * @brief Write PCI config byte using Mechanism #1
 */
static bool mechanism_1_write_config_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint8_t value) {
    uint32_t addr;
    
    addr = pci_build_config_addr(bus, device, function, offset);
    
    _disable();  /* Critical section */
    outpd(PCI_CONFIG_ADDRESS, addr);
    outp(PCI_CONFIG_DATA + (offset & 3), value);
    _enable();
    
    return true;  /* Mechanism #1 doesn't provide error feedback */
}

/**
 * @brief Read PCI config word using Mechanism #1
 */
static uint16_t mechanism_1_read_config_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t addr;
    uint16_t value;
    
    if (offset & 1) {
        LOG_WARNING("Unaligned word read at offset 0x%02X", offset);
        return 0xFFFF;
    }
    
    addr = pci_build_config_addr(bus, device, function, offset);
    
    _disable();  /* Critical section */
    outpd(PCI_CONFIG_ADDRESS, addr);
    value = inpw(PCI_CONFIG_DATA + (offset & 2));
    _enable();
    
    return value;
}

/**
 * @brief Write PCI config word using Mechanism #1
 */
static bool mechanism_1_write_config_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value) {
    uint32_t addr;
    
    if (offset & 1) {
        LOG_WARNING("Unaligned word write at offset 0x%02X", offset);
        return false;
    }
    
    addr = pci_build_config_addr(bus, device, function, offset);
    
    _disable();  /* Critical section */
    outpd(PCI_CONFIG_ADDRESS, addr);
    outpw(PCI_CONFIG_DATA + (offset & 2), value);
    _enable();
    
    return true;
}

/**
 * @brief Get last PCI bus number
 * 
 * @return Last bus number (0-255) or 0 if PCI BIOS not present
 */
uint8_t pci_get_last_bus(void) {
    if (!pci_bios_present()) {
        return 0;
    }
    return pci_bios_info.last_bus;
}

/**
 * @brief Read PCI configuration byte
 * 
 * @param bus Bus number (0-255)
 * @param device Device number (0-31)
 * @param function Function number (0-7)
 * @param offset Register offset (0-255)
 * @return Configuration byte value or 0xFF on error
 */
uint8_t pci_read_config_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    union REGS regs;
    struct SREGS sregs;
    
    if (!pci_bios_present()) {
        /* Try Mechanism #1 as fallback */
        if (check_pci_mechanism_1()) {
            LOG_DEBUG("Using PCI Mechanism #1 fallback for config read");
            return mechanism_1_read_config_byte(bus, device, function, offset);
        }
        return 0xFF;
    }
    
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.h.ah = PCI_FUNCTION_ID;
    regs.h.al = PCI_READ_CONFIG_BYTE;
    regs.h.bh = bus;
    regs.h.bl = (device << 3) | (function & 0x07);
    regs.x.di = offset;
    
    int86x(0x1A, &regs, &regs, &sregs);
    
    if (regs.x.cflag != 0) {
        return 0xFF;
    }
    
    return regs.h.cl;
}

/**
 * @brief Read PCI configuration word
 * 
 * @param bus Bus number (0-255)
 * @param device Device number (0-31)
 * @param function Function number (0-7)
 * @param offset Register offset (0-254, must be word-aligned)
 * @return Configuration word value or 0xFFFF on error
 */
uint16_t pci_read_config_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    union REGS regs;
    struct SREGS sregs;
    
    if (!pci_bios_present()) {
        return 0xFFFF;
    }
    
    /* Ensure word alignment */
    if (offset & 0x01) {
        LOG_WARNING("PCI config word read at unaligned offset 0x%02X", offset);
        return 0xFFFF;
    }
    
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.h.ah = PCI_FUNCTION_ID;
    regs.h.al = PCI_READ_CONFIG_WORD;
    regs.h.bh = bus;
    regs.h.bl = (device << 3) | (function & 0x07);
    regs.x.di = offset;
    
    int86x(0x1A, &regs, &regs, &sregs);
    
    if (regs.x.cflag != 0) {
        return 0xFFFF;
    }
    
    return regs.x.cx;
}

/**
 * @brief Read PCI configuration dword
 *
 * @param bus Bus number (0-255)
 * @param device Device number (0-31)
 * @param function Function number (0-7)
 * @param offset Register offset (0-252, must be dword-aligned)
 * @return Configuration dword value or 0xFFFFFFFF on error
 */
uint32_t pci_read_config_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
#ifdef __WATCOMC__
    pci_int1a_data_t pci_data;

    if (!pci_bios_present()) {
        return 0xFFFFFFFF;
    }

    /* Ensure dword alignment */
    if (offset & 0x03) {
        LOG_WARNING("PCI config dword read at unaligned offset 0x%02X", offset);
        return 0xFFFFFFFF;
    }

    memset(&pci_data, 0, sizeof(pci_data));

    pci_data.ax_in = (PCI_FUNCTION_ID << 8) | PCI_READ_CONFIG_DWORD;
    pci_data.bx_in = (bus << 8) | ((device << 3) | (function & 0x07));
    pci_data.di_in = offset;

    pci_int1a_call(&pci_data);

    if (pci_data.cflag_out != 0) {
        return 0xFFFFFFFF;
    }

    return pci_data.ecx_out;
#else
    union REGS regs;
    struct SREGS sregs;

    if (!pci_bios_present()) {
        return 0xFFFFFFFF;
    }

    /* Ensure dword alignment */
    if (offset & 0x03) {
        LOG_WARNING("PCI config dword read at unaligned offset 0x%02X", offset);
        return 0xFFFFFFFF;
    }

    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));

    regs.h.ah = PCI_FUNCTION_ID;
    regs.h.al = PCI_READ_CONFIG_DWORD;
    regs.h.bh = bus;
    regs.h.bl = (device << 3) | (function & 0x07);
    regs.x.di = offset;

    int86x(0x1A, &regs, &regs, &sregs);

    if (regs.x.cflag != 0) {
        return 0xFFFFFFFF;
    }

    return regs.x.ecx;
#endif
}

/**
 * @brief Write PCI configuration byte
 * 
 * @param bus Bus number (0-255)
 * @param device Device number (0-31)
 * @param function Function number (0-7)
 * @param offset Register offset (0-255)
 * @param value Value to write
 * @return true on success, false on error
 */
bool pci_write_config_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint8_t value) {
    union REGS regs;
    struct SREGS sregs;
    
    if (!pci_bios_present()) {
        /* Try Mechanism #1 as fallback */
        if (check_pci_mechanism_1()) {
            LOG_DEBUG("Using PCI Mechanism #1 fallback for config write");
            return mechanism_1_write_config_byte(bus, device, function, offset, value);
        }
        return false;
    }
    
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.h.ah = PCI_FUNCTION_ID;
    regs.h.al = PCI_WRITE_CONFIG_BYTE;
    regs.h.bh = bus;
    regs.h.bl = (device << 3) | (function & 0x07);
    regs.h.cl = value;
    regs.x.di = offset;
    
    int86x(0x1A, &regs, &regs, &sregs);
    
    return (regs.x.cflag == 0);
}

/**
 * @brief Write PCI configuration word
 * 
 * @param bus Bus number (0-255)
 * @param device Device number (0-31)
 * @param function Function number (0-7)
 * @param offset Register offset (0-254, must be word-aligned)
 * @param value Value to write
 * @return true on success, false on error
 */
bool pci_write_config_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value) {
    union REGS regs;
    struct SREGS sregs;
    
    if (!pci_bios_present()) {
        return false;
    }
    
    /* Ensure word alignment */
    if (offset & 0x01) {
        LOG_WARNING("PCI config word write at unaligned offset 0x%02X", offset);
        return false;
    }
    
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.h.ah = PCI_FUNCTION_ID;
    regs.h.al = PCI_WRITE_CONFIG_WORD;
    regs.h.bh = bus;
    regs.h.bl = (device << 3) | (function & 0x07);
    regs.x.cx = value;
    regs.x.di = offset;
    
    int86x(0x1A, &regs, &regs, &sregs);
    
    return (regs.x.cflag == 0);
}

/**
 * @brief Write PCI configuration dword
 *
 * @param bus Bus number (0-255)
 * @param device Device number (0-31)
 * @param function Function number (0-7)
 * @param offset Register offset (0-252, must be dword-aligned)
 * @param value Value to write
 * @return true on success, false on error
 */
bool pci_write_config_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
#ifdef __WATCOMC__
    pci_int1a_data_t pci_data;

    if (!pci_bios_present()) {
        return false;
    }

    /* Ensure dword alignment */
    if (offset & 0x03) {
        LOG_WARNING("PCI config dword write at unaligned offset 0x%02X", offset);
        return false;
    }

    memset(&pci_data, 0, sizeof(pci_data));

    pci_data.ax_in = (PCI_FUNCTION_ID << 8) | PCI_WRITE_CONFIG_DWORD;
    pci_data.bx_in = (bus << 8) | ((device << 3) | (function & 0x07));
    pci_data.ecx_in = value;
    pci_data.di_in = offset;

    pci_int1a_call(&pci_data);

    return (pci_data.cflag_out == 0);
#else
    union REGS regs;
    struct SREGS sregs;

    if (!pci_bios_present()) {
        return false;
    }

    /* Ensure dword alignment */
    if (offset & 0x03) {
        LOG_WARNING("PCI config dword write at unaligned offset 0x%02X", offset);
        return false;
    }

    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));

    regs.h.ah = PCI_FUNCTION_ID;
    regs.h.al = PCI_WRITE_CONFIG_DWORD;
    regs.h.bh = bus;
    regs.h.bl = (device << 3) | (function & 0x07);
    regs.x.ecx = value;
    regs.x.di = offset;

    int86x(0x1A, &regs, &regs, &sregs);

    return (regs.x.cflag == 0);
#endif
}

/**
 * @brief Find PCI device by vendor and device ID
 * 
 * @param vendor_id Vendor ID to search for
 * @param device_id Device ID to search for
 * @param index Device index (0 for first, 1 for second, etc.)
 * @param bus Output: Bus number where device was found
 * @param device Output: Device number where device was found
 * @param function Output: Function number where device was found
 * @return true if device found, false otherwise
 */
bool pci_find_device(uint16_t vendor_id, uint16_t device_id, uint16_t index,
                    uint8_t *bus, uint8_t *device, uint8_t *function) {
    union REGS regs;
    struct SREGS sregs;
    
    if (!pci_bios_present() || !bus || !device || !function) {
        return false;
    }
    
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.h.ah = PCI_FUNCTION_ID;
    regs.h.al = PCI_FIND_DEVICE;
    regs.x.cx = device_id;
    regs.x.dx = vendor_id;
    regs.x.si = index;
    
    int86x(0x1A, &regs, &regs, &sregs);
    
    if (regs.x.cflag != 0) {
        return false;
    }
    
    *bus = regs.h.bh;
    *device = (regs.h.bl >> 3) & 0x1F;
    *function = regs.h.bl & 0x07;
    
    return true;
}

/**
 * @brief Find PCI device by class code
 *
 * @param class_code 24-bit class code (class, subclass, prog-if)
 * @param index Device index (0 for first, 1 for second, etc.)
 * @param bus Output: Bus number where device was found
 * @param device Output: Device number where device was found
 * @param function Output: Function number where device was found
 * @return true if device found, false otherwise
 */
bool pci_find_class(uint32_t class_code, uint16_t index,
                   uint8_t *bus, uint8_t *device, uint8_t *function) {
#ifdef __WATCOMC__
    pci_int1a_data_t pci_data;

    if (!pci_bios_present() || !bus || !device || !function) {
        return false;
    }

    memset(&pci_data, 0, sizeof(pci_data));

    pci_data.ax_in = (PCI_FUNCTION_ID << 8) | PCI_FIND_CLASS;
    pci_data.ecx_in = class_code & 0x00FFFFFF;  /* Only lower 24 bits */
    pci_data.si_in = index;

    pci_int1a_call(&pci_data);

    if (pci_data.cflag_out != 0) {
        return false;
    }

    *bus = (pci_data.bx_out >> 8) & 0xFF;
    *device = ((pci_data.bx_out & 0xFF) >> 3) & 0x1F;
    *function = (pci_data.bx_out & 0xFF) & 0x07;

    return true;
#else
    union REGS regs;
    struct SREGS sregs;

    if (!pci_bios_present() || !bus || !device || !function) {
        return false;
    }

    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));

    regs.h.ah = PCI_FUNCTION_ID;
    regs.h.al = PCI_FIND_CLASS;
    regs.x.ecx = class_code & 0x00FFFFFF;  /* Only lower 24 bits */
    regs.x.si = index;

    int86x(0x1A, &regs, &regs, &sregs);

    if (regs.x.cflag != 0) {
        return false;
    }

    *bus = regs.h.bh;
    *device = (regs.h.bl >> 3) & 0x1F;
    *function = regs.h.bl & 0x07;

    return true;
#endif
}

/**
 * @brief Enable PCI device I/O and memory access
 * 
 * Sets the I/O Space Enable and Memory Space Enable bits in the Command register
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param enable_io Enable I/O space access
 * @param enable_memory Enable memory space access
 * @param enable_bus_master Enable bus mastering
 * @return true on success, false on error
 */
bool pci_enable_device(uint8_t bus, uint8_t device, uint8_t function,
                      bool enable_io, bool enable_memory, bool enable_bus_master) {
    uint16_t command;
    
    /* Read current command register */
    command = pci_read_config_word(bus, device, function, PCI_COMMAND);
    if (command == 0xFFFF) {
        return false;
    }
    
    /* Set enable bits */
    if (enable_io) {
        command |= 0x0001;  /* I/O Space Enable */
    }
    if (enable_memory) {
        command |= 0x0002;  /* Memory Space Enable */
    }
    if (enable_bus_master) {
        command |= 0x0004;  /* Bus Master Enable */
    }
    
    /* Write back command register */
    return pci_write_config_word(bus, device, function, PCI_COMMAND, command);
}

/**
 * @brief Read PCI BAR (Base Address Register)
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param bar_index BAR index (0-5)
 * @param bar_value Output: BAR value
 * @param is_io Output: true if I/O BAR, false if memory BAR
 * @return true on success, false on error
 */
bool pci_read_bar(uint8_t bus, uint8_t device, uint8_t function,
                 uint8_t bar_index, uint32_t *bar_value, bool *is_io) {
    uint32_t bar;
    uint8_t offset;
    
    if (bar_index > 5 || !bar_value || !is_io) {
        return false;
    }
    
    offset = PCI_BAR0 + (bar_index * 4);
    bar = pci_read_config_dword(bus, device, function, offset);
    
    if (bar == 0xFFFFFFFF) {
        return false;
    }
    
    *is_io = (bar & 0x01) ? true : false;
    
    if (*is_io) {
        /* I/O BAR - mask off flags */
        *bar_value = bar & 0xFFFFFFFC;
    } else {
        /* Memory BAR - mask off flags */
        *bar_value = bar & 0xFFFFFFF0;
    }
    
    return true;
}

/**
 * @brief Get PCI interrupt line
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @return IRQ number (0-15) or 0xFF on error
 */
uint8_t pci_get_irq(uint8_t bus, uint8_t device, uint8_t function) {
    return pci_read_config_byte(bus, device, function, PCI_INTERRUPT_LINE);
}

/**
 * @brief Set PCI Command register bits
 * 
 * Explicitly enables I/O, Memory, and Bus Master as needed.
 * Critical for ensuring device is properly configured.
 * 
 * @param bus Bus number
 * @param device Device number  
 * @param function Function number
 * @param bits Command bits to set (PCI_CMD_IO, PCI_CMD_MEMORY, PCI_CMD_MASTER)
 * @return true on success, false on error
 */
bool pci_set_command_bits(uint8_t bus, uint8_t device, uint8_t function, uint16_t bits) {
    uint16_t command;
    
    command = pci_read_config_word(bus, device, function, PCI_COMMAND);
    if (command == 0xFFFF) {
        LOG_ERROR("Failed to read PCI command register");
        return false;
    }
    
    /* Set requested bits */
    command |= bits;
    
    /* Also set parity error response and SERR for production */
    command |= PCI_CMD_PARITY | PCI_CMD_SERR;
    
    if (!pci_write_config_word(bus, device, function, PCI_COMMAND, command)) {
        LOG_ERROR("Failed to write PCI command register");
        return false;
    }
    
    LOG_DEBUG("PCI Command set to 0x%04X for %02X:%02X.%X", 
              command, bus, device, function);
    
    return true;
}

/**
 * @brief Clear PCI Status register error bits
 * 
 * Clears stale error conditions like Master Abort, Target Abort, Parity Error.
 * Write-1-to-clear semantics.
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @return true on success, false on error
 */
bool pci_clear_status_bits(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t status;
    
    status = pci_read_config_word(bus, device, function, PCI_STATUS);
    if (status == 0xFFFF) {
        LOG_ERROR("Failed to read PCI status register");
        return false;
    }
    
    /* Clear error bits (write-1-to-clear) */
    status &= (PCI_STATUS_PARITY | PCI_STATUS_SIG_TARGET_ABORT |
               PCI_STATUS_REC_TARGET_ABORT | PCI_STATUS_REC_MASTER_ABORT |
               PCI_STATUS_SIG_SYSTEM_ERROR | PCI_STATUS_DETECTED_PARITY);
    
    if (status) {
        if (!pci_write_config_word(bus, device, function, PCI_STATUS, status)) {
            LOG_ERROR("Failed to clear PCI status bits");
            return false;
        }
        LOG_DEBUG("Cleared PCI status bits 0x%04X for %02X:%02X.%X",
                  status, bus, device, function);
    }
    
    return true;
}

/**
 * @brief Set PCI Cache Line Size
 * 
 * Programs cache line size for optimal burst transfers.
 * Required for Memory Write and Invalidate.
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param cls Cache line size in dwords (typically 8 or 16)
 * @return true on success, false on error
 */
bool pci_set_cache_line_size(uint8_t bus, uint8_t device, uint8_t function, uint8_t cls) {
    uint8_t current;
    
    /* Validate cache line size (must be power of 2, typically 8 or 16) */
    if (cls != 0 && cls != 8 && cls != 16 && cls != 32) {
        LOG_WARNING("Non-standard cache line size %d requested", cls);
    }
    
    current = pci_read_config_byte(bus, device, function, PCI_CACHE_LINE_SIZE);
    
    if (current == 0 || current != cls) {
        if (!pci_write_config_byte(bus, device, function, PCI_CACHE_LINE_SIZE, cls)) {
            LOG_ERROR("Failed to set cache line size");
            return false;
        }
        LOG_DEBUG("Set cache line size to %d for %02X:%02X.%X",
                  cls, bus, device, function);
    }
    
    return true;
}

/**
 * @brief Set PCI Latency Timer
 * 
 * Programs latency timer for bus master burst duration.
 * Prevents single device from hogging the bus.
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param latency Latency timer value (typically 32-248)
 * @return true on success, false on error
 */
bool pci_set_latency_timer(uint8_t bus, uint8_t device, uint8_t function, uint8_t latency) {
    uint8_t current;
    
    /* Typical values are 32, 64, 96, 128, 248 */
    if (latency < 16) {
        LOG_WARNING("Very low latency timer %d may cause performance issues", latency);
    }
    
    current = pci_read_config_byte(bus, device, function, PCI_LATENCY_TIMER);
    
    if (current < latency) {
        if (!pci_write_config_byte(bus, device, function, PCI_LATENCY_TIMER, latency)) {
            LOG_ERROR("Failed to set latency timer");
            return false;
        }
        LOG_DEBUG("Set latency timer to %d for %02X:%02X.%X",
                  latency, bus, device, function);
    }
    
    return true;
}

/**
 * @brief Perform complete PCI device configuration setup
 * 
 * One-stop function that does all necessary PCI config hygiene:
 * - Clears status bits
 * - Sets command bits (I/O, MEM, Master)
 * - Programs cache line size
 * - Sets latency timer
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param enable_io Enable I/O space
 * @param enable_mem Enable memory space
 * @param enable_master Enable bus mastering
 * @return true on success, false on error
 */
bool pci_device_setup(uint8_t bus, uint8_t device, uint8_t function,
                     bool enable_io, bool enable_mem, bool enable_master) {
    uint16_t cmd_bits = 0;
    
    LOG_INFO("Setting up PCI device %02X:%02X.%X", bus, device, function);
    
    /* Clear any stale status bits first */
    if (!pci_clear_status_bits(bus, device, function)) {
        LOG_ERROR("Failed to clear status bits");
        return false;
    }
    
    /* Build command bits */
    if (enable_io) cmd_bits |= PCI_CMD_IO;
    if (enable_mem) cmd_bits |= PCI_CMD_MEMORY;
    if (enable_master) cmd_bits |= PCI_CMD_MASTER;
    
    /* Set command register */
    if (!pci_set_command_bits(bus, device, function, cmd_bits)) {
        LOG_ERROR("Failed to set command bits");
        return false;
    }
    
    /* Only fix cache line size if obviously invalid or user requested */
    if (!pci_set_cache_line_size(bus, device, function, 0)) {  /* 0 = auto-detect only if needed */
        LOG_WARNING("Failed to validate cache line size (non-fatal)");
    }
    
    /* Only adjust latency timer if current value is too low for bus masters */
    if (enable_master) {
        uint8_t current_latency = pci_read_config_byte(bus, device, function, PCI_LATENCY_TIMER);
        if (current_latency < 32) {  /* Only fix if too aggressive */
            if (!pci_set_latency_timer(bus, device, function, 32)) {
                LOG_WARNING("Failed to set minimum latency timer (non-fatal)");
            }
        } else {
            LOG_DEBUG("Latency timer %d is acceptable - leaving unchanged", current_latency);
        }
    }
    
    LOG_INFO("PCI device setup complete for %02X:%02X.%X", bus, device, function);
    
    return true;
}
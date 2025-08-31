/**
 * @file common.h
 * @brief Common constants, macros, and type definitions
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef _COMMON_H_
#define _COMMON_H_

#ifdef __cplusplus
extern "C" {
#endif

/* System includes */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <dos.h>  /* For FP_SEG and FP_OFF macros */

/* Assembly compatibility */
#ifdef __ASSEMBLER__
#define EXTERN extern
#else
#define EXTERN extern
#endif

/* Common constants */
#define MAX_NICS            8       /* Maximum number of NICs supported */
#define MAX_PACKET_SIZE     1514    /* Maximum Ethernet packet size */
#define MIN_PACKET_SIZE     60      /* Minimum Ethernet packet size (w/o CRC) */
#define ETH_ALEN            6       /* Ethernet address length */
#define ETH_HLEN            14      /* Ethernet header length */
#define ETH_CRC_LEN         4       /* Ethernet CRC length */

/* Buffer sizes */
#define TX_BUFFER_SIZE      1600    /* Transmit buffer size */
#define RX_BUFFER_SIZE      1600    /* Receive buffer size */
#define DMA_BUFFER_SIZE     2048    /* DMA buffer size */

/* Error codes */
#define SUCCESS             0       /* Operation successful */
#define ERROR_GENERIC       -1      /* Generic error */
#define ERROR_NO_MEMORY     -2      /* Out of memory */
#define ERROR_INVALID_PARAM -3      /* Invalid parameter */
#define ERROR_TIMEOUT       -4      /* Operation timeout */
#define ERROR_NOT_FOUND     -5      /* Resource not found */
#define ERROR_BUSY          -6      /* Resource busy */
#define ERROR_IO            -7      /* I/O error */
#define ERROR_HARDWARE      -8      /* Hardware error */
#define ERROR_NOT_SUPPORTED -9      /* Operation not supported */

/* Device registry error codes */
#define ERROR_NOT_INITIALIZED   -10     /* Service not initialized */
#define ERROR_INVALID_PARAMETER -11     /* Invalid parameter */
#define ERROR_DEVICE_EXISTS     -12     /* Device already exists */
#define ERROR_REGISTRY_FULL     -13     /* Registry full */
#define ERROR_DEVICE_NOT_FOUND  -14     /* Device not found */
#define ERROR_DEVICE_BUSY       -15     /* Device busy/claimed */
#define ERROR_DEVICE_NOT_CLAIMED -16    /* Device not claimed */
#define ERROR_ACCESS_DENIED     -17     /* Access denied */
#define ERROR_HARDWARE_NOT_FOUND -18    /* Hardware not found */
#define ERROR_MODULE_NOT_READY  -19     /* Module not ready */
#define ERROR_MEMORY_ALLOC      -20     /* Memory allocation failed */

/* ISR Safety error codes */
#define ERROR_ISR_UNSAFE        -21     /* ISR safety violation */
#define ERROR_ISR_REENTRANT     -22     /* ISR reentrancy detected */
#define ERROR_ISR_STACK_OVERFLOW -23    /* ISR stack overflow */
#define WARNING_ISR_SLOW        1       /* ISR execution time warning */

/* Hardware I/O macros - compatible with both C and Assembly */
#ifndef __ASSEMBLER__

/* I/O port access functions */
void outb(uint16_t port, uint8_t value);
void outw(uint16_t port, uint16_t value);
void outl(uint16_t port, uint32_t value);
uint8_t inb(uint16_t port);
uint16_t inw(uint16_t port);
uint32_t inl(uint16_t port);

/* Memory barriers and delays */
void io_delay(void);                    /* Short I/O delay */
void udelay(unsigned int microseconds); /* Microsecond delay */
void mdelay(unsigned int milliseconds); /* Millisecond delay */

/* Timestamp functions */
uint32_t get_system_timestamp_ticks(void);      /* Get BIOS timer ticks since midnight */
uint32_t get_system_timestamp_ms(void);         /* Get timestamp in milliseconds */
uint32_t get_timestamp_elapsed_ms(uint32_t start_ticks); /* Get elapsed milliseconds from start */

/* Physical address calculation for DMA (GPT-5 Critical Fix) */
static inline uint32_t phys_from_ptr(void far* p) {
    /* Convert far pointer to physical address in real mode */
    return ((uint32_t)FP_SEG(p) << 4) + FP_OFF(p);
}

#endif /* !__ASSEMBLER__ */

/* Bit manipulation macros */
#define BIT(n)              (1U << (n))
#define BITS(start, end)    (((1U << ((end) - (start) + 1)) - 1) << (start))
#define SET_BIT(reg, bit)   ((reg) |= BIT(bit))
#define CLEAR_BIT(reg, bit) ((reg) &= ~BIT(bit))
#define TEST_BIT(reg, bit)  (((reg) & BIT(bit)) != 0)

/* Alignment macros */
#define ALIGN_UP(x, align)    (((x) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(x, align)  ((x) & ~((align) - 1))
#define IS_ALIGNED(x, align)  (((x) & ((align) - 1)) == 0)

/* Endianness conversion macros */
#define SWAP16(x)   ((((x) & 0xFF) << 8) | (((x) >> 8) & 0xFF))
#define SWAP32(x)   ((((x) & 0xFF) << 24) | (((x) & 0xFF00) << 8) | \
                     (((x) >> 8) & 0xFF00) | (((x) >> 24) & 0xFF))

/* Network byte order conversions */
#ifdef LITTLE_ENDIAN
#define htons(x)    SWAP16(x)
#define ntohs(x)    SWAP16(x)
#define htonl(x)    SWAP32(x)
#define ntohl(x)    SWAP32(x)
#else
#define htons(x)    (x)
#define ntohs(x)    (x)
#define htonl(x)    (x)
#define ntohl(x)    (x)
#endif

/* Compiler attributes */
#ifdef __GNUC__
#define PACKED      __attribute__((packed))
#define ALIGNED(x)  __attribute__((aligned(x)))
#define UNUSED      __attribute__((unused))
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define PACKED
#define ALIGNED(x)
#define UNUSED
#define LIKELY(x)   (x)
#define UNLIKELY(x) (x)
#endif

/* Memory allocation alignment */
#define DMA_ALIGNMENT       16      /* DMA buffer alignment requirement */
#define CACHE_ALIGNMENT     32      /* Cache line alignment */

/* Debug and logging levels */
#define LOG_LEVEL_NONE      0
#define LOG_LEVEL_ERROR     1
#define LOG_LEVEL_WARN      2
#define LOG_LEVEL_INFO      3
#define LOG_LEVEL_DEBUG     4
#define LOG_LEVEL_TRACE     5

/* Feature flags */
#define FEATURE_PNP         BIT(0)  /* Plug and Play support */
#define FEATURE_DMA         BIT(1)  /* DMA support */
#define FEATURE_PROMISCUOUS BIT(2)  /* Promiscuous mode */
#define FEATURE_MULTICAST   BIT(3)  /* Multicast support */
#define FEATURE_FULL_DUPLEX BIT(4)  /* Full duplex support */
#define FEATURE_AUTO_SPEED  BIT(5)  /* Auto speed detection */

/* TSR Defensive Programming Patterns - Memory Protection */
#define CANARY_PATTERN_FRONT    0xDEADBEEF  /* Front canary for memory blocks */
#define CANARY_PATTERN_REAR     0xBEEFDEAD  /* Rear canary for memory blocks */
#define SIGNATURE_MAGIC         0x5A5A3C3C  /* Structure signature pattern */
#define CHECKSUM_SEED           0xA5A5      /* Initial checksum seed */

/* Hardware timeout constants (in iterations) */
#define TIMEOUT_SHORT           1000        /* ~1ms for quick operations */
#define TIMEOUT_MEDIUM          5000        /* ~5ms for medium operations */
#define TIMEOUT_LONG            10000       /* ~10ms for reset, initialization */
#define TIMEOUT_DMA             50000       /* ~50ms for DMA operations */

/* Retry constants */
#define MAX_RETRY_COUNT         3           /* Maximum retry attempts */
#define RETRY_DELAY_BASE        100         /* Base retry delay in iterations */

/* TSR (Terminate and Stay Resident) constants */
#define TSR_PARAGRAPH_SIZE  16      /* DOS paragraph size */
#define TSR_SUCCESS         0       /* TSR installation success */
#define TSR_ERROR           1       /* TSR installation error */

/* DOS Real-Mode Helpers */

/* Far pointer structure for real mode addressing */
typedef struct {
    uint16_t offset;
    uint16_t segment;
} far_ptr_t;

/* Convert far pointer to physical address */
#define FAR_TO_PHYSICAL(seg, off)  (((uint32_t)(seg) << 4) + (uint16_t)(off))
#define PHYSICAL_TO_SEG(addr)       ((uint16_t)((addr) >> 4))
#define PHYSICAL_TO_OFF(addr)       ((uint16_t)((addr) & 0x0F))

/* Make far pointer from segment:offset */
#define MK_FP(seg, off)            ((void far *)((((uint32_t)(seg)) << 16) | (off)))
#define FP_SEG(fp)                  ((uint16_t)((uint32_t)(fp) >> 16))
#define FP_OFF(fp)                  ((uint16_t)(fp))

/* Hardware NIC context structure */
typedef struct {
    uint16_t iobase;        /* I/O base address */
    uint8_t  irq;           /* IRQ number */
    uint8_t  nic_type;      /* 1=3C509B, 2=3C515 */
    uint8_t  flags;         /* Status flags */
    uint8_t  window;        /* Current register window */
    uint8_t  mac[6];        /* MAC address */
    uint16_t tx_free;       /* TX FIFO free bytes */
    uint16_t rx_status;     /* Last RX status */
    uint32_t tx_packets;    /* TX packet counter */
    uint32_t rx_packets;    /* RX packet counter */
    uint32_t tx_errors;     /* TX error counter */
    uint32_t rx_errors;     /* RX error counter */
} nic_context_t;

/* NIC type definitions */
#define NIC_TYPE_NONE       0
#define NIC_TYPE_3C509B     1
#define NIC_TYPE_3C515      2

/* Bus type enumeration */
typedef enum {
    BUS_TYPE_UNKNOWN = 0,
    BUS_TYPE_ISA,
    BUS_TYPE_EISA,
    BUS_TYPE_MCA,        /* IBM MicroChannel Architecture */
    BUS_TYPE_VLB,        /* VESA Local Bus */
    BUS_TYPE_PCI,
    BUS_TYPE_PCMCIA,
    BUS_TYPE_CARDBUS
} bus_type_t;

/* PS/2 Model identification (for MCA systems) */
typedef enum {
    PS2_MODEL_UNKNOWN = 0,
    PS2_MODEL_50,        /* 16-bit MCA */
    PS2_MODEL_60,        /* 16-bit MCA */
    PS2_MODEL_70,        /* 32-bit MCA, 16/20MHz */
    PS2_MODEL_80,        /* 32-bit MCA, 16/20MHz */
    PS2_MODEL_90,        /* 32-bit MCA, 20/25MHz */
    PS2_MODEL_95,        /* 32-bit MCA, 33/50MHz */
    PS2_MODEL_56,        /* 32-bit MCA, SLC2 */
    PS2_MODEL_57         /* 32-bit MCA, SLC3 */
} ps2_model_t;

/* NIC flags */
#define NIC_FLAG_CONFIGURED     0x01    /* NIC is configured */
#define NIC_FLAG_ENABLED        0x02    /* NIC is enabled */
#define NIC_FLAG_BUS_MASTER     0x04    /* Bus master DMA enabled */
#define NIC_FLAG_FULL_DUPLEX    0x08    /* Full duplex mode */
#define NIC_FLAG_100MBPS        0x10    /* 100Mbps mode */
#define NIC_FLAG_PROMISCUOUS    0x20    /* Promiscuous mode */

/* Critical section macros for interrupt safety */
#ifdef __WATCOMC__
/* GPT-5 Critical Fix: Proper interrupt state preservation
 * Always save/restore interrupt flag to avoid re-enabling interrupts in ISR context
 * Using do-while(0) pattern for proper statement behavior
 */
#define ENTER_CRITICAL() do { \
    __asm { pushf } \
    __asm { cli } \
} while(0)
    
#define EXIT_CRITICAL() do { \
    __asm { popf } \
} while(0)

#else
/* GCC/other compilers - already correct, with do-while(0) pattern */
#define ENTER_CRITICAL() do { __asm__("pushf; cli"); } while(0)
#define EXIT_CRITICAL()  do { __asm__("popf"); } while(0)
#endif

/* I/O port access macros for timing */
#define IO_DELAY()          inb(0x80)  /* ~3.3us delay on ISA bus */
#define DELAY_LOOPS(n)      { int _i; for(_i = 0; _i < (n); _i++) IO_DELAY(); }

/* TSR Defensive Macros - Structure Validation */
#define VALIDATE_STRUCTURE(ptr, sig) \
    ((ptr) && ((ptr)->signature == (sig)) && validate_checksum(ptr))

#define UPDATE_STRUCTURE_CHECKSUM(ptr) do { \
    (ptr)->checksum = calculate_checksum(ptr); \
} while(0)

#define VALIDATE_CANARY_FRONT(ptr) \
    (*(uint32_t*)((uint8_t*)(ptr) - sizeof(uint32_t)) == CANARY_PATTERN_FRONT)

#define VALIDATE_CANARY_REAR(ptr, size) \
    (*(uint32_t*)((uint8_t*)(ptr) + (size)) == CANARY_PATTERN_REAR)

/* TSR Defensive Macros - Hardware Operations */
#define WAIT_FOR_CONDITION(port, mask, timeout) ({ \
    uint32_t _count = (timeout); \
    uint8_t _val; \
    do { \
        _val = inb(port); \
        if ((_val & (mask)) == (mask)) break; \
        IO_DELAY(); \
    } while (--_count > 0); \
    (_count > 0) ? 0 : -1; \
})

#define RETRY_ON_ERROR(func, max_retries) ({ \
    int _result; \
    int _retry = 0; \
    do { \
        _result = (func); \
        if (_result == 0) break; \
        if (_retry > 0) { \
            for (int _d = 0; _d < RETRY_DELAY_BASE * _retry; _d++) \
                IO_DELAY(); \
        } \
    } while (++_retry < (max_retries)); \
    _result; \
})

/* Memory barrier for DOS (ensures I/O completion) */
#define IO_BARRIER()        { volatile uint8_t _dummy = inb(0x80); }

/* PIC (Programmable Interrupt Controller) helpers */
#define PIC1_COMMAND        0x20    /* Master PIC command port */
#define PIC1_DATA           0x21    /* Master PIC data port */
#define PIC2_COMMAND        0xA0    /* Slave PIC command port */
#define PIC2_DATA           0xA1    /* Slave PIC data port */
#define PIC_EOI             0x20    /* End of Interrupt command */

/* Send EOI to appropriate PIC */
#define SEND_EOI(irq) do { \
    if ((irq) > 7) outb(PIC2_COMMAND, PIC_EOI); \
    outb(PIC1_COMMAND, PIC_EOI); \
} while(0)

#ifdef __cplusplus
}
#endif

#endif /* _COMMON_H_ */

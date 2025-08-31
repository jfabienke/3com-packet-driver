/**
 * @file main.h
 * @brief Main driver functions, initialization, and entry points
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef _MAIN_H_
#define _MAIN_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"
#include "config.h"
#include "hardware.h"
#include "chipset_detect.h"

/* Bus type enumeration */
typedef enum {
    BUS_ISA = 0,
    BUS_EISA,
    BUS_MCA,
    BUS_PCI,
    BUS_VLB,
    BUS_PCMCIA
} bus_type_t;

/* Error codes */
#define MAIN_SUCCESS        0
#define MAIN_ERR_INIT      -1
#define MAIN_ERR_NO_NICS   -2
#define MAIN_ERR_MEMORY    -3
#define MAIN_ERR_CONFIG    -4
#define MAIN_ERR_HARDWARE  -5
#define MAIN_ERR_API       -6

/* Driver state structure */
typedef struct {
    config_t config;           /* Configuration parameters */
    int num_nics;             /* Number of detected NICs */
    uint32_t flags;           /* Driver status flags */
    void *memory_pool;        /* Memory pool pointer */
    uint16_t interrupt_vector; /* Installed interrupt vector */
    chipset_detection_result_t chipset_result; /* Chipset detection results */
    bus_type_t bus_type;      /* System bus type */
} driver_state_t;

/* Driver version information */
#define DRIVER_VERSION_MAJOR    1
#define DRIVER_VERSION_MINOR    0
#define DRIVER_BUILD_NUMBER     0

/* Driver signature for TSR identification */
#define DRIVER_SIGNATURE        "3COMPKT"
#define DRIVER_SIGNATURE_LEN    8

/* Global driver state */
extern driver_state_t g_driver_state;

/* Main entry points */
int main(int argc, char* argv[]);
void driver_entry_point(void);          /* Main driver entry from DOS */
void interrupt_entry_point(void);       /* Interrupt handler entry */

/* Enhanced function prototypes */
int driver_init(const char *config_params);
int driver_cleanup(void);
driver_state_t* get_driver_state(void);
int is_driver_initialized(void);
const char* get_error_message(int error_code);

/* TSR (Terminate and Stay Resident) functions */
int tsr_install(uint16_t paragraphs);
void tsr_uninstall(void);
bool tsr_check_installed(void);
int tsr_relocate(void);                /* Phase 11: TSR relocation */

/* Command line processing */
int parse_command_line(int argc, char* argv[], config_t* config);
void print_usage(const char* program_name);
void print_version(void);
void print_help(void);

/* Status and information functions */
void print_driver_status(void);
void print_nic_info(void);
void print_statistics(void);

/* Hardware abstraction interface */
int main_hardware_init(void);
void main_hardware_cleanup(void);

/* Interrupt management */
void main_interrupt_handler(void);
void main_enable_interrupts(void);
void main_disable_interrupts(void);
int enable_driver_interrupts(void);    /* Phase 12: Precise IRQ enabling */
int disable_driver_interrupts(void);   /* Cleanup: Disable IRQs */

/* Memory management interface */
int main_memory_init(void);
void main_memory_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* _MAIN_H_ */

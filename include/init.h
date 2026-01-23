/**
 * @file init.h
 * @brief Driver initialization and setup
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef _INIT_H_
#define _INIT_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include <stdint.h>
#include "config.h"
#include "pltprob.h"

/* CPU type definitions */
#define CPU_TYPE_8086      0
#define CPU_TYPE_80286     1  
#define CPU_TYPE_386       2
#define CPU_TYPE_486       3
#define CPU_TYPE_PENTIUM   4

/* Error codes */
#define INIT_SUCCESS           0
#define INIT_ERR_INVALID_PARAM -1
#define INIT_ERR_MEMORY        -2
#define INIT_ERR_NO_NICS       -3
#define INIT_ERR_NIC_INIT      -4
#define INIT_ERR_ROUTING       -5
#define INIT_ERR_STATS         -6
#define INIT_ERR_CPU_DETECT    -7
#define INIT_ERR_HARDWARE      -8
#define INIT_ERR_NO_PCI        -9

/* Initialization state structure */
typedef struct {
    int cpu_type;              /* Detected CPU type */
    int num_nics;              /* Number of detected NICs */
    int hardware_ready;        /* Hardware initialized flag */
    int memory_ready;          /* Memory management ready flag */
    int routing_ready;         /* Routing subsystem ready flag */
    int stats_ready;           /* Statistics subsystem ready flag */
    int xms_available;         /* XMS memory available flag */
    int fully_initialized;     /* Complete initialization flag */
    platform_probe_result_t platform; /* Platform detection results */
    dma_policy_t dma_policy;   /* Determined DMA policy */
} init_state_t;

/* Function prototypes */
int detect_cpu_type(void);
int hardware_init_all(const config_t *config);
int memory_init(const config_t *config);
int routing_init(const config_t *config);
int stats_init(const config_t *config);
int init_complete_sequence(const config_t *config);
int init_cleanup(void);
const init_state_t* get_init_state(void);
int is_init_complete(void);

#ifdef __cplusplus
}
#endif

#endif /* _INIT_H_ */

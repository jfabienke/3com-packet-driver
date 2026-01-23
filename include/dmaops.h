/**
 * @file dma_operations.h
 * @brief Production-grade DMA operations interface
 */

#ifndef _DMA_OPERATIONS_H_
#define _DMA_OPERATIONS_H_

#include <stdint.h>
#include <stdbool.h>
#include "common.h"
#include "hardware.h"
#include "vds.h"

/* Forward declarations */
typedef struct dma_operation dma_operation_t;

/* Error codes specific to DMA operations */
#define ERROR_DMA_LOCK      -100
#define ERROR_DMA_ADDRESS   -101
#define ERROR_DMA_UNSAFE    -102

/* ISR context management */
void dma_enter_isr_context(void);
void dma_exit_isr_context(void);
bool dma_in_isr_context(void);
void dma_process_deferred_cache_ops(void);

/* DMA preparation and completion */
int dma_prepare_tx(nic_info_t *nic, void far *buffer, uint32_t size, dma_operation_t *op);
int dma_prepare_rx(nic_info_t *nic, void far *buffer, uint32_t size, dma_operation_t *op);
void dma_complete_operation(dma_operation_t *op);

/* NIC-specific constraint validation */
bool dma_validate_3c515_constraints(uint32_t phys_addr, uint32_t size);
bool dma_validate_3c509_constraints(uint32_t phys_addr, uint32_t size);

/* Coherency strategy documentation */
const char* dma_get_nic_coherency_strategy(nic_info_t *nic);

#endif /* _DMA_OPERATIONS_H_ */
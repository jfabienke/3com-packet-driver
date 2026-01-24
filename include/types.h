/**
 * @file types.h
 * @brief Master Type Registry - Canonical type definitions for 3Com Packet Driver
 *
 * This file provides the definitive type definitions to resolve conflicts between
 * different headers. All modules should include this file for consistent types.
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 * Last Updated: 2026-01-24 14:45:00 CET
 */

#ifndef _TYPES_H_
#define _TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

/* System includes */
#include <stdint.h>
#include <stddef.h>

/* Core type definitions */
#include "common.h"

/* Canonical NIC type enumeration from nic_defs.h */
#include "nic_defs.h"

/* Canonical statistics types from stats.h */
#include "stats.h"

/* Forward declarations for context structures */
/* Primary NIC context - defined in nicctx.h */
struct nic_context;
typedef struct nic_context nic_context_t;

/* Capability-focused context - defined in niccap.h */
struct nic_cap_context;
typedef struct nic_cap_context nic_cap_context_t;

/* Extended NIC info structure - defined in hardware.h */
struct nic_info;

/* Compatibility defines for type naming variations */
/* Map underscore vs no-underscore variants */
#define NIC_TYPE_3C515TX    NIC_TYPE_3C515_TX

/* Guard macros for conditional type definitions */
#define NIC_CONTEXT_T_DEFINED       1
#define NIC_CAP_CONTEXT_T_DEFINED   1
#define NIC_TYPE_T_DEFINED          1

#ifdef __cplusplus
}
#endif

#endif /* _TYPES_H_ */

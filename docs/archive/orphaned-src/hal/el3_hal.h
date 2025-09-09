/**
 * @file el3_hal.h
 * @brief Hardware Abstraction Layer Header
 *
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef _EL3_HAL_H_
#define _EL3_HAL_H_

/* Ensure HAL is not used in datapath by mistake */
#ifdef EL3_DATAPATH_COMPILATION
#error "HAL must not be used in datapath code! Use direct I/O instead."
#endif

/* HAL functions are already declared in el3_core.h */

#endif /* _EL3_HAL_H_ */
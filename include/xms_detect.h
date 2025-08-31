/**
 * @file xms_detect.h
 * @brief XMS memory detection and allocation
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef _XMS_DETECT_H_
#define _XMS_DETECT_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include <stdint.h>

/* Defines */
#define XMS_MAX_HANDLES 16

/* Error codes */
#define XMS_SUCCESS               0
#define XMS_ERR_NOT_AVAILABLE    -1
#define XMS_ERR_FUNCTION_FAILED  -2
#define XMS_ERR_INVALID_PARAM    -3
#define XMS_ERR_INVALID_SIZE     -4
#define XMS_ERR_NO_HANDLES       -5
#define XMS_ERR_INVALID_HANDLE   -6
#define XMS_ERR_ALLOCATION_FAILED -7
#define XMS_ERR_NOT_LOCKED       -8

/* XMS information structure */
typedef struct {
    uint8_t version_major;      /* XMS major version */
    uint8_t version_minor;      /* XMS minor version */
    uint16_t total_kb;          /* Total XMS memory in KB */
    uint16_t free_kb;           /* Free XMS memory in KB */
    uint16_t largest_block_kb;  /* Largest available block in KB */
} xms_info_t;

/* XMS handle information */
typedef struct {
    uint16_t handle;            /* XMS handle */
    uint16_t size_kb;           /* Size in KB */
    int in_use;                 /* Handle in use flag */
    int locked;                 /* Block locked flag */
    int lock_count;             /* Lock count */
    uint32_t linear_address;    /* Linear address when locked */
} xms_handle_t;

/* Function prototypes */
int xms_detect_and_init(void);
int xms_allocate(int size_kb, uint16_t *handle);
int xms_free(uint16_t handle);
int xms_lock(uint16_t handle, uint32_t *linear_address);
int xms_unlock(uint16_t handle);
int xms_move_memory(uint16_t dest_handle, uint32_t dest_offset,
                   uint16_t src_handle, uint32_t src_offset, uint32_t length);
int xms_get_info(xms_info_t *info);
int xms_is_available(void);
int xms_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* _XMS_DETECT_H_ */

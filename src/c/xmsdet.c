/**
 * @file xms_detect.c
 * @brief XMS memory detection and allocation
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include <dos.h>
#include <stdio.h>
#include <string.h>
#include "../../include/common.h"
#include "../../include/xmsdet.h"
#include "../../include/logging.h"

/* XMS function numbers */
#define XMS_GET_VERSION        0x00
#define XMS_REQUEST_HMA        0x01
#define XMS_RELEASE_HMA        0x02
#define XMS_GLOBAL_ENABLE_A20  0x03
#define XMS_GLOBAL_DISABLE_A20 0x04
#define XMS_LOCAL_ENABLE_A20   0x05
#define XMS_LOCAL_DISABLE_A20  0x06
#define XMS_QUERY_A20          0x07
#define XMS_QUERY_FREE_EXTENDED 0x08
#define XMS_ALLOCATE_EXTENDED  0x09
#define XMS_FREE_EXTENDED      0x0A
#define XMS_MOVE_EXTENDED      0x0B
#define XMS_LOCK_EXTENDED      0x0C
#define XMS_UNLOCK_EXTENDED    0x0D
#define XMS_GET_HANDLE_INFO    0x0E
#define XMS_RESIZE_EXTENDED    0x0F

/* XMS error codes */
#define XMS_SUCCESS            0x00
#define XMS_NOT_IMPLEMENTED    0x80
#define XMS_VDISK_DETECTED     0x81
#define XMS_A20_ERROR          0x82
#define XMS_GENERAL_ERROR      0x8E
#define XMS_UNRECOVERABLE_ERROR 0x8F
#define XMS_HMA_NOT_EXIST      0x90
#define XMS_HMA_IN_USE         0x91
#define XMS_DX_LESS_THAN_REQ   0x92
#define XMS_OUT_OF_HANDLES     0x93
#define XMS_INVALID_HANDLE     0x94
#define XMS_INVALID_SOURCE     0x95
#define XMS_INVALID_DEST       0x96
#define XMS_INVALID_LENGTH     0x97
#define XMS_INVALID_OVERLAP    0x98
#define XMS_PARITY_ERROR       0x99
#define XMS_BLOCK_NOT_LOCKED   0x9A
#define XMS_BLOCK_LOCKED       0x9B
#define XMS_LOCK_COUNT_OVERFLOW 0x9C
#define XMS_LOCK_FAILED        0x9D
#define XMS_SMALLER_UMB        0xB0
#define XMS_NO_UMBS_AVAILABLE  0xB1
#define XMS_INVALID_UMB_SEGMENT 0xB2

/* Global XMS state */
static void (far *xms_entry_point)(void) = NULL;
static int xms_available = 0;
static xms_info_t xms_info = {0};
static xms_handle_t xms_handles[XMS_MAX_HANDLES];
static int num_handles = 0;

/**
 * @brief Check if XMS driver is installed
 * @return 1 if installed, 0 if not
 */
static int xms_check_installed(void) {
    union REGS regs;
    struct SREGS sregs;
    
    log_debug("Checking for XMS driver installation");
    
    /* Check for XMS driver using INT 2Fh, AX=4300h */
    regs.x.ax = 0x4300;
    int86(0x2F, &regs, &regs);
    
    if (regs.h.al != 0x80) {
        log_debug("XMS driver not installed (AL=%02Xh)", regs.h.al);
        return 0;
    }
    
    /* Get XMS entry point using INT 2Fh, AX=4310h */
    regs.x.ax = 0x4310;
    int86x(0x2F, &regs, &regs, &sregs);
    
    /* Store entry point */
    xms_entry_point = MK_FP(sregs.es, regs.x.bx);
    
    log_debug("XMS driver found at %04X:%04X", sregs.es, regs.x.bx);
    return 1;
}

/**
 * @brief Call XMS function
 * @param function XMS function number
 * @param dx DX register value
 * @return AX register value from XMS call
 */
static int xms_call(int function, int dx) {
    union REGS regs;
    
    if (!xms_entry_point) {
        return XMS_GENERAL_ERROR;
    }
    
    regs.h.ah = function;
    regs.x.dx = dx;
    
    /* Call XMS driver through far function pointer */
    /* Use inline assembly for proper XMS driver call */
#ifdef __WATCOMC__
    /* Watcom C inline assembly */
    _asm {
        mov ah, byte ptr function
        mov dx, word ptr dx
        call dword ptr xms_entry_point
        mov word ptr regs.x.ax, ax
        mov word ptr regs.x.dx, dx
        mov word ptr regs.h.bl, bl
    }
#elif defined(__TURBOC__)
    /* Turbo C inline assembly */
    asm {
        mov ah, function
        mov dx, dx
        call dword ptr xms_entry_point
        mov regs.x.ax, ax
        mov regs.x.dx, dx
        mov regs.h.bl, bl
    }
#else
    /* GCC/DJGPP inline assembly */
    asm volatile (
        "movb %3, %%ah\n\t"
        "movw %4, %%dx\n\t"
        "lcall *%5\n\t"
        "movw %%ax, %0\n\t"
        "movw %%dx, %1\n\t"
        "movb %%bl, %2\n\t"
        : "=m" (regs.x.ax), "=m" (regs.x.dx), "=m" (regs.h.bl)
        : "m" ((uint8_t)function), "m" ((uint16_t)dx), "m" (xms_entry_point)
        : "ax", "dx", "bx"
    );
#endif
    
    log_debug("XMS call: AH=%02Xh, DX=%04Xh -> AX=%04Xh", function, dx, regs.x.ax);
    
    return regs.x.ax;
}

/**
 * @brief Extended XMS call with multiple return values
 * @param function XMS function number
 * @param dx DX register value
 * @param ax_ret Pointer to store AX return value
 * @param dx_ret Pointer to store DX return value
 * @param bl_ret Pointer to store BL return value (error code)
 * @return 1 if AX indicates success, 0 if failure
 */
static int xms_call_extended(int function, int dx, uint16_t *ax_ret, uint16_t *dx_ret, uint8_t *bl_ret) {
    union REGS regs;
    
    if (!xms_entry_point) {
        if (bl_ret) *bl_ret = XMS_GENERAL_ERROR;
        return 0;
    }
    
    regs.h.ah = function;
    regs.x.dx = dx;
    
    /* Call XMS driver through far function pointer */
#ifdef __WATCOMC__
    _asm {
        mov ah, byte ptr function
        mov dx, word ptr dx
        call dword ptr xms_entry_point
        mov word ptr regs.x.ax, ax
        mov word ptr regs.x.dx, dx
        mov byte ptr regs.h.bl, bl
    }
#elif defined(__TURBOC__)
    asm {
        mov ah, function
        mov dx, dx
        call dword ptr xms_entry_point
        mov regs.x.ax, ax
        mov regs.x.dx, dx
        mov regs.h.bl, bl
    }
#else
    asm volatile (
        "movb %3, %%ah\n\t"
        "movw %4, %%dx\n\t"
        "lcall *%5\n\t"
        "movw %%ax, %0\n\t"
        "movw %%dx, %1\n\t"
        "movb %%bl, %2\n\t"
        : "=m" (regs.x.ax), "=m" (regs.x.dx), "=m" (regs.h.bl)
        : "m" ((uint8_t)function), "m" ((uint16_t)dx), "m" (xms_entry_point)
        : "ax", "dx", "bx"
    );
#endif
    
    if (ax_ret) *ax_ret = regs.x.ax;
    if (dx_ret) *dx_ret = regs.x.dx;
    if (bl_ret) *bl_ret = regs.h.bl;
    
    return (regs.x.ax != 0);
}

/**
 * @brief Detect and initialize XMS
 * @return 0 on success, negative on error
 */
int xms_detect_and_init(void) {
    uint16_t ax_val, dx_val;
    uint8_t bl_val;
    
    log_info("Detecting XMS memory manager");
    
    /* Check if XMS driver is installed */
    if (!xms_check_installed()) {
        log_info("XMS driver not available");
        return XMS_ERR_NOT_AVAILABLE;
    }
    
    /* Get XMS version information */
    if (!xms_call_extended(XMS_GET_VERSION, 0, &ax_val, &dx_val, &bl_val)) {
        log_error("Failed to get XMS version (error %02Xh)", bl_val);
        return XMS_ERR_FUNCTION_FAILED;
    }
    
    xms_info.version_major = (ax_val >> 8) & 0xFF;
    xms_info.version_minor = ax_val & 0xFF;
    
    /* Check for minimum XMS version 2.0 */
    if (xms_info.version_major < 2) {
        log_error("XMS version %d.%d too old (need 2.0+)", 
                 xms_info.version_major, xms_info.version_minor);
        return XMS_ERR_FUNCTION_FAILED;
    }
    
    /* Check A20 line capability */
    if (!xms_call_extended(XMS_QUERY_A20, 0, &ax_val, &dx_val, &bl_val)) {
        log_warning("Cannot query A20 line status (error %02Xh)", bl_val);
    } else {
        log_debug("A20 line status: %s", (ax_val == 1) ? "enabled" : "disabled");
    }
    
    /* Query available extended memory */
    if (!xms_call_extended(XMS_QUERY_FREE_EXTENDED, 0, &ax_val, &dx_val, &bl_val)) {
        log_error("Failed to query XMS memory (error %02Xh)", bl_val);
        return XMS_ERR_FUNCTION_FAILED;
    }
    
    xms_info.free_kb = ax_val;
    xms_info.largest_block_kb = dx_val;
    xms_info.total_kb = ax_val; /* Initially all free */
    
    /* Initialize handle table */
    memset(xms_handles, 0, sizeof(xms_handles));
    num_handles = 0;
    
    xms_available = 1;
    
    log_info("XMS initialized: version %d.%d, %d KB available (largest block: %d KB)",
             xms_info.version_major, xms_info.version_minor, 
             xms_info.free_kb, xms_info.largest_block_kb);
    
    return 0;
}

/**
 * @brief Allocate XMS memory block
 * @param size_kb Size in kilobytes
 * @param handle Pointer to store handle
 * @return 0 on success, negative on error
 */
int xms_allocate(int size_kb, uint16_t *handle) {
    uint16_t ax_val;
    uint8_t bl_val;
    int i;
    
    if (!handle) {
        return XMS_ERR_INVALID_PARAM;
    }
    
    if (!xms_available) {
        log_error("XMS not available");
        return XMS_ERR_NOT_AVAILABLE;
    }
    
    if (size_kb <= 0 || size_kb > xms_info.free_kb) {
        log_error("Invalid allocation size: %d KB (available: %d KB)", size_kb, xms_info.free_kb);
        return XMS_ERR_INVALID_SIZE;
    }
    
    log_debug("Allocating %d KB of XMS memory", size_kb);
    
    /* Find free handle slot */
    for (i = 0; i < XMS_MAX_HANDLES; i++) {
        if (!xms_handles[i].in_use) {
            break;
        }
    }
    
    if (i >= XMS_MAX_HANDLES) {
        log_error("No free XMS handle slots");
        return XMS_ERR_NO_HANDLES;
    }
    
    /* Call XMS allocate function */
    if (!xms_call_extended(XMS_ALLOCATE_EXTENDED, size_kb, &ax_val, NULL, &bl_val)) {
        log_error("XMS allocation failed (error %02Xh)", bl_val);
        switch (bl_val) {
            case XMS_OUT_OF_HANDLES:
                return XMS_ERR_NO_HANDLES;
            case XMS_DX_LESS_THAN_REQ:
                return XMS_ERR_INVALID_SIZE;
            default:
                return XMS_ERR_ALLOCATION_FAILED;
        }
    }
    
    /* Store handle information */
    xms_handles[i].handle = ax_val;
    xms_handles[i].size_kb = size_kb;
    xms_handles[i].in_use = 1;
    xms_handles[i].locked = 0;
    xms_handles[i].lock_count = 0;
    xms_handles[i].linear_address = 0;
    num_handles++;
    
    /* Update available memory */
    xms_info.free_kb -= size_kb;
    
    *handle = ax_val;
    
    log_info("Allocated XMS handle %04X, size %d KB", ax_val, size_kb);
    return 0;
}

/**
 * @brief Free XMS memory block
 * @param handle XMS handle to free
 * @return 0 on success, negative on error
 */
int xms_free(uint16_t handle) {
    uint8_t bl_val;
    int i;
    
    if (!xms_available) {
        return XMS_ERR_NOT_AVAILABLE;
    }
    
    log_debug("Freeing XMS handle %04X", handle);
    
    /* Find handle in our table */
    for (i = 0; i < XMS_MAX_HANDLES; i++) {
        if (xms_handles[i].in_use && xms_handles[i].handle == handle) {
            break;
        }
    }
    
    if (i >= XMS_MAX_HANDLES) {
        log_error("XMS handle %04X not found", handle);
        return XMS_ERR_INVALID_HANDLE;
    }
    
    /* Unlock if locked */
    if (xms_handles[i].locked) {
        xms_unlock(handle);
    }
    
    /* Call XMS free function */
    if (!xms_call_extended(XMS_FREE_EXTENDED, handle, NULL, NULL, &bl_val)) {
        log_error("XMS free failed for handle %04X (error %02Xh)", handle, bl_val);
        return XMS_ERR_FUNCTION_FAILED;
    }
    
    /* Update our records */
    xms_info.free_kb += xms_handles[i].size_kb;
    memset(&xms_handles[i], 0, sizeof(xms_handle_t));
    num_handles--;
    
    log_info("Freed XMS handle %04X", handle);
    return 0;
}

/**
 * @brief Lock XMS memory block
 * @param handle XMS handle
 * @param linear_address Pointer to store linear address
 * @return 0 on success, negative on error
 */
int xms_lock(uint16_t handle, uint32_t *linear_address) {
    uint16_t ax_val, dx_val;
    uint8_t bl_val;
    int i;
    
    if (!linear_address) {
        return XMS_ERR_INVALID_PARAM;
    }
    
    if (!xms_available) {
        return XMS_ERR_NOT_AVAILABLE;
    }
    
    log_debug("Locking XMS handle %04X", handle);
    
    /* Find handle */
    for (i = 0; i < XMS_MAX_HANDLES; i++) {
        if (xms_handles[i].in_use && xms_handles[i].handle == handle) {
            break;
        }
    }
    
    if (i >= XMS_MAX_HANDLES) {
        return XMS_ERR_INVALID_HANDLE;
    }
    
    /* Call XMS lock function */
    if (!xms_call_extended(XMS_LOCK_EXTENDED, handle, &ax_val, &dx_val, &bl_val)) {
        log_error("XMS lock failed for handle %04X (error %02Xh)", handle, bl_val);
        switch (bl_val) {
            case XMS_INVALID_HANDLE:
                return XMS_ERR_INVALID_HANDLE;
            case XMS_BLOCK_LOCKED:
                return XMS_ERR_NOT_LOCKED;
            case XMS_LOCK_COUNT_OVERFLOW:
                log_warning("XMS lock count overflow for handle %04X", handle);
                return XMS_ERR_FUNCTION_FAILED;
            default:
                return XMS_ERR_FUNCTION_FAILED;
        }
    }
    
    /* XMS lock returns linear address in DX:BX (BX in AX after call) */
    *linear_address = ((uint32_t)dx_val << 16) | ax_val;
    
    xms_handles[i].locked = 1;
    xms_handles[i].lock_count++;
    xms_handles[i].linear_address = *linear_address;
    
    log_debug("Locked XMS handle %04X at linear address %08lX", 
             handle, *linear_address);
    
    return 0;
}

/**
 * @brief Unlock XMS memory block
 * @param handle XMS handle
 * @return 0 on success, negative on error
 */
int xms_unlock(uint16_t handle) {
    uint8_t bl_val;
    int i;
    
    if (!xms_available) {
        return XMS_ERR_NOT_AVAILABLE;
    }
    
    log_debug("Unlocking XMS handle %04X", handle);
    
    /* Find handle */
    for (i = 0; i < XMS_MAX_HANDLES; i++) {
        if (xms_handles[i].in_use && xms_handles[i].handle == handle) {
            break;
        }
    }
    
    if (i >= XMS_MAX_HANDLES) {
        return XMS_ERR_INVALID_HANDLE;
    }
    
    if (!xms_handles[i].locked) {
        log_warning("XMS handle %04X not locked", handle);
        return XMS_ERR_NOT_LOCKED;
    }
    
    /* Call XMS unlock function */
    if (!xms_call_extended(XMS_UNLOCK_EXTENDED, handle, NULL, NULL, &bl_val)) {
        log_error("XMS unlock failed for handle %04X (error %02Xh)", handle, bl_val);
        switch (bl_val) {
            case XMS_INVALID_HANDLE:
                return XMS_ERR_INVALID_HANDLE;
            case XMS_BLOCK_NOT_LOCKED:
                return XMS_ERR_NOT_LOCKED;
            default:
                return XMS_ERR_FUNCTION_FAILED;
        }
    }
    
    xms_handles[i].locked = 0;
    xms_handles[i].lock_count--;
    xms_handles[i].linear_address = 0;
    
    log_debug("Unlocked XMS handle %04X", handle);
    return 0;
}

/**
 * @brief Get XMS information
 * @param info Pointer to store XMS information
 * @return 0 on success, negative on error
 */
int xms_get_info(xms_info_t *info) {
    if (!info) {
        return XMS_ERR_INVALID_PARAM;
    }
    
    if (!xms_available) {
        return XMS_ERR_NOT_AVAILABLE;
    }
    
    *info = xms_info;
    return 0;
}

/**
 * @brief Check if XMS is available
 * @return 1 if available, 0 if not
 */
int xms_is_available(void) {
    return xms_available;
}

/**
 * @brief Move data between XMS and conventional memory
 * @param dest_handle Destination XMS handle (0 for conventional memory)
 * @param dest_offset Offset in destination
 * @param src_handle Source XMS handle (0 for conventional memory)
 * @param src_offset Offset in source
 * @param length Number of bytes to move
 * @return 0 on success, negative on error
 */
int xms_move_memory(uint16_t dest_handle, uint32_t dest_offset,
                   uint16_t src_handle, uint32_t src_offset, uint32_t length) {
    struct {
        uint32_t length;
        uint16_t src_handle;
        uint32_t src_offset;
        uint16_t dest_handle;
        uint32_t dest_offset;
    } PACKED move_params;
    
    uint8_t bl_val;
    
    if (!xms_available) {
        return XMS_ERR_NOT_AVAILABLE;
    }
    
    if (length == 0) {
        return 0; /* Nothing to move */
    }
    
    /* Set up move parameters structure */
    move_params.length = length;
    move_params.src_handle = src_handle;
    move_params.src_offset = src_offset;
    move_params.dest_handle = dest_handle;
    move_params.dest_offset = dest_offset;
    
    /* Call XMS move function with DS:SI pointing to parameters */
    /* Note: This requires more complex assembly for the parameter block */
    union REGS regs;
    struct SREGS sregs;
    
    regs.h.ah = XMS_MOVE_EXTENDED;
    sregs.ds = FP_SEG(&move_params);
    regs.x.si = FP_OFF(&move_params);
    
    /* Call XMS driver through far function pointer */
#ifdef __WATCOMC__
    _asm {
        push ds
        mov ah, XMS_MOVE_EXTENDED
        lds si, dword ptr move_params
        call dword ptr xms_entry_point
        pop ds
        mov byte ptr bl_val, bl
        mov word ptr regs.x.ax, ax
    }
#elif defined(__TURBOC__)
    asm {
        push ds
        mov ah, XMS_MOVE_EXTENDED
        lds si, move_params
        call dword ptr xms_entry_point
        pop ds
        mov bl_val, bl
        mov regs.x.ax, ax
    }
#else
    /* GCC/DJGPP inline assembly for XMS move */
    asm volatile (
        "push %%ds\n\t"
        "movb %4, %%ah\n\t"
        "movw %5, %%ds\n\t"
        "movw %6, %%si\n\t"
        "lcall *%7\n\t"
        "pop %%ds\n\t"
        "movw %%ax, %0\n\t"
        "movb %%bl, %1\n\t"
        : "=m" (regs.x.ax), "=m" (bl_val)
        : "m" (move_params), "m" (move_params), "i" (XMS_MOVE_EXTENDED), 
          "m" (sregs.ds), "m" (regs.x.si), "m" (xms_entry_point)
        : "ax", "bx", "ds", "si"
    );
#endif
    
    if (regs.x.ax == 0) {
        log_error("XMS move failed (error %02Xh)", bl_val);
        return XMS_ERR_FUNCTION_FAILED;
    }
    
    return 0;
}

/**
 * @brief Cleanup XMS resources
 * @return 0 on success, negative on error
 */
int xms_cleanup(void) {
    int i, result = 0;
    
    if (!xms_available) {
        return 0;
    }
    
    log_info("Cleaning up XMS resources");
    
    /* Free all allocated handles */
    for (i = 0; i < XMS_MAX_HANDLES; i++) {
        if (xms_handles[i].in_use) {
            log_warning("Freeing unreleased XMS handle %04X", 
                       xms_handles[i].handle);
            result = xms_free(xms_handles[i].handle);
            if (result < 0) {
                log_error("Failed to free XMS handle %04X: %d", 
                         xms_handles[i].handle, result);
            }
        }
    }
    
    xms_available = 0;
    xms_entry_point = NULL;
    memset(&xms_info, 0, sizeof(xms_info));
    memset(xms_handles, 0, sizeof(xms_handles));
    num_handles = 0;
    
    log_info("XMS cleanup completed");
    return result;
}


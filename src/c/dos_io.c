/**
 * @file dos_io.c
 * @brief Custom stdio replacement using DOS INT 21h (no C library)
 *
 * All I/O goes through DOS INT 21h system calls. No C library stdio
 * functions are used, saving ~40KB from the ROOT segment.
 *
 * Updated: 2026-02-01 18:20:35 CET
 */

#include "dos_io.h"
#include <dos.h>

/*
 * USE_TSR_CRT: When defined, use the pure-ASM CRT replacements from
 * tsr_crt.asm instead of Watcom's string.h functions. This allows
 * dos_io.c to be compiled for the two-stage loader's resident image
 * without pulling in the Watcom CRT.
 */
#ifdef USE_TSR_CRT
extern size_t   tsr_strlen(const char *s);
extern void far *tsr_fmemcpy(void far *dst, const void far *src, size_t n);
#define strlen   tsr_strlen
#define _fmemcpy tsr_fmemcpy
#else
#include <string.h>
#endif

/* Internal formatting buffer for printf/fprintf output */
static char _io_buf[512];

/* ============================================================================
 * Internal helpers
 * ============================================================================ */

/**
 * @brief Convert unsigned long to string in given base
 */
static char *ulong_to_str(unsigned long val, char *buf, int base, int uppercase)
{
    char *p = buf;
    char *start;
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    /* Build string in reverse */
    do {
        *p++ = digits[val % (unsigned long)base];
        val /= (unsigned long)base;
    } while (val > 0);

    *p = '\0';

    /* Reverse the string */
    start = buf;
    p--;
    while (start < p) {
        char tmp = *start;
        *start++ = *p;
        *p-- = tmp;
    }

    return buf;
}

/**
 * @brief Convert signed long to string (base 10 only)
 */
static char *long_to_str(long val, char *buf)
{
    if (val < 0) {
        buf[0] = '-';
        ulong_to_str((unsigned long)(-val), buf + 1, 10, 0);
    } else {
        ulong_to_str((unsigned long)val, buf, 10, 0);
    }
    return buf;
}

/**
 * @brief Write bytes to a DOS file handle using INT 21h AH=40h
 * @return Number of bytes written, or -1 on error
 */
static int dos_write_handle(int handle, const char *data, int len)
{
    union REGS regs;
    struct SREGS sregs;

    if (len <= 0) return 0;

    regs.h.ah = 0x40;
    regs.w.bx = (unsigned short)handle;
    regs.w.cx = (unsigned short)len;
    regs.w.dx = FP_OFF(data);
    sregs.ds  = FP_SEG(data);

    int86x(0x21, &regs, &regs, &sregs);

    if (regs.w.cflag & 1) return -1;
    return (int)regs.w.ax;
}

/**
 * @brief Read bytes from a DOS file handle using INT 21h AH=3Fh
 * @return Number of bytes read, or -1 on error
 */
static int dos_read_handle(int handle, void *data, int len)
{
    union REGS regs;
    struct SREGS sregs;

    if (len <= 0) return 0;

    regs.h.ah = 0x3F;
    regs.w.bx = (unsigned short)handle;
    regs.w.cx = (unsigned short)len;
    regs.w.dx = FP_OFF(data);
    sregs.ds  = FP_SEG(data);

    int86x(0x21, &regs, &regs, &sregs);

    if (regs.w.cflag & 1) return -1;
    return (int)regs.w.ax;
}

/* ============================================================================
 * Core formatter: dos_vsnprintf
 * ============================================================================ */

/**
 * @brief Minimal vsnprintf supporting %s %d %u %x %X %ld %lu %lx %lX %c %%
 *        Also supports zero-padded width: %02x %04x %02X %04X etc.
 */
int dos_vsnprintf(char *buf, int size, const char *fmt, va_list args)
{
    int pos = 0;
    const char *p = fmt;
    int zero_pad;
    int width;
    int is_long;

    if (size <= 0) return 0;

    while (*p && pos < size - 1) {
        if (*p != '%') {
            buf[pos++] = *p++;
            continue;
        }

        p++;  /* skip '%' */
        if (!*p) break;

        /* Parse flags */
        zero_pad = 0;
        width = 0;

        if (*p == '0') {
            zero_pad = 1;
            p++;
        }

        /* Parse width */
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        if (!*p) break;

        /* Check for 'l' length modifier */
        is_long = 0;
        if (*p == 'l') {
            is_long = 1;
            p++;
            if (!*p) break;
        }

        /* Format specifier */
        switch (*p) {
            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                while (*s && pos < size - 1) {
                    buf[pos++] = *s++;
                }
                break;
            }

            case 'd': {
                char numbuf[24];
                if (is_long) {
                    long_to_str(va_arg(args, long), numbuf);
                } else {
                    long_to_str((long)va_arg(args, int), numbuf);
                }
                /* Apply width/zero-pad */
                {
                    int nlen = (int)strlen(numbuf);
                    int pad = (width > nlen) ? width - nlen : 0;
                    char padch = zero_pad ? '0' : ' ';
                    while (pad-- > 0 && pos < size - 1)
                        buf[pos++] = padch;
                    {
                        char *n = numbuf;
                        while (*n && pos < size - 1)
                            buf[pos++] = *n++;
                    }
                }
                break;
            }

            case 'u': {
                char numbuf[24];
                if (is_long) {
                    ulong_to_str(va_arg(args, unsigned long), numbuf, 10, 0);
                } else {
                    ulong_to_str((unsigned long)va_arg(args, unsigned int), numbuf, 10, 0);
                }
                {
                    int nlen = (int)strlen(numbuf);
                    int pad = (width > nlen) ? width - nlen : 0;
                    char padch = zero_pad ? '0' : ' ';
                    while (pad-- > 0 && pos < size - 1)
                        buf[pos++] = padch;
                    {
                        char *n = numbuf;
                        while (*n && pos < size - 1)
                            buf[pos++] = *n++;
                    }
                }
                break;
            }

            case 'x':
            case 'X': {
                char numbuf[24];
                int uppercase = (*p == 'X');
                if (is_long) {
                    ulong_to_str(va_arg(args, unsigned long), numbuf, 16, uppercase);
                } else {
                    ulong_to_str((unsigned long)va_arg(args, unsigned int), numbuf, 16, uppercase);
                }
                {
                    int nlen = (int)strlen(numbuf);
                    int pad = (width > nlen) ? width - nlen : 0;
                    char padch = zero_pad ? '0' : ' ';
                    while (pad-- > 0 && pos < size - 1)
                        buf[pos++] = padch;
                    {
                        char *n = numbuf;
                        while (*n && pos < size - 1)
                            buf[pos++] = *n++;
                    }
                }
                break;
            }

            case 'c': {
                char ch = (char)va_arg(args, int);
                buf[pos++] = ch;
                break;
            }

            case '%':
                buf[pos++] = '%';
                break;

            default:
                /* Unknown specifier, output literally */
                buf[pos++] = '%';
                if (pos < size - 1) buf[pos++] = *p;
                break;
        }

        p++;
    }

    buf[pos] = '\0';
    return pos;
}

/* ============================================================================
 * String formatting functions
 * ============================================================================ */

int dos_vsprintf(char *buf, const char *fmt, va_list args)
{
    return dos_vsnprintf(buf, 32767, fmt, args);
}

int dos_sprintf(char *buf, const char *fmt, ...)
{
    va_list args;
    int ret;
    va_start(args, fmt);
    ret = dos_vsprintf(buf, fmt, args);
    va_end(args);
    return ret;
}

int dos_snprintf(char *buf, int size, const char *fmt, ...)
{
    va_list args;
    int ret;
    va_start(args, fmt);
    ret = dos_vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}

/* ============================================================================
 * Console / file output functions
 * ============================================================================ */

int dos_vprintf(const char *fmt, va_list args)
{
    int len;
    len = dos_vsnprintf(_io_buf, sizeof(_io_buf), fmt, args);
    if (len > 0) {
        dos_write_handle(DOS_STDOUT, _io_buf, len);
    }
    return len;
}

int dos_printf(const char *fmt, ...)
{
    va_list args;
    int ret;
    va_start(args, fmt);
    ret = dos_vprintf(fmt, args);
    va_end(args);
    return ret;
}

int dos_fprintf(dos_file_t handle, const char *fmt, ...)
{
    va_list args;
    int len;
    va_start(args, fmt);
    len = dos_vsnprintf(_io_buf, sizeof(_io_buf), fmt, args);
    va_end(args);
    if (len > 0) {
        dos_write_handle(handle, _io_buf, len);
    }
    return len;
}

/* ============================================================================
 * File operations via DOS INT 21h
 * ============================================================================ */

/**
 * @brief Open a file using DOS INT 21h
 * @param mode "r" = read, "w" = write/create, "a" = append
 * @return DOS file handle, or -1 on error
 */
dos_file_t dos_fopen(const char *filename, const char *mode)
{
    union REGS regs;
    struct SREGS sregs;

    if (!filename || !mode) return -1;

    if (mode[0] == 'r') {
        /* INT 21h AH=3Dh: Open existing file */
        regs.h.ah = 0x3D;
        regs.h.al = 0x00;  /* read-only */
        regs.w.dx = FP_OFF(filename);
        sregs.ds  = FP_SEG(filename);

        int86x(0x21, &regs, &regs, &sregs);

        if (regs.w.cflag & 1) return -1;
        return (dos_file_t)regs.w.ax;

    } else if (mode[0] == 'w') {
        /* INT 21h AH=3Ch: Create / truncate file */
        regs.h.ah = 0x3C;
        regs.w.cx = 0x0000;  /* normal attributes */
        regs.w.dx = FP_OFF(filename);
        sregs.ds  = FP_SEG(filename);

        int86x(0x21, &regs, &regs, &sregs);

        if (regs.w.cflag & 1) return -1;
        return (dos_file_t)regs.w.ax;

    } else if (mode[0] == 'a') {
        /* Open existing for write, then seek to end */
        regs.h.ah = 0x3D;
        regs.h.al = 0x01;  /* write-only */
        regs.w.dx = FP_OFF(filename);
        sregs.ds  = FP_SEG(filename);

        int86x(0x21, &regs, &regs, &sregs);

        if (regs.w.cflag & 1) {
            /* File doesn't exist, create it */
            regs.h.ah = 0x3C;
            regs.w.cx = 0x0000;
            regs.w.dx = FP_OFF(filename);
            sregs.ds  = FP_SEG(filename);

            int86x(0x21, &regs, &regs, &sregs);

            if (regs.w.cflag & 1) return -1;
            return (dos_file_t)regs.w.ax;
        }

        /* Seek to end: INT 21h AH=42h, AL=02 (from end) */
        {
            dos_file_t handle = (dos_file_t)regs.w.ax;
            regs.h.ah = 0x42;
            regs.h.al = 0x02;  /* seek from end */
            regs.w.bx = (unsigned short)handle;
            regs.w.cx = 0;
            regs.w.dx = 0;

            int86(0x21, &regs, &regs);

            return handle;
        }
    }

    return -1;
}

/**
 * @brief Close a DOS file handle using INT 21h AH=3Eh
 */
int dos_fclose(dos_file_t handle)
{
    union REGS regs;

    if (handle < 0) return -1;

    regs.h.ah = 0x3E;
    regs.w.bx = (unsigned short)handle;

    int86(0x21, &regs, &regs);

    if (regs.w.cflag & 1) return -1;
    return 0;
}

/**
 * @brief Write to a DOS file handle
 * @return Number of items written
 */
int dos_fwrite(const void *buf, int size, int count, dos_file_t handle)
{
    int total = size * count;
    int written;

    if (total <= 0 || handle < 0) return 0;

    written = dos_write_handle(handle, (const char *)buf, total);
    if (written < 0) return 0;
    return written / size;
}

/**
 * @brief Read from a DOS file handle
 * @return Number of items read
 */
int dos_fread(void *buf, int size, int count, dos_file_t handle)
{
    int total = size * count;
    int nread;

    if (total <= 0 || handle < 0) return 0;

    nread = dos_read_handle(handle, buf, total);
    if (nread < 0) return 0;
    return nread / size;
}

/**
 * @brief Flush - no-op for DOS (writes are synchronous)
 */
int dos_fflush(dos_file_t handle)
{
    (void)handle;
    return 0;
}

/* ============================================================================
 * Console input
 * ============================================================================ */

/**
 * @brief Read a single character from stdin using DOS INT 21h AH=01h
 */
int dos_getchar(void)
{
    unsigned char ch;

    _asm {
        mov ah, 01h
        int 21h
        mov ch, al
    }

    return (int)ch;
}

/* ============================================================================
 * String-to-number conversions
 * ============================================================================ */

/**
 * @brief Convert decimal string to int
 */
int dos_atoi(const char *s)
{
    int result = 0;
    int neg = 0;

    if (!s) return 0;

    while (*s == ' ' || *s == '\t') s++;

    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }

    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }

    return neg ? -result : result;
}

/**
 * @brief Convert decimal string to unsigned long
 */
unsigned long dos_atoul(const char *s)
{
    unsigned long result = 0;

    if (!s) return 0;

    while (*s == ' ' || *s == '\t') s++;
    if (*s == '+') s++;

    while (*s >= '0' && *s <= '9') {
        result = result * 10UL + (unsigned long)(*s - '0');
        s++;
    }

    return result;
}

/**
 * @brief Convert hex string to unsigned long (with optional 0x prefix)
 */
unsigned long dos_hextoul(const char *s)
{
    unsigned long result = 0;

    if (!s) return 0;

    while (*s == ' ' || *s == '\t') s++;

    /* Skip optional 0x prefix */
    if (*s == '0' && (*(s+1) == 'x' || *(s+1) == 'X')) {
        s += 2;
    }

    while (1) {
        if (*s >= '0' && *s <= '9') {
            result = (result << 4) | (unsigned long)(*s - '0');
        } else if (*s >= 'a' && *s <= 'f') {
            result = (result << 4) | (unsigned long)(*s - 'a' + 10);
        } else if (*s >= 'A' && *s <= 'F') {
            result = (result << 4) | (unsigned long)(*s - 'A' + 10);
        } else {
            break;
        }
        s++;
    }

    return result;
}

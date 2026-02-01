/**
 * @file dos_io.h
 * @brief Custom stdio replacement using DOS INT 21h (no C library)
 *
 * Replaces printf, sprintf, snprintf, fprintf, fopen, fclose, fread, fwrite
 * with custom implementations that don't pull in the C library (~40KB savings).
 *
 * Updated: 2026-01-29 23:25:08 CET
 */

#ifndef DOS_IO_H
#define DOS_IO_H

#include <stdarg.h>

/* DOS file handle type (replaces FILE*) */
typedef int dos_file_t;

/* Standard handles */
#define DOS_STDOUT  1
#define DOS_STDERR  2

/* File open modes */
#define DOS_READ    0
#define DOS_WRITE   1
#define DOS_RDWR    2

/* Console output */
int dos_printf(const char *fmt, ...);
int dos_vprintf(const char *fmt, va_list args);

/* String formatting */
int dos_sprintf(char *buf, const char *fmt, ...);
int dos_snprintf(char *buf, int size, const char *fmt, ...);
int dos_vsprintf(char *buf, const char *fmt, va_list args);
int dos_vsnprintf(char *buf, int size, const char *fmt, va_list args);

/* File output */
int dos_fprintf(dos_file_t handle, const char *fmt, ...);

/* File operations */
dos_file_t dos_fopen(const char *filename, const char *mode);
int dos_fclose(dos_file_t handle);
int dos_fwrite(const void *buf, int size, int count, dos_file_t handle);
int dos_fread(void *buf, int size, int count, dos_file_t handle);
int dos_fflush(dos_file_t handle);

/* Console input */
int dos_getchar(void);

/* Simple string-to-int (replaces sscanf for simple cases) */
int dos_atoi(const char *s);
unsigned long dos_atoul(const char *s);
unsigned long dos_hextoul(const char *s);

/* Compatibility macros - map stdio names to dos_io names */
/* These can be used after removing #include <stdio.h> */
#define printf dos_printf
#define sprintf dos_sprintf
#define snprintf dos_snprintf
#define vsnprintf dos_vsnprintf
#define vsprintf dos_vsprintf
#define fprintf(f, ...) dos_fprintf((dos_file_t)(f), __VA_ARGS__)

#define getchar dos_getchar

/* Note: fopen/fclose/fread/fwrite need explicit conversion since FILE* vs dos_file_t */

#endif /* DOS_IO_H */

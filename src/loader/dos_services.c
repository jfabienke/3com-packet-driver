/**
 * @file dos_services.c
 * @brief DOS System Services Implementation
 * 
 * Provides DOS-specific services like InDOS checking and
 * configuration file handling for TSR context.
 */

#include <dos.h>
#include <i86.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include "dos_services.h"

/**
 * @brief Check if DOS is busy (unsafe to call INT 21h)
 * 
 * @return 1 if DOS is busy, 0 if safe to make DOS calls
 */
int dos_busy(void)
{
    union REGS in, out;
    struct SREGS segs;
    uint8_t __far *indos;

    in.h.ah = 0x34;          /* Get InDOS pointer */
    int86x(0x21, &in, &out, &segs);

    indos = (uint8_t __far *)MK_FP(segs.es, out.x.bx);
    return *indos != 0;
}

/**
 * @brief Parse key=value line from configuration file
 * 
 * @param line Input line to parse
 * @param key Output buffer for key (must be at least 32 bytes)
 * @param val Output buffer for value (must be at least 96 bytes)
 * @return 1 if parsing successful, 0 if invalid line
 */
int parse_config_line(const char *line, char *key, char *val)
{
    const char *p = line;
    const char *eq;
    size_t klen;
    
    /* Skip whitespace */
    while (isspace((unsigned char)*p)) ++p;
    
    /* Skip comments and empty lines */
    if (*p == 0 || *p == ';' || *p == '#') return 0;
    
    /* Find equals sign */
    eq = strchr(p, '=');
    if (!eq) return 0;
    
    /* Extract key */
    klen = eq - p;
    while (klen && isspace((unsigned char)p[klen-1])) --klen;
    if (klen == 0 || klen >= 32) return 0;
    
    strncpy(key, p, klen); 
    key[klen] = 0;
    
    /* Extract value */
    ++eq; /* Skip '=' */
    while (*eq && isspace((unsigned char)*eq)) ++eq;
    
    /* Copy value and trim trailing whitespace */
    strncpy(val, eq, 95);
    val[95] = 0;
    
    /* Remove trailing whitespace */
    char *end = val + strlen(val) - 1;
    while (end > val && isspace((unsigned char)*end)) {
        *end = 0;
        --end;
    }
    
    return 1;
}

/**
 * @brief Convert string value to boolean
 * 
 * @param str String to convert ("yes", "no", "true", "false", "1", "0")
 * @return 1 for true values, 0 for false values
 */
int string_to_bool(const char *str)
{
    if (!str) return 0;
    
    if (!stricmp(str, "yes") || !stricmp(str, "true") || 
        !stricmp(str, "on") || !stricmp(str, "1")) {
        return 1;
    }
    
    return 0;
}

/**
 * @brief Load configuration from DOS-style config file
 * 
 * @param filename Configuration file name
 * @param config_handler Callback function to process each key=value pair
 * @param user_data User data passed to callback
 * @return Number of parameters loaded, negative on error
 */
int load_dos_config_file(const char *filename, 
                        config_line_handler_t config_handler,
                        void *user_data)
{
    FILE *f;
    char line[128], key[32], val[96];
    int loaded_count = 0;
    int line_number = 0;

    if (dos_busy()) {
        return ERROR_DOS_BUSY;
    }

    f = fopen(filename, "rt");
    if (!f) {
        return ERROR_FILE_NOT_FOUND;
    }

    while (fgets(line, sizeof(line), f)) {
        line_number++;
        
        if (!parse_config_line(line, key, val)) {
            continue; /* Skip comments and invalid lines */
        }
        
        /* Call handler to process this key=value pair */
        if (config_handler(key, val, line_number, user_data) == SUCCESS) {
            loaded_count++;
        }
    }
    
    fclose(f);
    return loaded_count;
}
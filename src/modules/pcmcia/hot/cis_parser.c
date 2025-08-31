/**
 * @file cis_parser.c
 * @brief CIS (Card Information Structure) parsing for 3Com PCMCIA cards
 *
 * Optimized CIS parser that only handles tuples needed for 3Com network cards.
 * Provides significant memory savings over full CIS parsers.
 */

#include <string.h>
#include "../include/pcmcia_internal.h"

/* Supported 3Com card signatures */
const cis_signature_t supported_3com_cards[] = {
    /* 3C589 PCMCIA series - 16-bit PCMCIA */
    {0x0101, 0x0589, "3Com EtherLink III", CARD_3C589},
    {0x0101, 0x058A, "3Com 3C589B", CARD_3C589B},
    {0x0101, 0x058B, "3Com 3C589C", CARD_3C589C},
    {0x0101, 0x058C, "3Com 3C589D", CARD_3C589D},
    
    /* 3C562 LAN+Modem combo cards */
    {0x0101, 0x0562, "3Com 3C562", CARD_3C562},
    {0x0101, 0x0563, "3Com 3C562B", CARD_3C562B},
    
    /* 3C574 Fast EtherLink PCMCIA */
    {0x0101, 0x0574, "3Com 3C574-TX", CARD_3C574},
    
    /* 3C575 CardBus series - 32-bit CardBus */
    {0x0101, 0x5157, "3Com 3C575-TX", CARD_3C575},
    {0x0101, 0x515A, "3Com 3C575C-TX", CARD_3C575C},
    
    /* End marker */
    {0, 0, NULL, CARD_UNKNOWN}
};

/**
 * @brief Parse CIS for 3Com cards only
 * @param socket Socket number
 * @param info Pointer to structure to fill with parsed information
 * @return Card type if successful, negative error code otherwise
 */
int parse_3com_cis(uint8_t socket, cis_3com_info_t *info) {
    uint8_t *cis_base;
    tuple_header_t *tuple;
    uint16_t offset = 0;
    int result = PCMCIA_ERR_CIS_PARSE;
    
    if (!info) {
        return PCMCIA_ERR_INVALID_PARAM;
    }
    
    /* Clear info structure */
    memset(info, 0, sizeof(cis_3com_info_t));
    
    /* Map CIS memory (attribute memory at offset 0) */
    cis_base = map_attribute_memory(socket, 0, 512);
    if (!cis_base) {
        log_error("Failed to map CIS memory for socket %d", socket);
        return PCMCIA_ERR_HARDWARE;
    }
    
    log_debug("Parsing CIS for socket %d", socket);
    
    /* Parse essential tuples only */
    while (offset < 512) {
        tuple = (tuple_header_t*)(cis_base + offset);
        
        /* Check for end of CIS */
        if (tuple->type == CISTPL_END) {
            break;
        }
        
        /* Skip null tuples */
        if (tuple->type == CISTPL_NULL) {
            offset += 1;
            continue;
        }
        
        /* Validate tuple length */
        if (offset + tuple->length + 2 > 512) {
            log_error("Invalid tuple length at offset %d", offset);
            break;
        }
        
        /* Parse relevant tuples */
        switch (tuple->type) {
            case CISTPL_MANFID:
                if (parse_manufacturer_id(tuple, info) < 0) {
                    log_error("Failed to parse manufacturer ID");
                    goto cleanup;
                }
                log_debug("Manufacturer ID: %04X, Product ID: %04X", 
                         info->manufacturer_id, info->product_id);
                break;
                
            case CISTPL_VERS_1:
                if (parse_version_string(tuple, info) < 0) {
                    log_debug("Failed to parse version string (non-critical)");
                }
                break;
                
            case CISTPL_FUNCID:
                if (parse_function_id(tuple, info) < 0) {
                    log_error("Failed to parse function ID");
                    goto cleanup;
                }
                if (info->function_type != CISTPL_FUNCID_NETWORK) {
                    log_error("Not a network interface card (function type: %02X)", 
                             info->function_type);
                    result = PCMCIA_ERR_UNSUPPORTED;
                    goto cleanup;
                }
                break;
                
            case CISTPL_CONFIG:
                if (parse_config_base(tuple, info) < 0) {
                    log_error("Failed to parse configuration base");
                    goto cleanup;
                }
                break;
                
            case CISTPL_CFTABLE_ENTRY:
                if (parse_config_entry(tuple, info) < 0) {
                    log_debug("Failed to parse config entry at offset %d", offset);
                }
                break;
        }
        
        offset += tuple->length + 2;
    }
    
    /* Validate that this is a supported 3Com card */
    result = validate_3com_card(info);
    if (result < 0) {
        goto cleanup;
    }
    
    log_info("Detected %s in socket %d", 
             card_type_name(info->card_type), socket);
    
cleanup:
    unmap_attribute_memory(cis_base);
    return result;
}

/**
 * @brief Parse manufacturer ID tuple
 */
static int parse_manufacturer_id(tuple_header_t *tuple, cis_3com_info_t *info) {
    cistpl_manfid_t *manfid;
    
    if (tuple->length < sizeof(cistpl_manfid_t)) {
        return -1;
    }
    
    manfid = (cistpl_manfid_t*)tuple->data;
    info->manufacturer_id = manfid->manufacturer_id;
    info->product_id = manfid->product_id;
    
    return 0;
}

/**
 * @brief Parse version string tuple
 */
static int parse_version_string(tuple_header_t *tuple, cis_3com_info_t *info) {
    uint8_t *data = tuple->data;
    uint8_t major_version, minor_version;
    int offset = 0;
    int str_count = 0;
    
    if (tuple->length < 2) {
        return -1;
    }
    
    /* Get version numbers */
    major_version = data[offset++];
    minor_version = data[offset++];
    
    /* Parse strings */
    while (offset < tuple->length && str_count < 4) {
        int str_len = strlen((char*)(data + offset));
        
        if (str_count == 1) {
            /* Second string is usually the product name */
            strncpy(info->product_name, (char*)(data + offset), 
                   sizeof(info->product_name) - 1);
            info->product_name[sizeof(info->product_name) - 1] = '\0';
        }
        
        offset += str_len + 1;
        str_count++;
    }
    
    return 0;
}

/**
 * @brief Parse function ID tuple
 */
static int parse_function_id(tuple_header_t *tuple, cis_3com_info_t *info) {
    cistpl_funcid_t *funcid;
    
    if (tuple->length < sizeof(cistpl_funcid_t)) {
        return -1;
    }
    
    funcid = (cistpl_funcid_t*)tuple->data;
    info->function_type = funcid->function_type;
    
    return 0;
}

/**
 * @brief Parse configuration base tuple
 */
static int parse_config_base(tuple_header_t *tuple, cis_3com_info_t *info) {
    cistpl_config_t *config;
    
    if (tuple->length < sizeof(cistpl_config_t)) {
        return -1;
    }
    
    config = (cistpl_config_t*)tuple->data;
    /* Configuration base parsing for 3Com cards is simplified */
    /* We mainly need to know the configuration is present */
    
    return 0;
}

/**
 * @brief Parse configuration table entry
 */
static int parse_config_entry(tuple_header_t *tuple, cis_3com_info_t *info) {
    uint8_t *data = tuple->data;
    config_entry_t *config;
    int offset = 0;
    uint8_t index, interface_type, feature_selection;
    
    if (tuple->length < 2 || info->config_count >= 4) {
        return -1;  /* Skip if no space or too short */
    }
    
    config = &info->configs[info->config_count];
    
    /* Parse configuration index */
    index = data[offset++];
    config->index = index & 0x3F;
    
    /* Check if interface type is present */
    if (index & 0x40) {
        if (offset >= tuple->length) return -1;
        interface_type = data[offset++];
        config->interface_type = interface_type;
    }
    
    /* Check if feature selection is present */
    if (index & 0x80) {
        if (offset >= tuple->length) return -1;
        feature_selection = data[offset++];
        config->feature_selection = feature_selection;
    }
    
    /* Parse I/O information if present */
    if (feature_selection & 0x08) {
        offset += parse_io_ranges(data + offset, tuple->length - offset, config);
    }
    
    /* Parse IRQ information if present */
    if (feature_selection & 0x04) {
        offset += parse_irq_info(data + offset, tuple->length - offset, config);
    }
    
    /* Parse memory information if present */
    if (feature_selection & 0x03) {
        offset += parse_memory_info(data + offset, tuple->length - offset, config);
    }
    
    info->config_count++;
    
    log_debug("Config %d: I/O=0x%04X-0x%04X, IRQ mask=0x%02X", 
             config->index, config->io_base, 
             config->io_base + config->io_size - 1,
             config->irq_mask);
    
    return 0;
}

/**
 * @brief Parse I/O range information
 */
static int parse_io_ranges(uint8_t *data, int max_len, config_entry_t *config) {
    int offset = 0;
    uint8_t io_info;
    
    if (max_len < 1) return 0;
    
    io_info = data[offset++];
    
    /* Check if I/O ranges are present */
    if ((io_info & 0x80) == 0) {
        /* No I/O ranges */
        return offset;
    }
    
    config->io_ranges = (io_info & 0x0F) + 1;
    
    /* For 3Com cards, we typically have simple I/O ranges */
    if (config->io_ranges > 0 && offset + 3 < max_len) {
        /* Read base address (assuming 16-bit) */
        config->io_base = *(uint16_t*)(data + offset);
        offset += 2;
        
        /* Read size (assuming 8-bit) */
        config->io_size = data[offset++];
        if (config->io_size == 0) {
            config->io_size = 16;  /* Default for 3Com cards */
        }
    } else {
        /* Use defaults for 3Com cards */
        config->io_base = 0x300;
        config->io_size = 16;
    }
    
    return offset;
}

/**
 * @brief Parse IRQ information
 */
static int parse_irq_info(uint8_t *data, int max_len, config_entry_t *config) {
    int offset = 0;
    uint8_t irq_info;
    
    if (max_len < 1) return 0;
    
    irq_info = data[offset++];
    
    if (irq_info & 0x10) {
        /* Extended IRQ mask follows */
        if (offset + 1 < max_len) {
            config->irq_mask = *(uint16_t*)(data + offset) & 0xFF;
            offset += 2;
        }
    } else {
        /* Single IRQ number */
        config->irq_mask = 1 << (irq_info & 0x0F);
    }
    
    /* Default IRQ mask for 3Com cards if not specified */
    if (config->irq_mask == 0) {
        config->irq_mask = 0x4EB8;  /* IRQs 3, 4, 5, 7, 10, 11 */
    }
    
    return offset;
}

/**
 * @brief Parse memory information
 */
static int parse_memory_info(uint8_t *data, int max_len, config_entry_t *config) {
    int offset = 0;
    uint8_t mem_info;
    
    if (max_len < 1) return 0;
    
    mem_info = data[offset++];
    
    /* Most 3Com PCMCIA cards don't use memory windows */
    /* Just skip over any memory descriptors */
    config->mem_ranges = 0;
    config->mem_base = 0;
    config->mem_size = 0;
    
    /* Skip memory descriptors based on format */
    if (mem_info & 0x80) {
        /* Variable length descriptor */
        int desc_len = (mem_info >> 5) & 0x03;
        offset += desc_len * 2;  /* Each address is 2 bytes */
    }
    
    return offset;
}

/**
 * @brief Validate that this is a supported 3Com card
 */
int validate_3com_card(cis_3com_info_t *info) {
    const cis_signature_t *sig;
    
    /* Check manufacturer ID */
    if (info->manufacturer_id != MANFID_3COM) {
        log_debug("Not a 3Com card (manufacturer ID: %04X)", 
                 info->manufacturer_id);
        return PCMCIA_ERR_NOT_3COM;
    }
    
    /* Find matching signature */
    for (sig = supported_3com_cards; sig->manufacturer_id != 0; sig++) {
        if (sig->product_id == info->product_id) {
            info->card_type = sig->card_type;
            
            /* Update product name if not already set */
            if (info->product_name[0] == '\0') {
                strncpy(info->product_name, sig->name, 
                       sizeof(info->product_name) - 1);
                info->product_name[sizeof(info->product_name) - 1] = '\0';
            }
            
            log_info("Validated %s (ID: %04X)", sig->name, sig->product_id);
            return sig->card_type;
        }
    }
    
    log_warning("Unknown 3Com card ID: %04X", info->product_id);
    return PCMCIA_ERR_UNSUPPORTED_3COM;
}

/**
 * @brief Get human-readable card type name
 */
const char* card_type_name(card_type_t type) {
    const cis_signature_t *sig;
    
    for (sig = supported_3com_cards; sig->manufacturer_id != 0; sig++) {
        if (sig->card_type == type) {
            return sig->name;
        }
    }
    
    return "Unknown 3Com card";
}

/* Memory mapping functions - these will be implemented based on the 
 * Socket Services or Point Enabler interface being used */

/**
 * @brief Map attribute memory for CIS access
 * @param socket Socket number
 * @param offset Offset in attribute memory
 * @param size Size to map
 * @return Pointer to mapped memory or NULL on failure
 */
uint8_t *map_attribute_memory(uint8_t socket, uint32_t offset, uint32_t size) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    
    if (ctx->socket_services_available) {
        return map_attribute_memory_ss(socket, offset, size);
    } else {
        return map_attribute_memory_pe(socket, offset, size);
    }
}

/**
 * @brief Unmap attribute memory
 */
void unmap_attribute_memory(uint8_t *mapped_ptr) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    
    if (ctx->socket_services_available) {
        unmap_attribute_memory_ss(mapped_ptr);
    } else {
        unmap_attribute_memory_pe(mapped_ptr);
    }
}

/**
 * @brief Map attribute memory using Socket Services
 */
static uint8_t *map_attribute_memory_ss(uint8_t socket, uint32_t offset, uint32_t size) {
    socket_services_req_t req;
    static uint8_t mapped_buffer[512];  /* Static buffer for CIS */
    
    /* Use Socket Services to map attribute memory */
    req.function = SS_SET_WINDOW;
    req.socket = socket;
    req.buffer = (void far*)mapped_buffer;
    req.attributes = 0x40;  /* Attribute memory */
    
    if (call_socket_services(&req) != SS_SUCCESS) {
        return NULL;
    }
    
    /* Copy attribute memory to our buffer */
    /* This is a simplified implementation - real implementation would 
     * set up proper memory windows */
    
    return mapped_buffer;
}

/**
 * @brief Map attribute memory using Point Enabler direct access
 */
static uint8_t *map_attribute_memory_pe(uint8_t socket, uint32_t offset, uint32_t size) {
    point_enabler_context_t *pe = &g_pcmcia_context.point_enabler;
    static uint8_t mapped_buffer[512];
    uint16_t controller_base = pe->io_base;
    
    /* Configure memory window for attribute memory access */
    
    /* Window 0 setup for attribute memory */
    pcic_write_reg(controller_base, socket, 0x10, 0x00);  /* Mem win 0 start low */
    pcic_write_reg(controller_base, socket, 0x11, 0x00);  /* Mem win 0 start high */
    pcic_write_reg(controller_base, socket, 0x12, 0xFF);  /* Mem win 0 end low */
    pcic_write_reg(controller_base, socket, 0x13, 0x0F);  /* Mem win 0 end high */
    pcic_write_reg(controller_base, socket, 0x14, 0x00);  /* Mem win 0 offset low */
    pcic_write_reg(controller_base, socket, 0x15, 0x00);  /* Mem win 0 offset high */
    pcic_write_reg(controller_base, socket, 0x06, 0x40);  /* Enable, attribute mem */
    
    /* Read CIS data from attribute memory */
    /* This is a simplified implementation - real implementation would
     * map the actual memory window */
    
    return mapped_buffer;
}

/**
 * @brief Unmap attribute memory using Socket Services
 */
static void unmap_attribute_memory_ss(uint8_t *mapped_ptr) {
    /* Nothing to do for static buffer */
}

/**
 * @brief Unmap attribute memory using Point Enabler
 */
static void unmap_attribute_memory_pe(uint8_t *mapped_ptr) {
    /* Disable memory window */
    point_enabler_context_t *pe = &g_pcmcia_context.point_enabler;
    
    /* Could disable the memory window here if needed */
    /* For now, just leave it mapped since we use a static buffer */
}
# PCCARD.MOD - Hybrid Card Services Module Specification

## Overview

PCCARD.MOD implements a minimal Card Services layer optimized specifically for 3Com PC Card network interface cards (both PCMCIA and CardBus variants). This module provides 87-90% memory savings over traditional DOS Card Services while maintaining essential functionality including hot-plug support, resource management, and hardware compatibility.

Unlike full Card Services implementations that consume 45-120KB of memory, PCCARD.MOD targets 8-12KB resident footprint through aggressive optimization and 3Com-specific functionality focus.

## Module Classification

```
Module Type:        Service Module (Resident)
Size Target:        8-12KB resident
Primary Focus:      3Com PC Card NICs (PCMCIA + CardBus)
Supported Cards:    3C589 series (16-bit PCMCIA), 3C575 series (32-bit CardBus)
Fallback Support:   Point Enabler mode for systems without Socket Services
```

## Architecture Design

### Hybrid Implementation Strategy

```
┌──────────────────────────────┐
│                Application Layer                 │
├──────────────────────────────┤
│              Packet Driver API                   │
├──────────────────────────────┤
│          PTASK.MOD    │    BOOMTEX.MOD          │
├──────────────────────────────┤
│              PCCARD.MOD (This Module)            │
│ ┌───────────────────────────┐│
│ │        Minimal Card Services Layer          ││
│ │  ├─ CIS Parser (3Com specific)             ││
│ │  ├─ Resource Allocator                     ││
│ │  ├─ Hot-plug Event Manager                 ││
│ │  └─ Client Registration                    ││
│ └───────────────────────────┘│
│                      │                          │
│         ┌───────┴─────────┐         │
│         ▼                            ▼         │
│  Socket Services               Point Enabler     │
│  (INT 1A Interface)           (Direct Access)    │
└──────────────────────────────┘
                      │
                      ▼
              PCMCIA Controller Hardware
              (Intel 82365, Cirrus Logic, etc.)
```

### Memory Comparison

```
Traditional Implementation:
├─ Socket Services: 15-30KB
├─ Card Services: 30-90KB
└─ Total: 45-120KB

PCCARD.MOD Implementation:
├─ Minimal Card Services: 6-8KB
├─ CIS Parser: 1-2KB
├─ Socket Interface: 1-2KB
└─ Total: 8-12KB (87-90% savings)
```

## Core Components

### 1. Card Information Structure (CIS) Parser

#### 3Com-Specific CIS Processing
```c
// Optimized CIS parser for 3Com cards only
typedef struct {
    uint16_t manufacturer_id;    // Should be 0x0101 (3Com)
    uint16_t product_id;        // Card-specific ID
    char product_name[32];      // e.g., "3Com 3C589"
    uint8_t function_type;      // Network interface (0x06)
    uint16_t io_base_hint;      // Preferred I/O base
    uint8_t irq_mask;           // Supported IRQs
    config_entry_t configs[4];  // Configuration options
} cis_3com_info_t;

// Supported 3Com CIS signatures
static const cis_signature_t supported_cards[] = {
    // 3C589 EtherLink III PC Card series
    {0x0101, 0x0589, "3Com 3C589", CARD_3C589},
    {0x0101, 0x058A, "3Com 3C589B", CARD_3C589B},
    {0x0101, 0x058B, "3Com 3C589C", CARD_3C589C},
    {0x0101, 0x058C, "3Com 3C589D", CARD_3C589D},

    // 3C562 EtherLink III LAN+Modem PC Card
    {0x0101, 0x0562, "3Com 3C562", CARD_3C562},
    {0x0101, 0x0563, "3Com 3C562B", CARD_3C562B},

    // 3C574 Fast EtherLink PC Card
    {0x0101, 0x0574, "3Com 3C574-TX", CARD_3C574},

    // 3C575 EtherLink XL CardBus series
    {0x0101, 0x5157, "3Com 3C575-TX", CARD_3C575},
    {0x0101, 0x515A, "3Com 3C575C-TX", CARD_3C575C},

    {0, 0, NULL, CARD_UNKNOWN}
};

int parse_3com_cis(socket_t socket, cis_3com_info_t *info) {
    uint8_t *cis_base;
    tuple_header_t *tuple;
    uint16_t offset = 0;

    // Map CIS memory (attribute memory at offset 0)
    cis_base = map_attribute_memory(socket, 0, 512);
    if (!cis_base) {
        return CIS_ERR_MAP_FAILED;
    }

    // Clear info structure
    memset(info, 0, sizeof(cis_3com_info_t));

    // Parse essential tuples only
    while (offset < 512) {
        tuple = (tuple_header_t*)(cis_base + offset);

        if (tuple->type == CISTPL_END) {
            break;
        }

        switch (tuple->type) {
            case CISTPL_MANFID:
                parse_manufacturer_id(tuple, info);
                break;

            case CISTPL_VERS_1:
                parse_version_string(tuple, info);
                break;

            case CISTPL_FUNCID:
                if (tuple->data[0] != 0x06) {
                    // Not a network interface
                    unmap_attribute_memory(cis_base);
                    return CIS_ERR_NOT_NETWORK;
                }
                info->function_type = tuple->data[0];
                break;

            case CISTPL_CONFIG:
                parse_config_base(tuple, info);
                break;

            case CISTPL_CFTABLE_ENTRY:
                parse_config_entry(tuple, info);
                break;
        }

        offset += tuple->length + 2;
    }

    unmap_attribute_memory(cis_base);

    // Validate that this is a supported 3Com card
    return validate_3com_card(info);
}

static int validate_3com_card(cis_3com_info_t *info) {
    const cis_signature_t *sig;

    if (info->manufacturer_id != 0x0101) {
        return CIS_ERR_NOT_3COM;
    }

    // Find matching signature
    for (sig = supported_cards; sig->manufacturer_id != 0; sig++) {
        if (sig->product_id == info->product_id) {
            log_info("Detected %s (ID: %04X)", sig->name, sig->product_id);
            return sig->card_type;
        }
    }

    log_warning("Unknown 3Com card ID: %04X", info->product_id);
    return CIS_ERR_UNSUPPORTED_3COM;
}
```

### 2. Socket Services Interface

#### INT 1A Interface Implementation
```c
// Socket Services interface using INT 1A
typedef struct {
    uint16_t function;
    uint16_t socket;
    void far *buffer;
    uint16_t attributes;
} socket_services_req_t;

// Socket Services function codes
#define SS_GET_ADAPTER_COUNT    0x80
#define SS_GET_SOCKET_COUNT     0x81
#define SS_GET_SOCKET_INFO      0x82
#define SS_SET_SOCKET          0x83
#define SS_GET_SOCKET          0x84
#define SS_RESET_SOCKET        0x85
#define SS_INQUIRE_ADAPTER     0x86
#define SS_INQUIRE_SOCKET      0x87
#define SS_GET_WINDOW          0x88
#define SS_SET_WINDOW          0x89
#define SS_GET_PAGE            0x8A
#define SS_SET_PAGE            0x8B
#define SS_REGISTER_CALLBACK   0x8C

int call_socket_services(socket_services_req_t *req) {
    int result;

    __asm {
        push    es
        push    di
        push    si
        push    dx
        push    cx
        push    bx

        mov     si, req
        mov     ax, [si].function
        mov     bx, [si].socket
        les     di, [si].buffer
        mov     cx, [si].attributes

        int     1Ah             ; Call Socket Services

        mov     result, ax      ; Return code in AX

        pop     bx
        pop     cx
        pop     dx
        pop     si
        pop     di
        pop     es
    }

    return result;
}

int pcmcia_detect_sockets(void) {
    socket_services_req_t req;
    uint16_t adapter_count, socket_count;
    int result;

    // Check if Socket Services is available
    req.function = SS_GET_ADAPTER_COUNT;
    req.socket = 0;
    req.buffer = (void far*)&adapter_count;
    req.attributes = 0;

    result = call_socket_services(&req);
    if (result != SS_SUCCESS) {
        log_info("Socket Services not available, using Point Enabler mode");
        return init_point_enabler_mode();
    }

    log_info("Socket Services detected, %d adapters", adapter_count);

    // Get total socket count
    req.function = SS_GET_SOCKET_COUNT;
    req.buffer = (void far*)&socket_count;

    result = call_socket_services(&req);
    if (result != SS_SUCCESS || socket_count == 0) {
        return PCMCIA_ERR_NO_SOCKETS;
    }

    log_info("Found %d PCMCIA sockets", socket_count);
    return socket_count;
}
```

### 3. Point Enabler Fallback Mode

#### Direct Controller Access
```c
// Point Enabler mode for systems without Socket Services
typedef struct {
    uint16_t io_base;           // Controller I/O base (e.g., 0x3E0)
    uint8_t controller_type;    // Intel 82365, Cirrus Logic, etc.
    uint8_t socket_count;       // Number of sockets
    socket_info_t sockets[4];   // Socket information
} point_enabler_context_t;

// Common PCMCIA controller I/O addresses
#define PCIC_INDEX_REG     0x3E0
#define PCIC_DATA_REG      0x3E1

// Intel 82365 register definitions
#define PCIC_ID_REVISION   0x00
#define PCIC_STATUS        0x01
#define PCIC_POWER_CONTROL 0x02
#define PCIC_INT_GEN_CTRL  0x03
#define PCIC_CARD_STATUS   0x04
#define PCIC_CARD_CHANGE   0x05

int init_point_enabler_mode(void) {
    point_enabler_context_t *ctx = &point_enabler;
    int detected_sockets = 0;

    log_info("Initializing Point Enabler mode");

    // Try common controller locations
    if (detect_intel_82365(0x3E0)) {
        ctx->io_base = 0x3E0;
        ctx->controller_type = CONTROLLER_82365;
    } else if (detect_cirrus_logic(0x3E0)) {
        ctx->io_base = 0x3E0;
        ctx->controller_type = CONTROLLER_CIRRUS;
    } else if (detect_vadem(0x3E0)) {
        ctx->io_base = 0x3E0;
        ctx->controller_type = CONTROLLER_VADEM;
    } else {
        log_error("No supported PCMCIA controller found");
        return PCMCIA_ERR_NO_CONTROLLER;
    }

    // Detect sockets on controller
    detected_sockets = detect_controller_sockets(ctx);
    if (detected_sockets == 0) {
        log_error("No PCMCIA sockets detected");
        return PCMCIA_ERR_NO_SOCKETS;
    }

    log_info("Point Enabler: %s controller at 0x%04X, %d sockets",
             controller_type_name(ctx->controller_type),
             ctx->io_base, detected_sockets);

    return detected_sockets;
}

int detect_intel_82365(uint16_t io_base) {
    uint8_t id_rev, test_pattern;

    // Write test pattern to scratchpad register
    pcic_write_reg(io_base, 0, 0x0E, 0xAA);
    test_pattern = pcic_read_reg(io_base, 0, 0x0E);

    if (test_pattern != 0xAA) {
        return 0;  // Not responding
    }

    // Read ID/revision register
    id_rev = pcic_read_reg(io_base, 0, PCIC_ID_REVISION);

    // Intel 82365 family detection
    switch (id_rev & 0xF0) {
        case 0x80:
            log_info("Intel 82365SL detected");
            return 1;
        case 0x90:
            log_info("Intel 82365SL-A detected");
            return 1;
        default:
            return 0;
    }
}

uint8_t pcic_read_reg(uint16_t io_base, uint8_t socket, uint8_t reg) {
    uint8_t index = (socket << 6) | (reg & 0x3F);

    outb(io_base, index);           // Write index
    return inb(io_base + 1);        // Read data
}

void pcic_write_reg(uint16_t io_base, uint8_t socket, uint8_t reg, uint8_t value) {
    uint8_t index = (socket << 6) | (reg & 0x3F);

    outb(io_base, index);           // Write index
    outb(io_base + 1, value);       // Write data
}
```

### 4. Resource Management

#### Minimal Resource Allocator
```c
// Simplified resource management for 3Com cards
typedef struct {
    uint16_t io_base;
    uint8_t irq;
    uint32_t mem_base;
    uint16_t mem_size;
    uint8_t config_index;
} resource_allocation_t;

// Resource allocation strategy for 3Com cards
int allocate_card_resources(socket_t socket, cis_3com_info_t *cis_info,
                           resource_allocation_t *resources) {
    int i;

    // Try preferred configuration first
    for (i = 0; i < 4 && cis_info->configs[i].index != 0; i++) {
        config_entry_t *config = &cis_info->configs[i];

        // Try to allocate I/O
        if (config->io_ranges > 0) {
            resources->io_base = allocate_io_range(config->io_base,
                                                  config->io_size);
            if (resources->io_base == 0) {
                continue;  // Try next config
            }
        }

        // Try to allocate IRQ
        if (config->irq_mask != 0) {
            resources->irq = allocate_irq_from_mask(config->irq_mask);
            if (resources->irq == 0) {
                free_io_range(resources->io_base);
                continue;  // Try next config
            }
        }

        // Allocate memory if needed
        if (config->mem_ranges > 0) {
            resources->mem_base = allocate_memory_range(config->mem_size);
            if (resources->mem_base == 0) {
                free_io_range(resources->io_base);
                free_irq(resources->irq);
                continue;  // Try next config
            }
            resources->mem_size = config->mem_size;
        }

        resources->config_index = config->index;
        log_info("Allocated resources: I/O=0x%04X, IRQ=%d, Config=%d",
                resources->io_base, resources->irq, resources->config_index);
        return 0;
    }

    log_error("Failed to allocate resources for card");
    return PCMCIA_ERR_NO_RESOURCES;
}

uint16_t allocate_io_range(uint16_t preferred, uint16_t size) {
    // Simple allocation strategy for DOS environment
    static uint16_t io_pool[] = {0x300, 0x310, 0x320, 0x330, 0x340, 0x350, 0};
    int i;

    // Try preferred address first
    if (preferred != 0 && is_io_range_available(preferred, size)) {
        mark_io_range_used(preferred, size);
        return preferred;
    }

    // Try standard addresses
    for (i = 0; io_pool[i] != 0; i++) {
        if (is_io_range_available(io_pool[i], size)) {
            mark_io_range_used(io_pool[i], size);
            return io_pool[i];
        }
    }

    return 0;  // No available I/O range
}

uint8_t allocate_irq_from_mask(uint8_t irq_mask) {
    static uint8_t preferred_irqs[] = {10, 11, 5, 7, 3, 4, 0};
    int i;

    for (i = 0; preferred_irqs[i] != 0; i++) {
        uint8_t irq = preferred_irqs[i];
        if ((irq_mask & (1 << irq)) && is_irq_available(irq)) {
            mark_irq_used(irq);
            return irq;
        }
    }

    return 0;  // No available IRQ
}
```

### 5. Hot-Plug Event Handling

#### Simplified Event Manager
```c
// Hot-plug event management
typedef struct {
    void (*card_inserted)(socket_t socket);
    void (*card_removed)(socket_t socket);
    void (*status_changed)(socket_t socket, uint8_t status);
} pcmcia_event_handlers_t;

static pcmcia_event_handlers_t event_handlers = {0};

// Interrupt-driven card detection
void __interrupt pcmcia_card_status_isr(void) {
    socket_t socket;
    uint8_t status, changes;

    // Check all sockets for status changes
    for (socket = 0; socket < max_sockets; socket++) {
        status = get_socket_status(socket);
        changes = status ^ socket_status[socket];

        if (changes == 0) {
            continue;  // No changes
        }

        socket_status[socket] = status;

        if (changes & SOCKET_STATUS_CARD_DETECT) {
            if (status & SOCKET_STATUS_CARD_DETECT) {
                // Card inserted
                if (event_handlers.card_inserted) {
                    event_handlers.card_inserted(socket);
                }
            } else {
                // Card removed
                if (event_handlers.card_removed) {
                    event_handlers.card_removed(socket);
                }
            }
        }

        if (changes & SOCKET_STATUS_READY_CHANGE) {
            if (event_handlers.status_changed) {
                event_handlers.status_changed(socket, status);
            }
        }
    }

    // Acknowledge interrupt at controller
    acknowledge_pcmcia_interrupt();

    // Chain to previous interrupt handler
    chain_interrupt();
}

int register_pcmcia_events(pcmcia_event_handlers_t *handlers) {
    if (!handlers) {
        return PCMCIA_ERR_INVALID_PARAM;
    }

    // Copy event handlers
    event_handlers = *handlers;

    // Enable card status change interrupts
    enable_card_status_interrupts();

    log_info("PCMCIA event handlers registered");
    return 0;
}

// Card insertion handler
void handle_card_insertion(socket_t socket) {
    cis_3com_info_t cis_info;
    resource_allocation_t resources;
    int card_type;

    log_info("Card inserted in socket %d", socket);

    // Wait for card to stabilize
    delay_ms(500);

    // Parse CIS to identify card
    card_type = parse_3com_cis(socket, &cis_info);
    if (card_type < 0) {
        log_info("Not a supported 3Com card in socket %d", socket);
        return;
    }

    // Allocate resources
    if (allocate_card_resources(socket, &cis_info, &resources) < 0) {
        log_error("Failed to allocate resources for card in socket %d", socket);
        return;
    }

    // Configure card
    if (configure_card(socket, &resources, &cis_info) < 0) {
        log_error("Failed to configure card in socket %d", socket);
        free_card_resources(socket, &resources);
        return;
    }

    // Initialize appropriate driver module
    switch (card_type) {
        case CARD_3C589:
        case CARD_3C589B:
        case CARD_3C589C:
        case CARD_3C589D:
            initialize_ptask_pcmcia(socket, &resources);
            break;

        case CARD_3C575:
        case CARD_3C575C:
            initialize_boomtex_cardbus(socket, &resources);
            break;

        default:
            log_error("Unsupported card type: %d", card_type);
            break;
    }

    log_info("Card in socket %d initialized successfully", socket);
}
```

## Module Memory Layout

### Runtime Memory Map
```
PCCARD.MOD Memory Layout (8-12KB total):
┌───────────────────────┐
│ Hot Code Section (6-8KB)             │
│ ├─ Socket Services interface 1KB    │
│ ├─ CIS parser             1.5KB     │
│ ├─ Resource allocator     1KB       │
│ ├─ Event handlers         1KB       │
│ ├─ Point Enabler code     1.5KB     │
│ └─ Hot-plug management    1KB       │
├───────────────────────┤
│ Data Section (2-3KB)                 │
│ ├─ Socket information     512 bytes │
│ ├─ Resource tracking      512 bytes │
│ ├─ Event state           256 bytes  │
│ ├─ CIS cache             512 bytes  │
│ └─ Controller state      512 bytes  │
├───────────────────────┤
│ Cold Section (1KB, discarded)        │
│ ├─ Controller detection   512 bytes │
│ ├─ Initial card scan      256 bytes │
│ └─ Setup routines         256 bytes │
└───────────────────────┘

Comparison with Full Card Services:
├─ Traditional CS+SS: 45-120KB
├─ PCCARD.MOD: 8-12KB
└─ Memory savings: 87-90%
```

## Integration with NIC Modules

### PTASK.MOD Integration (3C589 PCMCIA)
```c
// Integration point for PTASK.MOD
int initialize_ptask_pcmcia(socket_t socket, resource_allocation_t *resources) {
    ptask_pcmcia_context_t *ctx;

    // Allocate PCMCIA context
    ctx = &ptask_pcmcia_contexts[socket];
    ctx->socket = socket;
    ctx->io_base = resources->io_base;
    ctx->irq = resources->irq;
    ctx->config_index = resources->config_index;

    // Initialize as standard 3C509 with PCMCIA extensions
    if (ptask_init_common(&ctx->nic_context, resources->io_base, resources->irq) < 0) {
        return -1;
    }

    // Set up hot-plug handlers
    ctx->nic_context.cleanup = ptask_pcmcia_cleanup;
    ctx->nic_context.suspend = ptask_pcmcia_suspend;
    ctx->nic_context.resume = ptask_pcmcia_resume;

    // Register with packet driver
    register_packet_interface(socket, &ptask_pcmcia_ops);

    log_info("3C589 PCMCIA initialized on socket %d", socket);
    return 0;
}

void ptask_pcmcia_cleanup(nic_context_t *nic_ctx) {
    ptask_pcmcia_context_t *ctx = container_of(nic_ctx, ptask_pcmcia_context_t, nic_context);

    // Standard 3C509 cleanup
    ptask_cleanup_common(nic_ctx);

    // PCCARD-specific cleanup
    free_card_resources(ctx->socket, &ctx->resources);

    log_info("3C589 PCMCIA cleaned up from socket %d", ctx->socket);
}
```

### BOOMTEX.MOD Integration (3C575 CardBus)
```c
// Integration point for BOOMTEX.MOD
int initialize_boomtex_cardbus(socket_t socket, resource_allocation_t *resources) {
    boomtex_cardbus_context_t *ctx;

    // CardBus cards use PCI-style configuration
    ctx = &boomtex_cardbus_contexts[socket];
    ctx->socket = socket;

    // Map CardBus as PCI device
    ctx->pci_device.io_base = resources->io_base;
    ctx->pci_device.irq_line = resources->irq;
    ctx->pci_device.device_id = get_cardbus_device_id(socket);

    // Initialize as PCI device
    if (boomtex_init_pci(&ctx->pci_device) < 0) {
        return -1;
    }

    // Set up CardBus-specific handlers
    ctx->pci_device.cleanup = boomtex_cardbus_cleanup;
    ctx->pci_device.power_management = boomtex_cardbus_power_mgmt;

    // Register with packet driver
    register_packet_interface(socket, &boomtex_cardbus_ops);

    log_info("3C575 CardBus initialized on socket %d", socket);
    return 0;
}
```

## Configuration and Usage

### Module Loading
```batch
REM Load PCCARD.MOD before NIC modules
3COMPD.COM /MODULE=PCMCIA

REM Auto-detect PCMCIA/CardBus cards
3COMPD.COM /MODULE=PCMCIA /MODULE=PTASK /MODULE=BOOMTEX

REM Force Point Enabler mode (bypass Socket Services)
3COMPD.COM /MODULE=PCMCIA /POINTENABLER=ON

REM Enable verbose PCMCIA logging
3COMPD.COM /MODULE=PCMCIA /PCMCIA_DEBUG=ON
```

### Error Handling and Diagnostics
```c
// Comprehensive error handling
typedef enum {
    PCMCIA_SUCCESS = 0,
    PCMCIA_ERR_NO_SOCKETS = -1,
    PCMCIA_ERR_NO_CONTROLLER = -2,
    PCMCIA_ERR_NO_RESOURCES = -3,
    PCMCIA_ERR_CIS_PARSE = -4,
    PCMCIA_ERR_NOT_3COM = -5,
    PCMCIA_ERR_UNSUPPORTED = -6,
    PCMCIA_ERR_HARDWARE = -7,
    PCMCIA_ERR_CONFIG = -8
} pcmcia_error_t;

const char* pcmcia_error_string(pcmcia_error_t error) {
    switch (error) {
        case PCMCIA_SUCCESS: return "Success";
        case PCMCIA_ERR_NO_SOCKETS: return "No PCMCIA sockets found";
        case PCMCIA_ERR_NO_CONTROLLER: return "No PCMCIA controller detected";
        case PCMCIA_ERR_NO_RESOURCES: return "Unable to allocate resources";
        case PCMCIA_ERR_CIS_PARSE: return "CIS parsing failed";
        case PCMCIA_ERR_NOT_3COM: return "Not a 3Com card";
        case PCMCIA_ERR_UNSUPPORTED: return "Unsupported card type";
        case PCMCIA_ERR_HARDWARE: return "Hardware error";
        case PCMCIA_ERR_CONFIG: return "Configuration error";
        default: return "Unknown error";
    }
}
```

## Testing and Validation

### Hardware Test Matrix
```
Primary Test Hardware:
├─ 3C589C PCMCIA in Toshiba Satellite
├─ 3C575C CardBus in IBM ThinkPad
├─ Intel 82365SL controller systems
├─ Cirrus Logic controller systems
└─ Various Socket Services implementations

Point Enabler Testing:
├─ Systems without Socket Services
├─ Direct controller access validation
├─ Resource allocation testing
└─ Manual configuration scenarios

Hot-Plug Testing:
├─ Card insertion during operation
├─ Card removal during transfer
├─ Power management transitions
└─ Multiple insert/remove cycles
```

### Performance Benchmarks
```
Memory Usage Targets:
├─ Resident size: 8-12KB (vs 45-120KB traditional)
├─ Initialization overhead: <2 seconds
├─ Hot-plug latency: <1 second
└─ Resource allocation: <100ms

Compatibility Targets:
├─ 95%+ Socket Services implementations
├─ 80%+ Point Enabler controller support
├─ 100% supported 3Com cards
└─ Zero regression vs full Card Services
```

## Future Enhancements

### Phase 6+ Extensions
```
Advanced PCMCIA Features:
├─ Multi-function card support
├─ Power management optimization
├─ Advanced resource arbitration
├─ CardBus 32-bit burst support
└─ Zoom Video support (future)

Integration Enhancements:
├─ APM integration for suspend/resume
├─ Disk caching for CIS data
├─ Network boot from PCMCIA
└─ Remote configuration support
```

## Module Interface

### Registration with Core Loader
```c
int pcmcia_register(void) {
    module_info_t info = {
        .name = "PCMCIA",
        .version = 0x0100,
        .size = 10240,             // 10KB average
        .type = MODULE_SERVICE,
        .family = FAMILY_PCMCIA,
        .ops = &pcmcia_operations,
        .init_priority = 10,       // Load before NIC modules
        .dependencies = NULL
    };

    return register_module(&info);
}
```

This PCCARD.MOD specification establishes the foundation for efficient PCMCIA and CardBus support while achieving 87-90% memory savings over traditional implementations. The hybrid approach ensures maximum compatibility with existing systems while providing the performance and memory efficiency required for the modular architecture.

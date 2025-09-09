# Naming Conventions - 3Com Packet Driver Modular Architecture

**Version**: 1.0  
**Date**: 2025-08-22  
**Status**: MANDATORY for all agents

## Module Naming

### Module Files
- **Format**: `MODNAME.MOD` (8.3 DOS format)
- **Case**: UPPERCASE for module names
- **Extensions**: Always `.MOD` for loadable modules

### Standard Module Names
```
PTASK.MOD     - Parallel Tasking (3C509B ISA + 3C589 PCMCIA)
CORKSCRW.MOD  - Corkscrew (3C515 ISA bus-master)
BOOMTEX.MOD   - Boomerang Extended (PCI/CardBus)
PCCARD.MOD    - PC Card Services (PCMCIA/CardBus support)
ROUTING.MOD   - Multi-homing and flow routing
MEMPOOL.MOD   - Memory pool management
DIAG.MOD      - Diagnostics and testing
STATS.MOD     - Statistics collection
```

### Service Module Prefixes
- **SVC_**: Core services (e.g., `SVC_MEM`, `SVC_HAL`)
- **NIC_**: NIC-specific modules (e.g., `NIC_3C509`)
- **OPT_**: Optional features (e.g., `OPT_ROUTE`)

## Function Naming

### Module Entry Points
```c
// Standard module interface functions
int module_init(void);           // Initialize module
int module_cleanup(void);        // Cleanup on unload
int module_get_info(module_info_t *info);  // Get module information
```

### NIC Module Functions
```c
// NIC interface (all modules must implement)
int nic_detect(nic_info_t *info);        // Hardware detection
int nic_init(nic_info_t *nic);           // Initialize NIC
int nic_open(nic_info_t *nic);           // Open for packet I/O
int nic_close(nic_info_t *nic);          // Close NIC
int nic_send(nic_info_t *nic, packet_t *pkt);  // Send packet
int nic_set_address(nic_info_t *nic, uint8_t *addr);  // Set MAC
int nic_set_mode(nic_info_t *nic, uint16_t mode);     // Set receive mode
```

### Service Functions
```c
// Memory service
int svc_mem_alloc(size_t size, void **ptr);
int svc_mem_free(void *ptr);
int svc_mem_get_dma_buffer(size_t size, dma_buffer_t *buf);

// Hardware abstraction
uint8_t svc_hal_inb(uint16_t port);
void svc_hal_outb(uint16_t port, uint8_t value);
int svc_hal_set_irq(uint8_t irq, irq_handler_t handler);
```

## Variable Naming

### Hungarian Notation (Limited Use)
```c
// Prefixes for clarity in real-mode context
uint16_t wPort;         // w = word (16-bit)
uint8_t  bValue;        // b = byte (8-bit)
uint32_t dwAddress;     // dw = double word (32-bit)
void far *lpBuffer;     // lp = long pointer (far)
char *szString;         // sz = null-terminated string
```

### Module-Specific Prefixes
```c
// PTASK.MOD variables
ptask_config_t ptask_cfg;
ptask_stats_t ptask_stats;

// CORKSCRW.MOD variables  
corkscrw_ring_t corkscrw_tx_ring;
corkscrw_state_t corkscrw_state;

// Generic module variables
mod_header_t mod_hdr;
mod_info_t mod_info;
```

## Macro Naming

### Module Identification
```c
#define PTASK_MODULE_ID         0x5054    // 'PT'
#define CORKSCRW_MODULE_ID      0x434B    // 'CK' 
#define BOOMTEX_MODULE_ID       0x4254    // 'BT'

#define MODULE_VERSION_MAJOR    1
#define MODULE_VERSION_MINOR    0
#define MODULE_ABI_VERSION      1
```

### Hardware Constants
```c
// Register offsets (NIC-specific)
#define PTASK_REG_STATUS        0x0E
#define PTASK_REG_COMMAND       0x0E  
#define PTASK_REG_TX_STATUS     0x1B

#define CORKSCRW_REG_UP_LIST    0x38
#define CORKSCRW_REG_DN_LIST    0x24
```

### Bit Definitions
```c
// Status bits
#define STATUS_LINK_UP          0x0001
#define STATUS_TX_COMPLETE      0x0002
#define STATUS_RX_COMPLETE      0x0004
#define STATUS_ERROR            0x8000

// Command bits
#define CMD_START_TX            0x0001
#define CMD_ENABLE_IRQ          0x0002
#define CMD_RESET               0x8000
```

## File and Directory Structure

### Source Organization
```
src/
├── modules/
│   ├── ptask/          # PTASK.MOD sources
│   ├── corkscrw/       # CORKSCRW.MOD sources  
│   ├── boomtex/        # BOOMTEX.MOD sources
│   └── services/       # Service modules
├── loader/             # 3COMPD.COM core loader
├── shared/             # Shared headers and utilities
└── tests/              # Module test suites
```

### Header Files
```c
// Module headers
#include "ptask.h"      // PTASK.MOD definitions
#include "corkscrw.h"   // CORKSCRW.MOD definitions
#include "mod_abi.h"    // Module ABI definitions
#include "pktdrv.h"     // Packet Driver API
```

## Assembly Language Conventions

### Label Naming
```asm
; Module entry points
ptask_init_entry:
ptask_tx_handler:
ptask_rx_handler:

; Local labels
.loop:
.done:
.error:

; Global symbols
_ptask_send_packet
_ptask_irq_handler
```

### Segment Names
```asm
; Standard segments
CSEG    SEGMENT PARA PUBLIC 'CODE'      ; Code segment
DSEG    SEGMENT PARA PUBLIC 'DATA'      ; Data segment  
BSEG    SEGMENT PARA PUBLIC 'BSS'       ; Uninitialized data

; Module-specific segments
PTASK_HOT   SEGMENT PARA PUBLIC 'CODE'  ; Hot path code
PTASK_COLD  SEGMENT PARA PUBLIC 'INIT'  ; Cold path code (discardable)
```

### Procedure Naming
```asm
; Far procedures (called from other modules)
ptask_send_packet   PROC FAR
corkscrw_init_ring  PROC FAR

; Near procedures (internal to module)
check_tx_status     PROC NEAR
update_statistics   PROC NEAR
```

## Error Code Naming

### Standard Format
```c
#define ERROR_[CATEGORY]_[SPECIFIC]     0x[CODE]

// Examples
#define ERROR_HARDWARE_NOT_FOUND        0x0040
#define ERROR_PACKET_TOO_LARGE         0x0060
#define ERROR_MODULE_LOAD_FAILED       0x0023
```

## Documentation Standards

### File Headers
```c
/*
 * Module: PTASK.MOD - Parallel Tasking Driver
 * Version: 1.0
 * Date: 2025-08-22
 * Author: [Agent Name]
 * 
 * Description: 3C509B ISA and 3C589 PCMCIA driver implementation
 */
```

### Function Documentation
```c
/*
 * Function: nic_send
 * Purpose: Transmit a packet through the NIC
 * Parameters:
 *   nic - Pointer to NIC information structure
 *   pkt - Pointer to packet to transmit
 * Returns: 
 *   SUCCESS - Packet queued successfully
 *   ERROR_* - Specific error code
 * Notes: Function is interrupt-safe
 */
```

## Consistency Rules

### Mandatory Requirements
1. **All modules** must use standard entry point names
2. **All functions** must return standardized error codes
3. **All variables** must use consistent prefixes within modules
4. **All macros** must follow category_specific_name format
5. **All files** must include proper headers with version info

### Prohibited Patterns
- **Mixed case** in module names (use UPPERCASE)
- **Long filenames** exceeding 8.3 format
- **Generic names** like `driver.mod` or `network.mod`
- **Inconsistent prefixes** within the same module
- **Hungarian notation** for simple loop counters

---

**Enforcement**: All agents must validate naming compliance before deliverable submission. CI pipeline will enforce naming conventions automatically.
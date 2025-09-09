/**
 * @file module_manager.c
 * @brief Module Manager Implementation for 3Com Packet Driver
 * 
 * Phase 3A: Dynamic Module Loading - Stream 1 Day 2-3
 * 
 * This file implements the module loading, unloading, and registry management
 * functionality. Handles .MOD file discovery, validation, and runtime management.
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#include "module_manager.h"
#include "core_loader.h"
#include "module_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <io.h>
#include <fcntl.h>

/* Module search patterns */
static const char* module_patterns[] = {
    "*.MOD",
    "ETHRLINK3.MOD",
    "CORKSCREW.MOD",
    "ROUTING.MOD",
    "FLOWCTRL.MOD",
    "STATS.MOD",
    "DIAG.MOD",
    "PROMISC.MOD",
    NULL
};

/* Forward declarations */
static bool discover_modules(module_registry_t* registry, const char* search_path);
static bool load_module_file(module_registry_t* registry, const char* filename, bool required);
static bool validate_module_file(const void* file_data, size_t file_size, module_header_t* header);
static bool relocate_module(loaded_module_t* module, const void* file_data);
static bool initialize_module(loaded_module_t* module, core_services_t* core_services);
static void cleanup_module(loaded_module_t* module);
static loaded_module_t* find_free_module_slot(module_registry_t* registry);
static bool check_module_dependencies(module_registry_t* registry, const module_header_t* header);
static uint16_t calculate_module_checksum(const void* module_data, size_t size);

/* ============================================================================
 * Module Registry Management
 * ============================================================================ */

/**
 * @brief Initialize the module registry
 */
bool module_registry_initialize(module_registry_t* registry, memory_services_t* memory)
{
    if (!registry || !memory) {
        return false;
    }
    
    /* Clear registry */
    memset(registry, 0, sizeof(module_registry_t));
    
    /* Set up default search paths */
    strcpy(registry->search_paths[0], ".");          /* Current directory */
    strcpy(registry->search_paths[1], "MODULES");    /* Modules subdirectory */
    strcpy(registry->search_paths[2], "C:\\3CPD");   /* Default installation */
    registry->search_path_count = 3;
    
    /* Initialize module states */
    for (int i = 0; i < MAX_LOADED_MODULES; i++) {
        registry->modules[i].state = MODULE_STATE_UNLOADED;
        registry->modules[i].base_address = NULL;
        registry->modules[i].header = NULL;
    }
    
    registry->module_count = 0;
    registry->next_module_id = 1;
    
    /* Initialize statistics */
    registry->total_loads = 0;
    registry->total_unloads = 0;
    registry->load_failures = 0;
    
    /* Bind operation functions */
    registry->load_module = load_module;
    registry->unload_module = unload_module;
    registry->find_module = find_module;
    registry->verify_dependencies = verify_dependencies;
    
    return true;
}

/**
 * @brief Shutdown the module registry
 */
void module_registry_shutdown(module_registry_t* registry)
{
    if (!registry) return;
    
    /* Unload all modules */
    for (int i = 0; i < MAX_LOADED_MODULES; i++) {
        if (registry->modules[i].state != MODULE_STATE_UNLOADED) {
            unload_module(registry, i);
        }
    }
    
    /* Clear registry */
    memset(registry, 0, sizeof(module_registry_t));
}

/* ============================================================================
 * Module Loading Implementation
 * ============================================================================ */

/**
 * @brief Load required modules for system operation
 */
int load_required_modules(core_loader_t* core)
{
    module_registry_t* registry = &core->module_registry;
    int loaded_count = 0;
    char search_path[256];
    
    if (!core || !registry) {
        return -1;
    }
    
    core->core_services.log_message(LOG_LEVEL_INFO, "MODULE", 
        "Loading required modules...");
    
    /* Determine search path */
    if (strlen(core->config.module_path) > 0) {
        strcpy(search_path, core->config.module_path);
    } else {
        strcpy(search_path, ".");  /* Current directory */
    }
    
    /* Discover and load modules */
    if (!discover_modules(registry, search_path)) {
        core->core_services.log_message(LOG_LEVEL_ERROR, "MODULE",
            "Module discovery failed in path: %s", search_path);
        return -1;
    }
    
    /* Load hardware modules first (required for basic operation) */
    const char* required_modules[] = {
        "ETHRLINK3.MOD",  /* 3C509 family support */
        "CORKSCREW.MOD",  /* 3C515 family support */
        NULL
    };
    
    for (int i = 0; required_modules[i]; i++) {
        if (load_module_file(registry, required_modules[i], true)) {
            loaded_count++;
            core->core_services.log_message(LOG_LEVEL_INFO, "MODULE",
                "Required module loaded: %s", required_modules[i]);
        } else {
            core->core_services.log_message(LOG_LEVEL_WARNING, "MODULE",
                "Required module not found: %s", required_modules[i]);
        }
    }
    
    registry->total_loads += loaded_count;
    
    if (core->config.verbose_logging) {
        printf("3CPD: Loaded %d required modules\n", loaded_count);
    }
    
    return loaded_count;
}

/**
 * @brief Load optional feature modules
 */
int load_optional_modules(core_loader_t* core)
{
    module_registry_t* registry = &core->module_registry;
    int loaded_count = 0;
    
    if (!core || !registry) {
        return -1;
    }
    
    /* Skip if not loading all features */
    if (!core->config.load_all_features) {
        return 0;
    }
    
    core->core_services.log_message(LOG_LEVEL_INFO, "MODULE",
        "Loading optional feature modules...");
    
    /* Optional feature modules */
    const char* optional_modules[] = {
        "ROUTING.MOD",   /* Multi-NIC routing */
        "FLOWCTRL.MOD",  /* Flow control */
        "STATS.MOD",     /* Statistics */
        "DIAG.MOD",      /* Diagnostics */
        "PROMISC.MOD",   /* Promiscuous mode */
        NULL
    };
    
    for (int i = 0; optional_modules[i]; i++) {
        if (load_module_file(registry, optional_modules[i], false)) {
            loaded_count++;
            core->core_services.log_message(LOG_LEVEL_INFO, "MODULE",
                "Optional module loaded: %s", optional_modules[i]);
        }
    }
    
    registry->total_loads += loaded_count;
    
    if (core->config.verbose_logging) {
        printf("3CPD: Loaded %d optional modules\n", loaded_count);
    }
    
    return loaded_count;
}

/**
 * @brief Unload all modules
 */
void unload_all_modules(core_loader_t* core)
{
    module_registry_t* registry = &core->module_registry;
    int unloaded_count = 0;
    
    if (!core || !registry) {
        return;
    }
    
    core->core_services.log_message(LOG_LEVEL_INFO, "MODULE",
        "Unloading all modules...");
    
    /* Unload in reverse dependency order */
    for (int i = MAX_LOADED_MODULES - 1; i >= 0; i--) {
        if (registry->modules[i].state != MODULE_STATE_UNLOADED) {
            if (unload_module(registry, i)) {
                unloaded_count++;
            }
        }
    }
    
    registry->total_unloads += unloaded_count;
    
    if (core->config.verbose_logging) {
        printf("3CPD: Unloaded %d modules\n", unloaded_count);
    }
}

/**
 * @brief Load a specific module
 */
int load_module(module_registry_t* registry, const char* module_name, bool required)
{
    loaded_module_t* module;
    char full_path[256];
    FILE* file;
    void* file_data = NULL;
    size_t file_size;
    module_header_t header;
    core_loader_t* core = get_core_loader();
    
    if (!registry || !module_name) {
        return -1;
    }
    
    /* Find free module slot */
    module = find_free_module_slot(registry);
    if (!module) {
        if (core) {
            core->core_services.log_message(LOG_LEVEL_ERROR, "MODULE",
                "No free module slots available");
        }
        registry->load_failures++;
        return -1;
    }
    
    /* Find module file in search paths */
    bool found = false;
    for (int i = 0; i < registry->search_path_count && !found; i++) {
        snprintf(full_path, sizeof(full_path), "%s\\%s", 
                registry->search_paths[i], module_name);
        
        file = fopen(full_path, "rb");
        if (file) {
            found = true;
            break;
        }
    }
    
    if (!found) {
        if (required && core) {
            core->core_services.log_message(LOG_LEVEL_ERROR, "MODULE",
                "Required module not found: %s", module_name);
        }
        registry->load_failures++;
        return -1;
    }
    
    /* Get file size */
    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    /* Allocate buffer for file data */
    file_data = core->memory_services.allocate(file_size, MEMORY_TYPE_TEMP, 0, 1);
    if (!file_data) {
        fclose(file);
        if (core) {
            core->core_services.log_message(LOG_LEVEL_ERROR, "MODULE",
                "Memory allocation failed for module: %s", module_name);
        }
        registry->load_failures++;
        return -1;
    }
    
    /* Read file data */
    if (fread(file_data, 1, file_size, file) != file_size) {
        fclose(file);
        core->memory_services.deallocate(file_data);
        if (core) {
            core->core_services.log_message(LOG_LEVEL_ERROR, "MODULE",
                "File read error for module: %s", module_name);
        }
        registry->load_failures++;
        return -1;
    }
    fclose(file);
    
    /* Validate module file */
    if (!validate_module_file(file_data, file_size, &header)) {
        core->memory_services.deallocate(file_data);
        if (core) {
            core->core_services.log_message(LOG_LEVEL_ERROR, "MODULE",
                "Module validation failed: %s", module_name);
        }
        registry->load_failures++;
        return -1;
    }
    
    /* Check dependencies */
    if (!check_module_dependencies(registry, &header)) {
        core->memory_services.deallocate(file_data);
        if (core) {
            core->core_services.log_message(LOG_LEVEL_ERROR, "MODULE",
                "Module dependency check failed: %s", module_name);
        }
        registry->load_failures++;
        return -1;
    }
    
    /* Allocate memory for module */
    size_t module_size = header.module_size * 16;  /* Convert paragraphs to bytes */
    module->base_address = core->memory_services.allocate(
        module_size, MEMORY_TYPE_MODULE, 
        MEMORY_FLAG_ALIGN | MEMORY_FLAG_EXECUTABLE, 
        MEMORY_ALIGN_PARA);
    
    if (!module->base_address) {
        core->memory_services.deallocate(file_data);
        if (core) {
            core->core_services.log_message(LOG_LEVEL_ERROR, "MODULE",
                "Module memory allocation failed: %s", module_name);
        }
        registry->load_failures++;
        return -1;
    }
    
    /* Copy module data */
    memcpy(module->base_address, file_data, file_size);
    core->memory_services.deallocate(file_data);
    
    /* Set up module information */
    strncpy(module->name, header.name, MODULE_NAME_LENGTH - 1);
    module->name[MODULE_NAME_LENGTH - 1] = '\0';
    strncpy(module->filename, module_name, sizeof(module->filename) - 1);
    module->filename[sizeof(module->filename) - 1] = '\0';
    
    module->header = (module_header_t*)module->base_address;
    module->state = MODULE_STATE_LOADING;
    module->memory_size = module_size;
    module->memory_type = MEMORY_TYPE_MODULE;
    module->load_time = core->core_services.timing.get_ticks();
    module->last_activity = module->load_time;
    
    /* Relocate module if necessary */
    if (!relocate_module(module, module->base_address)) {
        cleanup_module(module);
        if (core) {
            core->core_services.log_message(LOG_LEVEL_ERROR, "MODULE",
                "Module relocation failed: %s", module_name);
        }
        registry->load_failures++;
        return -1;
    }
    
    /* Initialize module */
    if (!initialize_module(module, &core->core_services)) {
        cleanup_module(module);
        if (core) {
            core->core_services.log_message(LOG_LEVEL_ERROR, "MODULE",
                "Module initialization failed: %s", module_name);
        }
        registry->load_failures++;
        return -1;
    }
    
    module->state = MODULE_STATE_LOADED;
    registry->module_count++;
    
    if (core && core->config.verbose_logging) {
        printf("3CPD: Module loaded: %s (%zu bytes)\n", module_name, module_size);
    }
    
    return module - registry->modules;  /* Return module index */
}

/**
 * @brief Unload a specific module
 */
bool unload_module(module_registry_t* registry, uint8_t module_id)
{
    loaded_module_t* module;
    core_loader_t* core = get_core_loader();
    
    if (!registry || module_id >= MAX_LOADED_MODULES) {
        return false;
    }
    
    module = &registry->modules[module_id];
    if (module->state == MODULE_STATE_UNLOADED) {
        return true;  /* Already unloaded */
    }
    
    module->state = MODULE_STATE_UNLOADING;
    
    /* Call module cleanup function if available */
    if (module->cleanup_function) {
        ((module_cleanup_fn)module->cleanup_function)();
    }
    
    /* Clean up module resources */
    cleanup_module(module);
    
    registry->module_count--;
    
    if (core && core->config.verbose_logging) {
        printf("3CPD: Module unloaded: %s\n", module->name);
    }
    
    return true;
}

/**
 * @brief Find a module by name
 */
loaded_module_t* find_module(module_registry_t* registry, const char* name)
{
    if (!registry || !name) {
        return NULL;
    }
    
    for (int i = 0; i < MAX_LOADED_MODULES; i++) {
        if (registry->modules[i].state != MODULE_STATE_UNLOADED &&
            strcmp(registry->modules[i].name, name) == 0) {
            return &registry->modules[i];
        }
    }
    
    return NULL;
}

/**
 * @brief Verify module dependencies
 */
bool verify_dependencies(module_registry_t* registry, const module_header_t* header)
{
    if (!registry || !header) {
        return false;
    }
    
    /* For now, assume no dependencies are required */
    /* Full implementation would check header->deps_count and deps_offset */
    
    return true;
}

/* ============================================================================
 * Module File Processing
 * ============================================================================ */

/**
 * @brief Discover modules in a search path
 */
static bool discover_modules(module_registry_t* registry, const char* search_path)
{
    /* Simple implementation - just adds the search path to registry */
    /* Full implementation would scan directory for .MOD files */
    
    if (registry->search_path_count < MAX_MODULE_SEARCH_PATHS) {
        strncpy(registry->search_paths[registry->search_path_count], 
                search_path, MODULE_SEARCH_PATH_LENGTH - 1);
        registry->search_paths[registry->search_path_count][MODULE_SEARCH_PATH_LENGTH - 1] = '\0';
        registry->search_path_count++;
    }
    
    return true;
}

/**
 * @brief Load a module file
 */
static bool load_module_file(module_registry_t* registry, const char* filename, bool required)
{
    return load_module(registry, filename, required) >= 0;
}

/**
 * @brief Validate module file format and header
 */
static bool validate_module_file(const void* file_data, size_t file_size, module_header_t* header)
{
    const module_file_header_t* file_header;
    const module_header_t* mod_header;
    uint32_t calculated_checksum;
    
    if (!file_data || file_size < sizeof(module_file_header_t) + sizeof(module_header_t)) {
        return false;
    }
    
    /* Validate file header */
    file_header = (const module_file_header_t*)file_data;
    
    if (memcmp(file_header->signature, MODULE_FILE_SIGNATURE, MODULE_FILE_SIGNATURE_LENGTH) != 0) {
        return false;
    }
    
    if (!is_format_compatible(file_header->format_version)) {
        return false;
    }
    
    if (file_header->file_size != file_size) {
        return false;
    }
    
    /* Validate checksum */
    calculated_checksum = calculate_crc32(file_data, 
        file_size - sizeof(file_header->checksum));
    if (calculated_checksum != file_header->checksum) {
        return false;
    }
    
    /* Get module header */
    if (file_header->header_offset >= file_size) {
        return false;
    }
    
    mod_header = (const module_header_t*)((const uint8_t*)file_data + file_header->header_offset);
    
    /* Validate module header */
    if (!VALIDATE_MODULE_HEADER(mod_header)) {
        return false;
    }
    
    /* Copy header for caller */
    if (header) {
        *header = *mod_header;
    }
    
    return true;
}

/**
 * @brief Relocate module in memory
 */
static bool relocate_module(loaded_module_t* module, const void* file_data)
{
    /* Simple implementation - assume no relocation needed for now */
    /* Full implementation would process relocation tables */
    
    return true;
}

/**
 * @brief Initialize a loaded module
 */
static bool initialize_module(loaded_module_t* module, core_services_t* core_services)
{
    if (!module || !module->header || !core_services) {
        return false;
    }
    
    /* Get initialization function */
    if (module->header->init_offset) {
        module->init_function = (uint8_t*)module->base_address + module->header->init_offset;
        
        /* Call initialization function based on module class */
        if (module->header->module_class == MODULE_CLASS_HARDWARE) {
            /* Hardware module initialization */
            hardware_init_fn init_fn = (hardware_init_fn)module->init_function;
            hardware_info_t hw_info = {0};  /* Would be populated from detection */
            
            module->vtable = init_fn(0, core_services, &hw_info);
            if (!module->vtable) {
                return false;
            }
        } else if (module->header->module_class == MODULE_CLASS_FEATURE) {
            /* Feature module initialization */
            feature_init_fn init_fn = (feature_init_fn)module->init_function;
            module_config_t config = {0};  /* Would be populated from configuration */
            
            if (!init_fn(core_services, &config)) {
                return false;
            }
        }
    }
    
    /* Get cleanup function */
    if (module->header->cleanup_offset) {
        module->cleanup_function = (uint8_t*)module->base_address + module->header->cleanup_offset;
    }
    
    return true;
}

/**
 * @brief Clean up module resources
 */
static void cleanup_module(loaded_module_t* module)
{
    core_loader_t* core = get_core_loader();
    
    if (!module) return;
    
    /* Free module memory */
    if (module->base_address && core) {
        core->memory_services.deallocate(module->base_address);
    }
    
    /* Clear module information */
    memset(module, 0, sizeof(loaded_module_t));
    module->state = MODULE_STATE_UNLOADED;
}

/**
 * @brief Find a free module slot
 */
static loaded_module_t* find_free_module_slot(module_registry_t* registry)
{
    for (int i = 0; i < MAX_LOADED_MODULES; i++) {
        if (registry->modules[i].state == MODULE_STATE_UNLOADED) {
            return &registry->modules[i];
        }
    }
    return NULL;
}

/**
 * @brief Check module dependencies
 */
static bool check_module_dependencies(module_registry_t* registry, const module_header_t* header)
{
    /* Simplified implementation - assume no dependencies for now */
    return true;
}

/**
 * @brief Calculate module checksum
 */
static uint16_t calculate_module_checksum(const void* module_data, size_t size)
{
    const uint8_t* data = (const uint8_t*)module_data;
    uint16_t checksum = 0;
    
    for (size_t i = 0; i < size; i++) {
        checksum += data[i];
    }
    
    return checksum;
}
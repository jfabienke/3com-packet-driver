/**
 * @file multi_nic_coord.c
 * @brief Enhanced Multi-NIC Coordination Implementation
 * 
 * Phase 5 Enhancement: Advanced multi-NIC management with load balancing,
 * failover, and intelligent packet routing
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#include "../../include/multi_nic_coord.h"
#include "../../include/logging.h"
#include "../../include/memory.h"
#include "../../include/routing.h"
#include "../../include/stats.h"
#include <string.h>
#include <stdio.h>

/* Global multi-NIC coordinator */
static multi_nic_coordinator_t g_coordinator = {0};
static int g_initialized = 0;

/* Load balancing algorithms */
static int load_balance_round_robin(packet_context_t *context, uint8_t *selected_nic);
static int load_balance_weighted(packet_context_t *context, uint8_t *selected_nic);
static int load_balance_least_loaded(packet_context_t *context, uint8_t *selected_nic);
static int load_balance_hash_based(packet_context_t *context, uint8_t *selected_nic);
static int load_balance_adaptive(packet_context_t *context, uint8_t *selected_nic);

/* Function pointer table for load balancing */
static load_balance_func_t g_load_balance_funcs[] = {
    load_balance_round_robin,
    load_balance_weighted,
    load_balance_least_loaded,
    load_balance_hash_based,
    load_balance_adaptive
};

/**
 * @brief Initialize multi-NIC coordination system
 */
int multi_nic_init(void) {
    if (g_initialized) {
        log_warning("Multi-NIC coordinator already initialized");
        return SUCCESS;
    }
    
    log_info("Initializing multi-NIC coordination system");
    
    /* Clear coordinator structure */
    memset(&g_coordinator, 0, sizeof(multi_nic_coordinator_t));
    
    /* Set default configuration */
    g_coordinator.config.mode = MULTI_NIC_MODE_ACTIVE_STANDBY;
    g_coordinator.config.load_balance_algo = LB_ALGO_ROUND_ROBIN;
    g_coordinator.config.failover_threshold = 3;
    g_coordinator.config.failback_delay = 30;
    g_coordinator.config.health_check_interval = 5;
    g_coordinator.config.flow_timeout = 300;
    g_coordinator.config.max_flows = 1024;
    g_coordinator.config.flags = MULTI_NIC_FLAG_ENABLED | MULTI_NIC_FLAG_HEALTH_CHECK;
    
    /* Allocate flow table */
    g_coordinator.flow_table = (flow_entry_t*)memory_allocate(
        g_coordinator.config.max_flows * sizeof(flow_entry_t),
        MEMORY_TYPE_KERNEL
    );
    
    if (!g_coordinator.flow_table) {
        log_error("Failed to allocate flow table");
        return ERROR_MEMORY;
    }
    
    memset(g_coordinator.flow_table, 0, 
           g_coordinator.config.max_flows * sizeof(flow_entry_t));
    
    /* Initialize NIC entries */
    int i;
    for (i = 0; i < MAX_MULTI_NICS; i++) {
        g_coordinator.nics[i].nic_index = 0xFF;  /* Invalid */
        g_coordinator.nics[i].state = NIC_STATE_UNKNOWN;
    }
    
    g_coordinator.last_health_check = 0;
    g_coordinator.next_flow_id = 1;
    g_initialized = 1;
    
    log_info("Multi-NIC coordinator initialized with %s mode",
             g_coordinator.config.mode == MULTI_NIC_MODE_ACTIVE_ACTIVE ? 
             "active-active" : "active-standby");
    
    return SUCCESS;
}

/**
 * @brief Clean up multi-NIC coordination system
 */
int multi_nic_cleanup(void) {
    if (!g_initialized) {
        return SUCCESS;
    }
    
    log_info("Cleaning up multi-NIC coordinator");
    
    /* Free flow table */
    if (g_coordinator.flow_table) {
        memory_free(g_coordinator.flow_table);
        g_coordinator.flow_table = NULL;
    }
    
    /* Free NIC groups */
    int i;
    for (i = 0; i < MAX_NIC_GROUPS; i++) {
        if (g_coordinator.groups[i].members) {
            memory_free(g_coordinator.groups[i].members);
            g_coordinator.groups[i].members = NULL;
        }
    }
    
    memset(&g_coordinator, 0, sizeof(multi_nic_coordinator_t));
    g_initialized = 0;
    
    return SUCCESS;
}

/**
 * @brief Register a NIC with the coordinator
 */
int multi_nic_register(uint8_t nic_index, const nic_capabilities_t *caps) {
    if (!g_initialized || nic_index >= MAX_NICS || !caps) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Find free slot */
    int slot = -1;
    int i;
    for (i = 0; i < MAX_MULTI_NICS; i++) {
        if (g_coordinator.nics[i].nic_index == 0xFF) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        log_error("No free NIC slots");
        return ERROR_NO_RESOURCES;
    }
    
    /* Initialize NIC entry */
    nic_entry_t *nic = &g_coordinator.nics[slot];
    nic->nic_index = nic_index;
    nic->state = NIC_STATE_DOWN;
    nic->priority = 100;  /* Default priority */
    nic->weight = 1;      /* Default weight */
    memcpy(&nic->capabilities, caps, sizeof(nic_capabilities_t));
    
    /* Clear statistics */
    memset(&nic->stats, 0, sizeof(nic_stats_t));
    
    /* Set role based on mode */
    if (g_coordinator.config.mode == MULTI_NIC_MODE_ACTIVE_STANDBY) {
        nic->role = (g_coordinator.active_nic_count == 0) ? 
                    NIC_ROLE_PRIMARY : NIC_ROLE_STANDBY;
    } else {
        nic->role = NIC_ROLE_ACTIVE;
    }
    
    g_coordinator.nic_count++;
    
    log_info("Registered NIC %u (slot %d) with role %s",
             nic_index, slot,
             nic->role == NIC_ROLE_PRIMARY ? "PRIMARY" :
             nic->role == NIC_ROLE_STANDBY ? "STANDBY" : "ACTIVE");
    
    return SUCCESS;
}

/**
 * @brief Unregister a NIC from the coordinator
 */
int multi_nic_unregister(uint8_t nic_index) {
    if (!g_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    /* Find NIC entry */
    nic_entry_t *nic = multi_nic_find_entry(nic_index);
    if (!nic) {
        return ERROR_INVALID_NIC;
    }
    
    /* Handle failover if this was active NIC */
    if (nic->state == NIC_STATE_UP && nic->role != NIC_ROLE_STANDBY) {
        multi_nic_handle_failure(nic_index);
    }
    
    /* Clear flows using this NIC */
    int i;
    for (i = 0; i < g_coordinator.config.max_flows; i++) {
        if (g_coordinator.flow_table[i].nic_index == nic_index) {
            memset(&g_coordinator.flow_table[i], 0, sizeof(flow_entry_t));
            g_coordinator.flow_count--;
        }
    }
    
    /* Clear NIC entry */
    nic->nic_index = 0xFF;
    nic->state = NIC_STATE_UNKNOWN;
    g_coordinator.nic_count--;
    
    if (nic->state == NIC_STATE_UP) {
        g_coordinator.active_nic_count--;
    }
    
    log_info("Unregistered NIC %u", nic_index);
    
    return SUCCESS;
}

/**
 * @brief Update NIC state
 */
int multi_nic_update_state(uint8_t nic_index, uint8_t new_state) {
    if (!g_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    nic_entry_t *nic = multi_nic_find_entry(nic_index);
    if (!nic) {
        return ERROR_INVALID_NIC;
    }
    
    uint8_t old_state = nic->state;
    
    if (old_state == new_state) {
        return SUCCESS;
    }
    
    log_info("NIC %u state change: %s -> %s",
             nic_index,
             multi_nic_state_name(old_state),
             multi_nic_state_name(new_state));
    
    nic->state = new_state;
    nic->last_state_change = get_system_time();
    
    /* Update active count */
    if (old_state == NIC_STATE_UP) {
        g_coordinator.active_nic_count--;
    }
    if (new_state == NIC_STATE_UP) {
        g_coordinator.active_nic_count++;
    }
    
    /* Handle state transitions */
    if (new_state == NIC_STATE_DOWN || new_state == NIC_STATE_ERROR) {
        /* NIC went down - trigger failover */
        multi_nic_handle_failure(nic_index);
    } else if (new_state == NIC_STATE_UP && old_state != NIC_STATE_UP) {
        /* NIC came up - consider for failback */
        if (nic->role == NIC_ROLE_PRIMARY && 
            g_coordinator.config.flags & MULTI_NIC_FLAG_AUTO_FAILBACK) {
            multi_nic_schedule_failback(nic_index);
        }
    }
    
    /* Update statistics */
    g_coordinator.stats.state_changes++;
    
    return SUCCESS;
}

/**
 * @brief Select NIC for packet transmission
 */
int multi_nic_select_tx(packet_context_t *context, uint8_t *selected_nic) {
    if (!g_initialized || !context || !selected_nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Check if flow exists */
    flow_entry_t *flow = multi_nic_find_flow(context);
    if (flow && flow->nic_index != 0xFF) {
        nic_entry_t *nic = multi_nic_find_entry(flow->nic_index);
        if (nic && nic->state == NIC_STATE_UP) {
            *selected_nic = flow->nic_index;
            flow->packet_count++;
            flow->last_activity = get_system_time();
            g_coordinator.stats.flow_hits++;
            return SUCCESS;
        }
    }
    
    /* Select based on mode */
    int result = ERROR_NO_ROUTE;
    
    switch (g_coordinator.config.mode) {
        case MULTI_NIC_MODE_ACTIVE_STANDBY:
            result = multi_nic_select_active_standby(selected_nic);
            break;
            
        case MULTI_NIC_MODE_ACTIVE_ACTIVE:
            result = multi_nic_select_active_active(context, selected_nic);
            break;
            
        case MULTI_NIC_MODE_LOAD_BALANCE:
            result = multi_nic_select_load_balance(context, selected_nic);
            break;
            
        case MULTI_NIC_MODE_LACP:
            result = multi_nic_select_lacp(context, selected_nic);
            break;
            
        default:
            log_error("Invalid multi-NIC mode: %u", g_coordinator.config.mode);
            result = ERROR_INVALID_CONFIG;
            break;
    }
    
    if (result == SUCCESS) {
        /* Create or update flow entry */
        multi_nic_create_flow(context, *selected_nic);
        g_coordinator.stats.packets_routed++;
    } else {
        g_coordinator.stats.routing_failures++;
    }
    
    return result;
}

/**
 * @brief Handle NIC failure
 */
int multi_nic_handle_failure(uint8_t failed_nic) {
    if (!g_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    nic_entry_t *nic = multi_nic_find_entry(failed_nic);
    if (!nic) {
        return ERROR_INVALID_NIC;
    }
    
    log_warning("Handling failure of NIC %u (role=%s)",
                failed_nic, multi_nic_role_name(nic->role));
    
    nic->consecutive_failures++;
    g_coordinator.stats.failovers++;
    
    /* Find replacement NIC */
    uint8_t replacement = 0xFF;
    
    if (nic->role == NIC_ROLE_PRIMARY || nic->role == NIC_ROLE_ACTIVE) {
        /* Find best standby or active NIC */
        int best_priority = -1;
        
        int i;
        for (i = 0; i < MAX_MULTI_NICS; i++) {
            nic_entry_t *candidate = &g_coordinator.nics[i];
            
            if (candidate->nic_index != 0xFF &&
                candidate->nic_index != failed_nic &&
                candidate->state == NIC_STATE_UP &&
                candidate->priority > best_priority) {
                
                replacement = candidate->nic_index;
                best_priority = candidate->priority;
            }
        }
        
        if (replacement != 0xFF) {
            /* Promote replacement NIC */
            nic_entry_t *new_primary = multi_nic_find_entry(replacement);
            if (new_primary) {
                new_primary->role = nic->role;
                nic->role = NIC_ROLE_STANDBY;
                
                log_info("Failover: NIC %u -> NIC %u",
                         failed_nic, replacement);
                
                /* Migrate flows */
                multi_nic_migrate_flows(failed_nic, replacement);
            }
        } else {
            log_error("No replacement NIC available for failover");
            return ERROR_NO_RESOURCES;
        }
    }
    
    return SUCCESS;
}

/**
 * @brief Perform health check on all NICs
 */
int multi_nic_health_check(void) {
    if (!g_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    uint32_t now = get_system_time();
    
    /* Check if it's time for health check */
    if (now - g_coordinator.last_health_check < g_coordinator.config.health_check_interval) {
        return SUCCESS;
    }
    
    g_coordinator.last_health_check = now;
    
    int i;
    for (i = 0; i < MAX_MULTI_NICS; i++) {
        nic_entry_t *nic = &g_coordinator.nics[i];
        
        if (nic->nic_index == 0xFF) {
            continue;
        }
        
        /* Perform NIC-specific health check */
        int healthy = multi_nic_check_nic_health(nic);
        
        if (healthy) {
            if (nic->state != NIC_STATE_UP) {
                multi_nic_update_state(nic->nic_index, NIC_STATE_UP);
            }
            nic->consecutive_failures = 0;
        } else {
            nic->consecutive_failures++;
            
            if (nic->consecutive_failures >= g_coordinator.config.failover_threshold) {
                multi_nic_update_state(nic->nic_index, NIC_STATE_ERROR);
            }
        }
    }
    
    /* Clean up expired flows */
    multi_nic_cleanup_flows();
    
    g_coordinator.stats.health_checks++;
    
    return SUCCESS;
}

/**
 * @brief Create or add NIC group
 */
int multi_nic_create_group(uint8_t group_id, const char *name, uint8_t type) {
    if (!g_initialized || group_id >= MAX_NIC_GROUPS || !name) {
        return ERROR_INVALID_PARAM;
    }
    
    nic_group_t *group = &g_coordinator.groups[group_id];
    
    if (group->group_id != 0xFF) {
        log_warning("Group %u already exists", group_id);
        return ERROR_ALREADY_EXISTS;
    }
    
    /* Initialize group */
    group->group_id = group_id;
    strncpy(group->name, name, sizeof(group->name) - 1);
    group->type = type;
    group->member_count = 0;
    group->active_members = 0;
    
    /* Allocate member array */
    group->members = (uint8_t*)memory_allocate(
        MAX_MULTI_NICS * sizeof(uint8_t),
        MEMORY_TYPE_KERNEL
    );
    
    if (!group->members) {
        log_error("Failed to allocate group members");
        return ERROR_MEMORY;
    }
    
    memset(group->members, 0xFF, MAX_MULTI_NICS * sizeof(uint8_t));
    
    g_coordinator.group_count++;
    
    log_info("Created NIC group %u: %s (type=%u)", group_id, name, type);
    
    return SUCCESS;
}

/**
 * @brief Add NIC to group
 */
int multi_nic_add_to_group(uint8_t group_id, uint8_t nic_index) {
    if (!g_initialized || group_id >= MAX_NIC_GROUPS) {
        return ERROR_INVALID_PARAM;
    }
    
    nic_group_t *group = &g_coordinator.groups[group_id];
    if (group->group_id == 0xFF) {
        log_error("Group %u does not exist", group_id);
        return ERROR_NOT_FOUND;
    }
    
    nic_entry_t *nic = multi_nic_find_entry(nic_index);
    if (!nic) {
        return ERROR_INVALID_NIC;
    }
    
    /* Check if already in group */
    int i;
    for (i = 0; i < group->member_count; i++) {
        if (group->members[i] == nic_index) {
            log_warning("NIC %u already in group %u", nic_index, group_id);
            return ERROR_ALREADY_EXISTS;
        }
    }
    
    /* Add to group */
    if (group->member_count >= MAX_MULTI_NICS) {
        log_error("Group %u is full", group_id);
        return ERROR_NO_RESOURCES;
    }
    
    group->members[group->member_count++] = nic_index;
    
    if (nic->state == NIC_STATE_UP) {
        group->active_members++;
    }
    
    log_info("Added NIC %u to group %u", nic_index, group_id);
    
    return SUCCESS;
}

/**
 * @brief Get multi-NIC statistics
 */
void multi_nic_get_stats(multi_nic_stats_t *stats) {
    if (!g_initialized || !stats) {
        return;
    }
    
    memcpy(stats, &g_coordinator.stats, sizeof(multi_nic_stats_t));
}

/**
 * @brief Dump multi-NIC status
 */
void multi_nic_dump_status(void) {
    if (!g_initialized) {
        printf("Multi-NIC coordinator not initialized\n");
        return;
    }
    
    printf("\n=== Multi-NIC Coordination Status ===\n");
    printf("Mode: %s\n", 
           g_coordinator.config.mode == MULTI_NIC_MODE_ACTIVE_STANDBY ? "Active-Standby" :
           g_coordinator.config.mode == MULTI_NIC_MODE_ACTIVE_ACTIVE ? "Active-Active" :
           g_coordinator.config.mode == MULTI_NIC_MODE_LOAD_BALANCE ? "Load Balance" : "LACP");
    
    printf("NICs: %u registered, %u active\n",
           g_coordinator.nic_count, g_coordinator.active_nic_count);
    
    printf("\nNIC Status:\n");
    printf("Index | State | Role      | Priority | Failures | Packets\n");
    printf("------|-------|-----------|----------|----------|---------\n");
    
    int i;
    for (i = 0; i < MAX_MULTI_NICS; i++) {
        nic_entry_t *nic = &g_coordinator.nics[i];
        if (nic->nic_index != 0xFF) {
            printf("%5u | %5s | %9s | %8u | %8u | %7lu\n",
                   nic->nic_index,
                   multi_nic_state_name(nic->state),
                   multi_nic_role_name(nic->role),
                   nic->priority,
                   nic->consecutive_failures,
                   nic->stats.packets_sent + nic->stats.packets_received);
        }
    }
    
    printf("\nStatistics:\n");
    printf("  Packets routed: %lu\n", g_coordinator.stats.packets_routed);
    printf("  Flow hits: %lu\n", g_coordinator.stats.flow_hits);
    printf("  Failovers: %lu\n", g_coordinator.stats.failovers);
    printf("  Routing failures: %lu\n", g_coordinator.stats.routing_failures);
    printf("  Health checks: %lu\n", g_coordinator.stats.health_checks);
    printf("  Active flows: %u/%u\n", g_coordinator.flow_count, g_coordinator.config.max_flows);
    printf("\n");
}

/* Internal helper functions */

static nic_entry_t* multi_nic_find_entry(uint8_t nic_index) {
    int i;
    for (i = 0; i < MAX_MULTI_NICS; i++) {
        if (g_coordinator.nics[i].nic_index == nic_index) {
            return &g_coordinator.nics[i];
        }
    }
    return NULL;
}

static flow_entry_t* multi_nic_find_flow(packet_context_t *context) {
    uint32_t hash = multi_nic_hash_flow(context);
    
    int i;
    for (i = 0; i < g_coordinator.config.max_flows; i++) {
        flow_entry_t *flow = &g_coordinator.flow_table[i];
        
        if (flow->flow_hash == hash && 
            flow->src_ip == context->src_ip &&
            flow->dst_ip == context->dst_ip &&
            flow->src_port == context->src_port &&
            flow->dst_port == context->dst_port &&
            flow->protocol == context->protocol) {
            return flow;
        }
    }
    return NULL;
}

static int multi_nic_create_flow(packet_context_t *context, uint8_t nic_index) {
    /* Find free slot or LRU entry */
    flow_entry_t *flow = NULL;
    uint32_t oldest_time = 0xFFFFFFFF;
    int lru_index = -1;
    
    int i;
    for (i = 0; i < g_coordinator.config.max_flows; i++) {
        if (g_coordinator.flow_table[i].flow_id == 0) {
            flow = &g_coordinator.flow_table[i];
            break;
        }
        
        if (g_coordinator.flow_table[i].last_activity < oldest_time) {
            oldest_time = g_coordinator.flow_table[i].last_activity;
            lru_index = i;
        }
    }
    
    if (!flow && lru_index >= 0) {
        flow = &g_coordinator.flow_table[lru_index];
        g_coordinator.flow_count--;
    }
    
    if (!flow) {
        return ERROR_NO_RESOURCES;
    }
    
    /* Initialize flow entry */
    flow->flow_id = g_coordinator.next_flow_id++;
    flow->flow_hash = multi_nic_hash_flow(context);
    flow->src_ip = context->src_ip;
    flow->dst_ip = context->dst_ip;
    flow->src_port = context->src_port;
    flow->dst_port = context->dst_port;
    flow->protocol = context->protocol;
    flow->nic_index = nic_index;
    flow->created = get_system_time();
    flow->last_activity = flow->created;
    flow->packet_count = 1;
    
    g_coordinator.flow_count++;
    
    return SUCCESS;
}

static void multi_nic_migrate_flows(uint8_t from_nic, uint8_t to_nic) {
    int migrated = 0;
    
    int i;
    for (i = 0; i < g_coordinator.config.max_flows; i++) {
        if (g_coordinator.flow_table[i].nic_index == from_nic) {
            g_coordinator.flow_table[i].nic_index = to_nic;
            migrated++;
        }
    }
    
    if (migrated > 0) {
        log_info("Migrated %d flows from NIC %u to NIC %u", 
                 migrated, from_nic, to_nic);
    }
}

static void multi_nic_cleanup_flows(void) {
    uint32_t now = get_system_time();
    int expired = 0;
    
    int i;
    for (i = 0; i < g_coordinator.config.max_flows; i++) {
        flow_entry_t *flow = &g_coordinator.flow_table[i];
        
        if (flow->flow_id != 0 &&
            (now - flow->last_activity) > g_coordinator.config.flow_timeout) {
            memset(flow, 0, sizeof(flow_entry_t));
            g_coordinator.flow_count--;
            expired++;
        }
    }
    
    if (expired > 0) {
        log_debug("Expired %d inactive flows", expired);
    }
}

static int multi_nic_check_nic_health(nic_entry_t *nic) {
    /* Simplified health check - would normally test actual NIC */
    /* Check if NIC is responding, link is up, etc. */
    
    /* For now, assume healthy if not explicitly marked as down */
    return (nic->state != NIC_STATE_DOWN && nic->state != NIC_STATE_ERROR) ? 1 : 0;
}

static int multi_nic_select_active_standby(uint8_t *selected_nic) {
    /* Find primary NIC */
    int i;
    for (i = 0; i < MAX_MULTI_NICS; i++) {
        nic_entry_t *nic = &g_coordinator.nics[i];
        
        if (nic->nic_index != 0xFF &&
            nic->state == NIC_STATE_UP &&
            nic->role == NIC_ROLE_PRIMARY) {
            *selected_nic = nic->nic_index;
            return SUCCESS;
        }
    }
    
    /* Fallback to any active NIC */
    for (i = 0; i < MAX_MULTI_NICS; i++) {
        nic_entry_t *nic = &g_coordinator.nics[i];
        
        if (nic->nic_index != 0xFF &&
            nic->state == NIC_STATE_UP) {
            *selected_nic = nic->nic_index;
            return SUCCESS;
        }
    }
    
    return ERROR_NO_ROUTE;
}

static int multi_nic_select_active_active(packet_context_t *context, uint8_t *selected_nic) {
    /* Use load balancing for active-active */
    return multi_nic_select_load_balance(context, selected_nic);
}

static int multi_nic_select_load_balance(packet_context_t *context, uint8_t *selected_nic) {
    uint8_t algo = g_coordinator.config.load_balance_algo;
    
    /* Bounds check - critical safety fix */
    if (algo >= LB_ALGO_COUNT) {
        log_error("Invalid load balance algorithm: %u", algo);
        return ERROR_INVALID_CONFIG;
    }
    
    /* Call load balance function with proper signature */
    uint8_t selected = 0xFF;
    int result = g_load_balance_funcs[algo](context, &selected);
    if (result == SUCCESS) {
        *selected_nic = selected;
    }
    return result;
}

static int multi_nic_select_lacp(packet_context_t *context, uint8_t *selected_nic) {
    /* LACP would require 802.3ad protocol implementation */
    /* For now, use hash-based selection */
    return load_balance_hash_based(context, selected_nic);
}

static void multi_nic_schedule_failback(uint8_t nic_index) {
    /* Schedule failback after delay */
    /* This would typically use a timer mechanism */
    log_info("Scheduling failback to NIC %u after %u seconds",
             nic_index, g_coordinator.config.failback_delay);
}

static uint32_t multi_nic_hash_flow(packet_context_t *context) {
    /* Simple hash function for flow identification */
    uint32_t hash = context->src_ip;
    hash ^= context->dst_ip;
    hash ^= (context->src_port << 16) | context->dst_port;
    hash ^= context->protocol;
    
    /* Mix bits */
    hash ^= (hash >> 16);
    hash *= 0x85ebca6b;
    hash ^= (hash >> 13);
    hash *= 0xc2b2ae35;
    hash ^= (hash >> 16);
    
    return hash;
}

static const char* multi_nic_state_name(uint8_t state) {
    switch (state) {
        case NIC_STATE_DOWN: return "DOWN";
        case NIC_STATE_UP: return "UP";
        case NIC_STATE_ERROR: return "ERROR";
        case NIC_STATE_TESTING: return "TEST";
        default: return "UNKNOWN";
    }
}

static const char* multi_nic_role_name(uint8_t role) {
    switch (role) {
        case NIC_ROLE_PRIMARY: return "PRIMARY";
        case NIC_ROLE_STANDBY: return "STANDBY";
        case NIC_ROLE_ACTIVE: return "ACTIVE";
        case NIC_ROLE_PASSIVE: return "PASSIVE";
        default: return "UNKNOWN";
    }
}

/* Load balancing algorithm implementations */

static int load_balance_round_robin(packet_context_t *context, uint8_t *selected_nic) {
    static uint8_t next_nic = 0;
    uint8_t start = next_nic;
    
    /* Input validation */
    if (!context || !selected_nic) {
        return ERROR_INVALID_PARAM;
    }
    
    do {
        if (next_nic >= MAX_MULTI_NICS) next_nic = 0;  /* Bounds check */
        nic_entry_t *nic = &g_coordinator.nics[next_nic];
        next_nic = (next_nic + 1) % MAX_MULTI_NICS;
        
        if (nic->nic_index != 0xFF && nic->state == NIC_STATE_UP) {
            *selected_nic = nic->nic_index;
            return SUCCESS;
        }
    } while (next_nic != start);
    
    return ERROR_NO_ROUTE;
}

static int load_balance_weighted(packet_context_t *context, uint8_t *selected_nic) {
    /* Input validation */
    if (!context || !selected_nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Select based on weight */
    uint32_t total_weight = 0;
    
    int i;
    for (i = 0; i < MAX_MULTI_NICS; i++) {
        nic_entry_t *nic = &g_coordinator.nics[i];
        if (nic->nic_index != 0xFF && nic->state == NIC_STATE_UP) {
            total_weight += nic->weight;
        }
    }
    
    if (total_weight == 0) {
        return ERROR_NO_ROUTE;
    }
    
    /* Random selection based on weight */
    uint32_t random = (get_system_time() * 1103515245 + 12345) % total_weight;
    uint32_t cumulative = 0;
    
    for (i = 0; i < MAX_MULTI_NICS; i++) {
        nic_entry_t *nic = &g_coordinator.nics[i];
        if (nic->nic_index != 0xFF && nic->state == NIC_STATE_UP) {
            cumulative += nic->weight;
            if (random < cumulative) {
                *selected_nic = nic->nic_index;
                return SUCCESS;
            }
        }
    }
    
    return ERROR_NO_ROUTE;
}

static int load_balance_least_loaded(packet_context_t *context, uint8_t *selected_nic) {
    /* Input validation */
    if (!context || !selected_nic) {
        return ERROR_INVALID_PARAM;
    }
    
    uint8_t best_nic = 0xFF;
    uint32_t min_load = 0xFFFFFFFF;
    
    int i;
    for (i = 0; i < MAX_MULTI_NICS; i++) {
        nic_entry_t *nic = &g_coordinator.nics[i];
        
        if (nic->nic_index != 0xFF && nic->state == NIC_STATE_UP) {
            uint32_t load = nic->stats.packets_sent + nic->stats.packets_queued;
            
            if (load < min_load) {
                min_load = load;
                best_nic = nic->nic_index;
            }
        }
    }
    
    if (best_nic != 0xFF) {
        *selected_nic = best_nic;
        return SUCCESS;
    }
    
    return ERROR_NO_ROUTE;
}

static int load_balance_hash_based(packet_context_t *context, uint8_t *selected_nic) {
    /* Input validation */
    if (!context || !selected_nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Use simpler 16-bit hash for DOS performance */
    uint16_t hash = (uint16_t)(context->src_ip ^ context->dst_ip);
    hash ^= (uint16_t)((context->src_port << 8) | (context->dst_port >> 8));
    hash ^= (uint16_t)context->protocol;
    
    /* Count active NICs */
    uint8_t active_nics[MAX_MULTI_NICS];
    int active_count = 0;
    
    int i;
    for (i = 0; i < MAX_MULTI_NICS; i++) {
        nic_entry_t *nic = &g_coordinator.nics[i];
        if (nic->nic_index != 0xFF && nic->state == NIC_STATE_UP) {
            active_nics[active_count++] = nic->nic_index;
        }
    }
    
    if (active_count == 0) {
        return ERROR_NO_ROUTE;
    }
    
    /* Select based on hash */
    *selected_nic = active_nics[hash % active_count];
    return SUCCESS;
}

static int load_balance_adaptive(packet_context_t *context, uint8_t *selected_nic) {
    /* Input validation */
    if (!context || !selected_nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Adaptive algorithm based on performance metrics - using integer math for DOS */
    uint8_t best_nic = 0xFF;
    uint32_t best_score = 0;
    
    int i;
    for (i = 0; i < MAX_MULTI_NICS; i++) {
        nic_entry_t *nic = &g_coordinator.nics[i];
        
        if (nic->nic_index != 0xFF && nic->state == NIC_STATE_UP) {
            /* Calculate performance score using integer math */
            uint32_t total_packets = nic->stats.packets_sent + 1;
            uint32_t error_rate = (nic->stats.errors * 100) / total_packets;  /* Percentage */
            
            uint32_t max_queue = (nic->capabilities.max_queue_size > 0) ? 
                                nic->capabilities.max_queue_size : 1;
            uint32_t utilization = (nic->stats.packets_queued * 100) / max_queue;
            
            /* Score = (100 - error_rate) * (100 - utilization) * priority / 100 */
            uint32_t score = ((100 - error_rate) * (100 - utilization) * nic->priority) / 100;
            
            if (score > best_score) {
                best_score = score;
                best_nic = nic->nic_index;
            }
        }
    }
    
    if (best_nic != 0xFF) {
        *selected_nic = best_nic;
        return SUCCESS;
    }
    
    return ERROR_NO_ROUTE;
}
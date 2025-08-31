/**
 * Test Vector Generator for 3Com NIC Emulation
 * 
 * This tool generates test vectors that capture expected hardware behavior
 * for various operations. These vectors will be used to validate QEMU
 * emulation accuracy.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* Test Vector Types */
typedef enum {
    TEST_RESET_SEQUENCE,
    TEST_EEPROM_READ,
    TEST_WINDOW_SWITCH,
    TEST_PACKET_TX,
    TEST_PACKET_RX,
    TEST_INTERRUPT_HANDLING,
    TEST_DMA_OPERATION,
    TEST_MII_ACCESS,
    TEST_AUTO_NEGOTIATION,
    TEST_ERROR_RECOVERY
} test_type_t;

/* I/O Operation Record */
typedef struct {
    uint16_t offset;
    uint16_t value;
    uint8_t is_write;  /* 0=read, 1=write */
    uint8_t width;     /* 1=byte, 2=word, 4=dword */
    uint32_t timestamp_us;  /* Microseconds since test start */
} io_operation_t;

/* Test Vector */
typedef struct {
    char name[64];
    test_type_t type;
    uint16_t io_base;
    io_operation_t *operations;
    uint32_t op_count;
    uint32_t op_capacity;
    uint8_t expected_result[256];
    uint16_t result_length;
} test_vector_t;

/* Global test vectors */
static test_vector_t *vectors = NULL;
static uint32_t vector_count = 0;
static uint32_t vector_capacity = 0;

/* Add I/O operation to test vector */
static void add_io_op(test_vector_t *vec, uint16_t offset, uint16_t value, 
                      uint8_t is_write, uint8_t width, uint32_t timestamp) {
    if (vec->op_count >= vec->op_capacity) {
        vec->op_capacity = vec->op_capacity ? vec->op_capacity * 2 : 16;
        vec->operations = realloc(vec->operations, 
                                 vec->op_capacity * sizeof(io_operation_t));
    }
    
    io_operation_t *op = &vec->operations[vec->op_count++];
    op->offset = offset;
    op->value = value;
    op->is_write = is_write;
    op->width = width;
    op->timestamp_us = timestamp;
}

/* Create new test vector */
static test_vector_t* create_vector(const char *name, test_type_t type, uint16_t io_base) {
    if (vector_count >= vector_capacity) {
        vector_capacity = vector_capacity ? vector_capacity * 2 : 8;
        vectors = realloc(vectors, vector_capacity * sizeof(test_vector_t));
    }
    
    test_vector_t *vec = &vectors[vector_count++];
    memset(vec, 0, sizeof(test_vector_t));
    strncpy(vec->name, name, sizeof(vec->name) - 1);
    vec->type = type;
    vec->io_base = io_base;
    
    return vec;
}

/* Generate 3C509B Reset Sequence */
static void generate_3c509b_reset(void) {
    test_vector_t *vec = create_vector("3C509B_Reset", TEST_RESET_SEQUENCE, 0x300);
    uint32_t t = 0;
    
    /* Issue reset command */
    add_io_op(vec, 0x0E, 0x0000, 1, 2, t);  /* TOTAL_RESET */
    t += 2000;  /* 2ms delay */
    
    /* Select window 0 */
    add_io_op(vec, 0x0E, 0x0800, 1, 2, t);  /* SELECT_WINDOW(0) */
    t += 10;
    
    /* Read status to verify reset complete */
    add_io_op(vec, 0x0E, 0x0000, 0, 2, t);  /* Read status (expect 0) */
    t += 10;
    
    /* Expected result: Status register cleared */
    vec->expected_result[0] = 0x00;
    vec->expected_result[1] = 0x00;
    vec->result_length = 2;
}

/* Generate 3C509B EEPROM Read */
static void generate_3c509b_eeprom_read(void) {
    test_vector_t *vec = create_vector("3C509B_EEPROM_MAC", TEST_EEPROM_READ, 0x300);
    uint32_t t = 0;
    
    /* Select window 0 */
    add_io_op(vec, 0x0E, 0x0800, 1, 2, t);
    t += 10;
    
    /* Read MAC address from EEPROM */
    for (int i = 0; i < 3; i++) {
        /* Issue EEPROM read command */
        add_io_op(vec, 0x0A, 0x80 | i, 1, 2, t);
        t += 200;  /* 200us EEPROM delay */
        
        /* Read EEPROM data */
        add_io_op(vec, 0x0C, 0x0000, 0, 2, t);  /* Will contain MAC bytes */
        t += 10;
    }
    
    /* Expected result: Valid 3Com OUI (00:50:04) */
    vec->expected_result[0] = 0x00;
    vec->expected_result[1] = 0x50;
    vec->expected_result[2] = 0x04;
    vec->result_length = 6;
}

/* Generate Window Switching Test */
static void generate_window_switch(void) {
    test_vector_t *vec = create_vector("3C509B_Window_Switch", TEST_WINDOW_SWITCH, 0x300);
    uint32_t t = 0;
    
    /* Test all 8 windows */
    for (int win = 0; win < 8; win++) {
        /* Select window */
        add_io_op(vec, 0x0E, 0x0800 | win, 1, 2, t);
        t += 10;
        
        /* Verify window-specific register access */
        if (win == 1) {
            /* Window 1: Read TX_FREE register */
            add_io_op(vec, 0x0C, 0x0000, 0, 2, t);
            t += 10;
        } else if (win == 6) {
            /* Window 6: Read statistics */
            add_io_op(vec, 0x00, 0x0000, 0, 1, t);
            t += 10;
        }
    }
}

/* Generate Simple Packet Transmission */
static void generate_packet_tx(void) {
    test_vector_t *vec = create_vector("3C509B_Packet_TX", TEST_PACKET_TX, 0x300);
    uint32_t t = 0;
    uint16_t packet_len = 64;  /* Minimum Ethernet frame */
    
    /* Select window 1 */
    add_io_op(vec, 0x0E, 0x0801, 1, 2, t);
    t += 10;
    
    /* Check TX_FREE space */
    add_io_op(vec, 0x0C, 0x07FF, 0, 2, t);  /* Read TX_FREE (expect >64) */
    t += 10;
    
    /* Write packet length to TX FIFO */
    add_io_op(vec, 0x00, packet_len, 1, 2, t);
    t += 10;
    
    /* Write packet data (first few bytes) */
    add_io_op(vec, 0x00, 0xFFFF, 1, 2, t);  /* Dest MAC[0:1] = FF:FF */
    t += 5;
    add_io_op(vec, 0x00, 0xFFFF, 1, 2, t);  /* Dest MAC[2:3] = FF:FF */
    t += 5;
    add_io_op(vec, 0x00, 0xFFFF, 1, 2, t);  /* Dest MAC[4:5] = FF:FF */
    t += 5;
    
    /* Source MAC */
    add_io_op(vec, 0x00, 0x0050, 1, 2, t);  /* Src MAC[0:1] = 00:50 */
    t += 5;
    add_io_op(vec, 0x00, 0x0401, 1, 2, t);  /* Src MAC[2:3] = 04:01 */
    t += 5;
    add_io_op(vec, 0x00, 0x0203, 1, 2, t);  /* Src MAC[4:5] = 02:03 */
    t += 5;
    
    /* EtherType */
    add_io_op(vec, 0x00, 0x0800, 1, 2, t);  /* IPv4 */
    t += 5;
    
    /* Padding (to reach 64 bytes) */
    for (int i = 14; i < packet_len/2; i++) {
        add_io_op(vec, 0x00, 0x0000, 1, 2, t);
        t += 5;
    }
    
    /* Wait for TX complete */
    t += 1000;  /* 1ms for transmission */
    
    /* Read TX status */
    add_io_op(vec, 0x0B, 0x0001, 0, 1, t);  /* Expect COMPLETE bit */
    
    /* Expected: TX complete status */
    vec->expected_result[0] = 0x01;  /* TX_COMPLETE */
    vec->result_length = 1;
}

/* Generate Interrupt Handling Sequence */
static void generate_interrupt_test(void) {
    test_vector_t *vec = create_vector("3C509B_Interrupt", TEST_INTERRUPT_HANDLING, 0x300);
    uint32_t t = 0;
    
    /* Enable interrupts */
    add_io_op(vec, 0x0E, 0x7098, 1, 2, t);  /* SET_INTR_ENB */
    t += 10;
    
    /* Simulate TX complete interrupt */
    t += 1000;
    
    /* Read interrupt status */
    add_io_op(vec, 0x0E, 0x0004, 0, 2, t);  /* Read status (TX_COMPLETE) */
    t += 10;
    
    /* Acknowledge interrupt */
    add_io_op(vec, 0x0E, 0x6804, 1, 2, t);  /* ACK_INTR(TX_COMPLETE) */
    t += 10;
    
    /* Verify interrupt cleared */
    add_io_op(vec, 0x0E, 0x0000, 0, 2, t);  /* Status should be clear */
}

/* Generate 3C515 DMA Descriptor Setup */
static void generate_3c515_dma_setup(void) {
    test_vector_t *vec = create_vector("3C515_DMA_Setup", TEST_DMA_OPERATION, 0x300);
    uint32_t t = 0;
    
    /* Select window 7 (Bus Master) */
    add_io_op(vec, 0x0E, 0x0807, 1, 2, t);
    t += 10;
    
    /* Write RX descriptor list pointer */
    add_io_op(vec, 0x38, 0x1000, 1, 2, t);  /* UP_LIST_PTR low */
    t += 5;
    add_io_op(vec, 0x3A, 0x0010, 1, 2, t);  /* UP_LIST_PTR high */
    t += 5;
    
    /* Write TX descriptor list pointer */
    add_io_op(vec, 0x24, 0x2000, 1, 2, t);  /* DOWN_LIST_PTR low */
    t += 5;
    add_io_op(vec, 0x26, 0x0010, 1, 2, t);  /* DOWN_LIST_PTR high */
    t += 5;
    
    /* Start DMA engines */
    add_io_op(vec, 0x0E, 0x3001, 1, 2, t);  /* UP_UNSTALL */
    t += 10;
    add_io_op(vec, 0x0E, 0x3003, 1, 2, t);  /* DOWN_UNSTALL */
    t += 10;
    
    /* Verify DMA status */
    add_io_op(vec, 0x0C, 0x0800, 0, 2, t);  /* Read MASTER_STATUS */
}

/* Generate MII PHY Access */
static void generate_mii_access(void) {
    test_vector_t *vec = create_vector("3C515_MII_Access", TEST_MII_ACCESS, 0x300);
    uint32_t t = 0;
    
    /* Select window 4 */
    add_io_op(vec, 0x0E, 0x0804, 1, 2, t);
    t += 10;
    
    /* Read PHY ID (register 2) */
    uint32_t mii_cmd = 0x60000000 | (0 << 23) | (2 << 18);  /* Read PHY 0, Reg 2 */
    add_io_op(vec, 0x0800, mii_cmd & 0xFFFF, 1, 2, t);
    t += 5;
    add_io_op(vec, 0x0802, mii_cmd >> 16, 1, 2, t);
    t += 30;  /* MII transaction time */
    
    /* Read result */
    add_io_op(vec, 0x0800, 0x0000, 0, 2, t);  /* Low word */
    t += 5;
    add_io_op(vec, 0x0802, 0x0000, 0, 2, t);  /* High word (status) */
}

/* Generate Auto-Negotiation Sequence */
static void generate_auto_negotiation(void) {
    test_vector_t *vec = create_vector("3C515_AutoNeg", TEST_AUTO_NEGOTIATION, 0x300);
    uint32_t t = 0;
    
    /* Select window 4 */
    add_io_op(vec, 0x0E, 0x0804, 1, 2, t);
    t += 10;
    
    /* Start auto-negotiation (write to PHY reg 0) */
    uint32_t mii_cmd = 0x50000000 | (0 << 23) | (0 << 18) | 0x1200;
    add_io_op(vec, 0x0A00, mii_cmd & 0xFFFF, 1, 2, t);
    t += 5;
    add_io_op(vec, 0x0A02, mii_cmd >> 16, 1, 2, t);
    t += 30;
    
    /* Poll for completion (read PHY reg 1) */
    for (int i = 0; i < 10; i++) {
        t += 100000;  /* 100ms between polls */
        mii_cmd = 0x60000000 | (0 << 23) | (1 << 18);
        add_io_op(vec, 0x0800, mii_cmd & 0xFFFF, 1, 2, t);
        t += 5;
        add_io_op(vec, 0x0802, mii_cmd >> 16, 1, 2, t);
        t += 30;
        add_io_op(vec, 0x0800, 0x0020, 0, 2, t);  /* Check ANEG_COMPLETE */
        
        if (i == 5) {  /* Simulate completion after 500ms */
            vec->expected_result[0] = 0x20;  /* ANEG_COMPLETE set */
            vec->result_length = 1;
            break;
        }
    }
}

/* Generate Error Recovery Sequence */
static void generate_error_recovery(void) {
    test_vector_t *vec = create_vector("3C509B_Error_Recovery", TEST_ERROR_RECOVERY, 0x300);
    uint32_t t = 0;
    
    /* Simulate adapter failure */
    add_io_op(vec, 0x0E, 0x0002, 0, 2, t);  /* Read status (ADAPTER_FAILURE) */
    t += 10;
    
    /* Recovery sequence */
    add_io_op(vec, 0x0E, 0x1800, 1, 2, t);  /* RX_DISABLE */
    t += 10;
    add_io_op(vec, 0x0E, 0x5000, 1, 2, t);  /* TX_DISABLE */
    t += 10;
    add_io_op(vec, 0x0E, 0x2800, 1, 2, t);  /* RX_RESET */
    t += 10;
    add_io_op(vec, 0x0E, 0x5800, 1, 2, t);  /* TX_RESET */
    t += 10;
    
    /* Re-enable operations */
    add_io_op(vec, 0x0E, 0x2000, 1, 2, t);  /* RX_ENABLE */
    t += 10;
    add_io_op(vec, 0x0E, 0x4800, 1, 2, t);  /* TX_ENABLE */
    t += 10;
    
    /* Clear error status */
    add_io_op(vec, 0x0E, 0x6802, 1, 2, t);  /* ACK_INTR(ADAPTER_FAILURE) */
}

/* Export test vectors to JSON */
static void export_json(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("Failed to create output file");
        return;
    }
    
    fprintf(fp, "{\n  \"test_vectors\": [\n");
    
    for (uint32_t i = 0; i < vector_count; i++) {
        test_vector_t *vec = &vectors[i];
        
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"name\": \"%s\",\n", vec->name);
        fprintf(fp, "      \"type\": %d,\n", vec->type);
        fprintf(fp, "      \"io_base\": \"0x%04X\",\n", vec->io_base);
        fprintf(fp, "      \"operations\": [\n");
        
        for (uint32_t j = 0; j < vec->op_count; j++) {
            io_operation_t *op = &vec->operations[j];
            fprintf(fp, "        {\n");
            fprintf(fp, "          \"offset\": \"0x%02X\",\n", op->offset);
            fprintf(fp, "          \"value\": \"0x%04X\",\n", op->value);
            fprintf(fp, "          \"is_write\": %s,\n", op->is_write ? "true" : "false");
            fprintf(fp, "          \"width\": %d,\n", op->width);
            fprintf(fp, "          \"timestamp_us\": %u\n", op->timestamp_us);
            fprintf(fp, "        }%s\n", (j < vec->op_count - 1) ? "," : "");
        }
        
        fprintf(fp, "      ],\n");
        fprintf(fp, "      \"expected_result\": [");
        for (uint16_t j = 0; j < vec->result_length; j++) {
            fprintf(fp, "\"0x%02X\"%s", vec->expected_result[j], 
                    (j < vec->result_length - 1) ? ", " : "");
        }
        fprintf(fp, "]\n");
        fprintf(fp, "    }%s\n", (i < vector_count - 1) ? "," : "");
    }
    
    fprintf(fp, "  ]\n}\n");
    fclose(fp);
    
    printf("Exported %u test vectors to %s\n", vector_count, filename);
}

/* Export test vectors to C header */
static void export_c_header(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("Failed to create header file");
        return;
    }
    
    fprintf(fp, "/* Auto-generated test vectors for 3Com NIC emulation */\n\n");
    fprintf(fp, "#ifndef TEST_VECTORS_H\n");
    fprintf(fp, "#define TEST_VECTORS_H\n\n");
    fprintf(fp, "#include <stdint.h>\n\n");
    
    /* Export each test vector as static data */
    for (uint32_t i = 0; i < vector_count; i++) {
        test_vector_t *vec = &vectors[i];
        char clean_name[64];
        
        /* Clean name for C identifier */
        for (int j = 0; vec->name[j]; j++) {
            clean_name[j] = (vec->name[j] == '-' || vec->name[j] == ' ') ? '_' : vec->name[j];
        }
        
        fprintf(fp, "/* %s */\n", vec->name);
        fprintf(fp, "static const io_operation_t %s_ops[] = {\n", clean_name);
        
        for (uint32_t j = 0; j < vec->op_count; j++) {
            io_operation_t *op = &vec->operations[j];
            fprintf(fp, "    { 0x%02X, 0x%04X, %u, %u, %u },\n",
                    op->offset, op->value, op->is_write, op->width, op->timestamp_us);
        }
        fprintf(fp, "};\n\n");
    }
    
    fprintf(fp, "#endif /* TEST_VECTORS_H */\n");
    fclose(fp);
    
    printf("Exported test vectors to %s\n", filename);
}

int main(int argc, char *argv[]) {
    printf("3Com NIC Test Vector Generator\n");
    printf("===============================\n\n");
    
    /* Generate all test vectors */
    printf("Generating 3C509B test vectors...\n");
    generate_3c509b_reset();
    generate_3c509b_eeprom_read();
    generate_window_switch();
    generate_packet_tx();
    generate_interrupt_test();
    generate_error_recovery();
    
    printf("Generating 3C515 test vectors...\n");
    generate_3c515_dma_setup();
    generate_mii_access();
    generate_auto_negotiation();
    
    /* Export results */
    export_json("test_vectors.json");
    export_c_header("test_vectors.h");
    
    printf("\nGenerated %u test vectors\n", vector_count);
    
    /* Cleanup */
    for (uint32_t i = 0; i < vector_count; i++) {
        free(vectors[i].operations);
    }
    free(vectors);
    
    return 0;
}
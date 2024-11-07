/**
 *  Copyright Notice:
 *  Copyright 2021-2022 DMTF. All rights reserved.
 *  License: BSD 3-Clause License. For full text see link: https://github.com/DMTF/libspdm/blob/main/LICENSE.md
 **/

#include "spdm_unit_test.h"

static libspdm_test_context_t *m_spdm_test_context;

static uint8_t m_send_receive_buffer[LIBSPDM_MAX_SENDER_RECEIVER_BUFFER_SIZE];

static bool m_sender_buffer_acquired = false;
static bool m_receiver_buffer_acquired = false;

static bool m_error_acquire_sender_buffer = false;
static bool m_error_acquire_receiver_buffer = false;

#if (LIBSPDM_ENABLE_CAPABILITY_MUT_AUTH_CAP) && (LIBSPDM_ENABLE_CAPABILITY_ENCAP_CAP) && \
    (LIBSPDM_SEND_GET_CERTIFICATE_SUPPORT)
static uint8_t m_cert_chain_buffer[SPDM_MAX_CERTIFICATE_CHAIN_SIZE];
#endif

#define I2C_WR_SLAVE_ADDR 0x3a
int aardvark_handle; // Global variable for Aardvark device handle 
uint8_t I2C_RD_SLAVE_ADDR=0x70; 
#define POLY (0x1070 << 3)  //polynomial for CRC-8 

void initialize_aardvark(){    
    aardvark_handle = aa_open(0);  // Open Aardvark device on port 0    
    printf("aardvark_handle: %d\n",aardvark_handle);
    aa_configure(aardvark_handle, AA_CONFIG_SPI_I2C);  // Configure for I2C    
    aa_i2c_pullup(aardvark_handle, AA_I2C_PULLUP_BOTH);  // Enable I2C pull-up resistors    
    aa_i2c_bitrate(aardvark_handle, 100);  // Set I2C bitrate to 100 kHz
}

uint8_t crc8(uint16_t data){
    for (int i = 0; i < 8; i++) {
        if (data & 0x8000) {
            data ^= POLY;
        }
        data <<= 1;
    }
    return (data >> 8) & 0xFF;
}
//Calculates PEC of a given message using CRC-8.
uint8_t calculate_pec(uint8_t *message,int message_size){
    uint8_t crc = crc8(I2C_WR_SLAVE_ADDR << 9);
    for (int i = 0; i < message_size; i++) {
        crc = crc8((crc ^ message[i]) << 8);
    }
    return crc;
}       

// Add SMBus and MCTP header to SPDM message.
uint8_t* modify_send_message(uint8_t *message_copy, size_t message_size, size_t *new_message_size){
    // Define the number of bytes to append at the beginning
    size_t mctp_smbus_header_size = 8; // 8 bytes of MCTP and SMBUS header
    size_t pec_size = 1; // 1 byte for PEC

    // MCTP SMBUS Header fields
    uint8_t command_code = 0x0f;
    uint8_t byte_count = 6 + message_size;
    uint8_t source_slave_addr = ((I2C_RD_SLAVE_ADDR<<1)|0x01);
    uint8_t mctp_hdr_ver = 0x01;
    uint8_t default_eid = 0x00 ;
    uint8_t default_s_eid = 0x10; 
    uint8_t default_eom_som_seq_num = 0xc8;
    uint8_t mctp_spdm_type = 0x05;

    uint8_t mctp_smbus_header[] = {command_code,byte_count,source_slave_addr,mctp_hdr_ver,default_eid,default_s_eid,default_eom_som_seq_num,mctp_spdm_type};

    // Calculate the new message size
    *new_message_size = mctp_smbus_header_size + message_size + pec_size;

    // Allocate memory for the new message
    uint8_t *new_message = (uint8_t *)malloc(*new_message_size);
    // if (new_message == NULL) {
    //     printf("Memory allocation failed.\n");
    //     return NULL;
    // }

    
    // Copy the mctp_smbus_header
    memcpy(new_message , mctp_smbus_header, mctp_smbus_header_size);

    // Copy the original message data
    memcpy(new_message + mctp_smbus_header_size, message_copy, message_size);

    // Calculate and add the PEC byte at the end
    uint8_t pec = calculate_pec(new_message, mctp_smbus_header_size + message_size);
    new_message[*new_message_size - 1] = pec;

    return new_message;
}   

// uint8_t* modify_receive_message(uint8_t *message_copy, size_t message_size, size_t *new_message_size){

//     // Calculate the new message size
//     *new_message_size = message_size - 9 + 1; // +1 for the extra byte at the beginning

//     // Append the extra byte 0x01 at the beginning
//     new_message[0] = 0x01;

//     // Copy the content of the message without the first 8 bytes and the last byte
//     memcpy(new_message + 1, message_copy + 8, *new_message_size - 1);

//     return new_message;
// }

//Print the content of message byte wise.
void print_message_aardvark(uint8_t *message, size_t message_size){
    for (size_t i = 0; i < message_size; ++i) {
        printf("%02x ", message[i]);
    }
    printf("\n");
}

//Function to send SPDM message
libspdm_return_t libspdm_device_send_message_aardvark(void *spdm_context, size_t message_size, const void *message, uint64_t timeout){
    uint8_t message_copy[message_size];
    memcpy(message_copy, message, message_size);

    printf("Request message:\n");
    print_message_aardvark(message_copy,message_size);

    size_t *new_message_size;
    uint8_t *new_message = modify_send_message(message_copy+1,message_size-1,new_message_size);
    printf("Request message:\n");
    print_message_aardvark(new_message,*new_message_size);

    int res = aa_i2c_write(aardvark_handle, I2C_WR_SLAVE_ADDR, AA_I2C_NO_FLAGS, (uint16_t)(*new_message_size), (uint8_t*)new_message);
    printf("Send res: %d\n",res);
    

    if (res < 0) {
        return LIBSPDM_STATUS_SEND_FAIL;
    }
    return LIBSPDM_STATUS_SUCCESS;
} 

//Function to receive SPDM message
libspdm_return_t libspdm_device_receive_message_aardvark(void *spdm_context, size_t *message_size, void **message, uint64_t timeout){
    static uint8_t buffer[1024];
    //int res = aa_i2c_read(aardvark_handle, I2C_RD_SLAVE_ADDR, AA_I2C_NO_FLAGS, (uint16_t)*message_size, buffer);
    int res = aa_i2c_slave_read (aardvark_handle, (uint8_t*)&I2C_RD_SLAVE_ADDR, (uint16_t)*message_size, buffer);
    printf("%d\n",res);
    if (res < 0) {
        return LIBSPDM_STATUS_RECEIVE_FAIL;
    }
    
    *message = buffer;  // Point to the received buffer
    *message_size = res;  // Set the actual size of the received data

    return LIBSPDM_STATUS_SUCCESS;
}    


libspdm_return_t spdm_device_acquire_sender_buffer (
    void *context, void **msg_buf_ptr)
{
    LIBSPDM_ASSERT (!m_sender_buffer_acquired && !m_receiver_buffer_acquired);
    if (m_error_acquire_sender_buffer) {
        return LIBSPDM_STATUS_ACQUIRE_FAIL;
    } else {
        *msg_buf_ptr = m_send_receive_buffer;
        libspdm_zero_mem (m_send_receive_buffer, sizeof(m_send_receive_buffer));
        m_sender_buffer_acquired = true;

        return LIBSPDM_STATUS_SUCCESS;
    }
}

void spdm_device_release_sender_buffer (void *context, const void *msg_buf_ptr)
{
    LIBSPDM_ASSERT (m_sender_buffer_acquired && !m_receiver_buffer_acquired);
    LIBSPDM_ASSERT (msg_buf_ptr == m_send_receive_buffer);

    m_sender_buffer_acquired = false;
}

libspdm_return_t spdm_device_acquire_receiver_buffer (
    void *context, void **msg_buf_ptr)
{
    LIBSPDM_ASSERT (!m_sender_buffer_acquired && !m_receiver_buffer_acquired);

    if (m_error_acquire_receiver_buffer) {
        return LIBSPDM_STATUS_ACQUIRE_FAIL;
    } else {
        *msg_buf_ptr = m_send_receive_buffer;
        libspdm_zero_mem (m_send_receive_buffer, sizeof(m_send_receive_buffer));
        m_receiver_buffer_acquired = true;

        return LIBSPDM_STATUS_SUCCESS;
    }
}

void spdm_device_release_receiver_buffer (void *context, const void *msg_buf_ptr)
{
    LIBSPDM_ASSERT (!m_sender_buffer_acquired && m_receiver_buffer_acquired);
    LIBSPDM_ASSERT (msg_buf_ptr == m_send_receive_buffer);

    m_receiver_buffer_acquired = false;
}

libspdm_test_context_t *libspdm_get_test_context(void)
{
    return m_spdm_test_context;
}

void libspdm_setup_test_context(libspdm_test_context_t *spdm_test_context)
{
    m_spdm_test_context = spdm_test_context;
}

int libspdm_unit_test_group_setup(void **state)
{
    initialize_aardvark();
    libspdm_test_context_t *spdm_test_context;
    void *spdm_context;

    spdm_test_context = m_spdm_test_context;
    spdm_test_context->spdm_context = (void *)malloc(libspdm_get_context_size());
    if (spdm_test_context->spdm_context == NULL) {
        return -1;
    }
    spdm_context = spdm_test_context->spdm_context;
    spdm_test_context->case_id = 0xFFFFFFFF;

    libspdm_init_context(spdm_context);

    libspdm_register_device_io_func(spdm_context,
                                    spdm_test_context->send_message,
                                    spdm_test_context->receive_message);
    libspdm_register_transport_layer_func(spdm_context,
                                          LIBSPDM_MAX_SPDM_MSG_SIZE,
                                          LIBSPDM_TEST_TRANSPORT_HEADER_SIZE,
                                          LIBSPDM_TEST_TRANSPORT_TAIL_SIZE,
                                          libspdm_transport_test_encode_message,
                                          libspdm_transport_test_decode_message);
    libspdm_register_device_buffer_func(spdm_context,
                                        LIBSPDM_MAX_SENDER_RECEIVER_BUFFER_SIZE,
                                        LIBSPDM_MAX_SENDER_RECEIVER_BUFFER_SIZE,
                                        spdm_device_acquire_sender_buffer,
                                        spdm_device_release_sender_buffer,
                                        spdm_device_acquire_receiver_buffer,
                                        spdm_device_release_receiver_buffer);

    spdm_test_context->scratch_buffer_size =
        libspdm_get_sizeof_required_scratch_buffer(spdm_context);
    spdm_test_context->scratch_buffer = (void *)malloc(spdm_test_context->scratch_buffer_size);
    libspdm_set_scratch_buffer (spdm_context,
                                spdm_test_context->scratch_buffer,
                                spdm_test_context->scratch_buffer_size);

    m_error_acquire_sender_buffer = false;
    m_error_acquire_receiver_buffer = false;

    #if (LIBSPDM_ENABLE_CAPABILITY_MUT_AUTH_CAP) && (LIBSPDM_ENABLE_CAPABILITY_ENCAP_CAP) && \
    (LIBSPDM_SEND_GET_CERTIFICATE_SUPPORT)
    libspdm_register_cert_chain_buffer(
        spdm_context, m_cert_chain_buffer, sizeof(m_cert_chain_buffer));
    #endif /* (LIBSPDM_ENABLE_CAPABILITY_MUT_AUTH_CAP) && (...) */

    *state = spdm_test_context;

    return 0;
}

int libspdm_unit_test_group_teardown(void **state)
{
    aa_close(aardvark_handle);
    libspdm_test_context_t *spdm_test_context;

    LIBSPDM_ASSERT (!m_sender_buffer_acquired && !m_receiver_buffer_acquired);

    spdm_test_context = *state;
    free(spdm_test_context->spdm_context);
    free(spdm_test_context->scratch_buffer);
    spdm_test_context->spdm_context = NULL;
    spdm_test_context->case_id = 0xFFFFFFFF;

    return 0;
}

void libspdm_force_error (libspdm_error_target_t target)
{
    switch (target) {
    case LIBSPDM_ERR_ACQUIRE_SENDER_BUFFER:
        m_error_acquire_sender_buffer = true;
        break;
    case LIBSPDM_ERR_ACQUIRE_RECEIVER_BUFFER:
        m_error_acquire_receiver_buffer = true;
        break;
    }
}

void libspdm_release_error (libspdm_error_target_t target)
{
    switch (target) {
    case LIBSPDM_ERR_ACQUIRE_SENDER_BUFFER:
        m_error_acquire_sender_buffer = false;
        break;
    case LIBSPDM_ERR_ACQUIRE_RECEIVER_BUFFER:
        m_error_acquire_receiver_buffer = false;
        break;
    }
}

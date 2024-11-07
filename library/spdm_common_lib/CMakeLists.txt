cmake_minimum_required(VERSION 3.5)

add_library(spdm_common_lib STATIC "")

target_include_directories(spdm_common_lib
    PRIVATE
        ${LIBSPDM_DIR}/include
)

target_sources(spdm_common_lib
    PRIVATE
        libspdm_com_context_data.c
        libspdm_com_context_data_session.c
        libspdm_com_crypto_service.c
        libspdm_com_crypto_service_session.c
        libspdm_com_opaque_data.c
        libspdm_com_support.c
        libspdm_com_msg_log.c
)
include_directories("/home/labpc/sachins/libspdm_aardvark/aardvark-api-linux-x86_64-v6.00/c")
link_directories("/home/labpc/sachins/libspdm_aardvark/aardvark-api-linux-x86_64-v6.00/c/")
target_link_libraries(spdm_common_lib /home/labpc/sachins/libspdm_aardvark/aardvark-api-linux-x86_64-v6.00/c/_output/aardvark.o)

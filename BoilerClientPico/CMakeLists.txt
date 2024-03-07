add_executable(boiler_client_background
        boiler_client.cpp
        )
target_compile_definitions(boiler_client_background PRIVATE
        WIFI_SSID=\"${WIFI_SSID}\"
        WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
        TLS_CLIENT_SERVER=\"${BOILER_MANAGER_SERVER}\"
        )
target_include_directories(boiler_client_background PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts
        )
#dependencies
target_link_libraries(boiler_client_background
        pico_cyw43_arch_lwip_threadsafe_background
        pico_lwip_mbedtls
        pico_mbedtls
        pico_stdlib
        pico_one_wire
        )
# enable usb output, disable uart output
pico_enable_stdio_usb(boiler_client_background 1)
pico_enable_stdio_uart(boiler_client_background 0)
# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(boiler_client_background)

# Ignore warnings from lwip code
set_source_files_properties(
        ${PICO_LWIP_PATH}/src/apps/altcp_tls/altcp_tls_mbedtls.c
        PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-result"
        )
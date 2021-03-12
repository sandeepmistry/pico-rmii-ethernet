cmake_minimum_required(VERSION 3.12)

# initialize pico_sdk from GIT
# (note this can come from environment, CMake cache etc)
# set(PICO_SDK_FETCH_FROM_GIT on)

# pico_sdk_import.cmake is a single file copied from this SDK
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(pico_rmii_ethernet)

# initialize the Pico SDK
pico_sdk_init()

set(LWIP_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/lwip)

add_library(pico_lwip INTERFACE)

target_sources(pico_lwip INTERFACE
    ${LWIP_PATH}/src/core/altcp.c
    ${LWIP_PATH}/src/core/altcp_alloc.c
    ${LWIP_PATH}/src/core/altcp_tcp.c
    ${LWIP_PATH}/src/core/def.c
    ${LWIP_PATH}/src/core/dns.c
    ${LWIP_PATH}/src/core/inet_chksum.c
    ${LWIP_PATH}/src/core/init.c
    ${LWIP_PATH}/src/core/ip.c
    ${LWIP_PATH}/src/core/mem.c
    ${LWIP_PATH}/src/core/memp.c
    ${LWIP_PATH}/src/core/netif.c
    ${LWIP_PATH}/src/core/pbuf.c
    ${LWIP_PATH}/src/core/raw.c
    ${LWIP_PATH}/src/core/stats.c
    ${LWIP_PATH}/src/core/sys.c
    ${LWIP_PATH}/src/core/tcp.c
    ${LWIP_PATH}/src/core/tcp_in.c
    ${LWIP_PATH}/src/core/tcp_out.c
    ${LWIP_PATH}/src/core/timeouts.c
    ${LWIP_PATH}/src/core/udp.c
    ${LWIP_PATH}/src/core/ipv4/autoip.c
    ${LWIP_PATH}/src/core/ipv4/dhcp.c
    ${LWIP_PATH}/src/core/ipv4/etharp.c
    ${LWIP_PATH}/src/core/ipv4/icmp.c
    ${LWIP_PATH}/src/core/ipv4/igmp.c
    ${LWIP_PATH}/src/core/ipv4/ip4.c
    ${LWIP_PATH}/src/core/ipv4/ip4_addr.c
    ${LWIP_PATH}/src/core/ipv4/ip4_frag.c
    ${LWIP_PATH}/src/netif/ethernet.c

    ${LWIP_PATH}/src/apps/http/httpd.c
    ${LWIP_PATH}/src/apps/http/fs.c

    ${CMAKE_CURRENT_LIST_DIR}/src/lwip/sys_arch.c
)

target_include_directories(pico_lwip INTERFACE
	${LWIP_PATH}/src/include
	${CMAKE_CURRENT_LIST_DIR}/src/lwip
)

add_library(pico_rmii_ethernet INTERFACE)

target_sources(pico_rmii_ethernet INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/rmii_ethernet.c
)

target_include_directories(pico_rmii_ethernet INTERFACE
	${CMAKE_CURRENT_LIST_DIR}/src/include
)

pico_generate_pio_header(pico_rmii_ethernet ${CMAKE_CURRENT_LIST_DIR}/src/rmii_ethernet_phy_rx.pio)
pico_generate_pio_header(pico_rmii_ethernet ${CMAKE_CURRENT_LIST_DIR}/src/rmii_ethernet_phy_tx.pio)

target_link_libraries(pico_rmii_ethernet INTERFACE hardware_pio hardware_dma pico_stdlib pico_unique_id pico_lwip)

add_subdirectory("examples/httpd")

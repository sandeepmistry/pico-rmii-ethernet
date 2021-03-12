/*
 * Copyright (c) 2021 Sandeep Mistry
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _PICO_RMII_ETHERNET_NETIF_H_
#define _PICO_RMII_ETHERNET_NETIF_H_

#include "hardware/pio.h"

#include "lwip/netif.h"

struct netif_rmii_ethernet_config {
    PIO pio;
    uint pio_sm_start; // uses 2 PIO sm's
    uint rx_pin_start; // RX0, RX1, CRS
    uint tx_pin_start; // TX0, TX1, TX-EN
    uint mdio_pin_start; // MDIO, MDC
    uint8_t *mac_addr; // 6 bytes
};

#define NETIF_RMII_ETHERNET_DEFAULT_CONFIG() { \
    .pio = pio0, \
    .pio_sm_start = 0, \
    .rx_pin_start = 6, \
    .tx_pin_start = 10, \
    .mdio_pin_start = 14, \
    .mac_addr = NULL \
}

err_t netif_rmii_ethernet_init(struct netif *netif, struct netif_rmii_ethernet_config *config);

void netif_rmii_ethernet_poll();

void netif_rmii_ethernet_loop();

#endif

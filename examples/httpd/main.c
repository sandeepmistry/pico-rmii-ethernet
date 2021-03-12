/*
 * Copyright (c) 2021 Sandeep Mistry
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hardware/clocks.h"

#include "lwip/dhcp.h"
#include "lwip/init.h"

#include "lwip/apps/httpd.h"

#include "rmii_ethernet/netif.h"

void netif_link_callback(struct netif *netif)
{
    printf("netif link status changed %s\n", netif_is_link_up(netif) ? "up" : "down");
}

void netif_status_callback(struct netif *netif)
{
    printf("netif status changed %s\n", ip4addr_ntoa(netif_ip4_addr(netif)));
}

int main() {
    struct netif netif;

    // change the system clock to use the RMII reference clock from pin 20
    clock_configure_gpin(clk_sys, 20, 50 * MHZ, 50 * MHZ);
    sleep_ms(100);

    stdio_init_all();

    sleep_ms(5000);
    
    printf("pico rmii ethernet - httpd\n");

    lwip_init();

    netif_rmii_ethernet_init(&netif);
    
    netif_set_link_callback(&netif, netif_link_callback);
    netif_set_status_callback(&netif, netif_status_callback);
    netif_set_default(&netif);
    netif_set_up(&netif);

    // Start DHCP
    dhcp_start(&netif);
    httpd_init();

    multicore_launch_core1(netif_rmii_ethernet_loop);

    while (1) {
        tight_loop_contents();
    }

    return 0;
}

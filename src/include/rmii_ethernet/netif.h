#ifndef _PICO_RMII_ETHERNET_NETIF_H_
#define _PICO_RMII_ETHERNET_NETIF_H_

#include "lwip/netif.h"

err_t netif_rmii_ethernet_init(struct netif *netif);

void netif_rmii_ethernet_poll();

void netif_rmii_ethernet_loop();

#endif

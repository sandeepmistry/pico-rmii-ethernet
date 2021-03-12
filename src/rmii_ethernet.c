/*
 * Copyright (c) 2021 Sandeep Mistry
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>

#include "hardware/dma.h"

#include "pico/stdlib.h"
#include "pico/unique_id.h"

#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"

#include "rmii_ethernet_phy_rx.pio.h"
#include "rmii_ethernet_phy_tx.pio.h"

#include "rmii_ethernet/netif.h"

#define PICO_RMII_ETHERNET_PIO      (rmii_eth_netif_config.pio)
#define PICO_RMII_ETHERNET_SM_RX    (rmii_eth_netif_config.pio_sm_start)
#define PICO_RMII_ETHERNET_SM_TX    (rmii_eth_netif_config.pio_sm_start + 1)
#define PICO_RMII_ETHERNET_RX_PIN   (rmii_eth_netif_config.rx_pin_start)
#define PICO_RMII_ETHERNET_TX_PIN   (rmii_eth_netif_config.tx_pin_start)
#define PICO_RMII_ETHERNET_MDIO_PIN (rmii_eth_netif_config.mdio_pin_start)
#define PICO_RMII_ETHERNET_MDC_PIN  (rmii_eth_netif_config.mdio_pin_start + 1)
#define PICO_RMII_ETHERNET_MAC_ADDR (rmii_eth_netif_config.mac_addr)

static struct netif *rmii_eth_netif;
static struct netif_rmii_ethernet_config rmii_eth_netif_config = NETIF_RMII_ETHERNET_DEFAULT_CONFIG();

static uint rx_sm_offset;
static uint tx_sm_offset;

static int rx_dma_chan;
static int tx_dma_chan;

static dma_channel_config rx_dma_channel_config;
static dma_channel_config tx_dma_channel_config;

static int phy_address = 0;

static uint8_t rx_frame[1518];
static uint8_t tx_frame[1518];
static uint8_t tx_frame_bits[1538 * 4];

static const uint32_t ethernet_polynomial_le = 0xedb88320U;

static uint ethernet_frame_crc(const uint8_t *data, int length)
{
    uint crc = 0xffffffff;  /* Initial value. */

    while(--length >= 0) 
    {
        uint8_t current_octet = *data++;

        for (int bit = 8; --bit >= 0; current_octet >>= 1) {
          if ((crc ^ current_octet) & 1) {
            crc >>= 1;
            crc ^= ethernet_polynomial_le;
          } else
            crc >>= 1;
        }
    }

    return ~crc;
}

static uint ethernet_frame_length(const uint8_t *data, int length) {
    uint crc = 0xffffffff;  /* Initial value. */
    uint index = 0;

    while(--length >= 0) 
    {
        uint8_t current_octet = *data++;

        for (int bit = 8; --bit >= 0; current_octet >>= 1) {
          if ((crc ^ current_octet) & 1) {
            crc >>= 1;
            crc ^= ethernet_polynomial_le;
          } else
            crc >>= 1;
        }

        index++;

        uint inverted_crc = ~crc;

        if (memcmp(data, &inverted_crc, sizeof(inverted_crc)) == 0) {
            return index;
        }
    }

    return 0;
}

static void netif_rmii_ethernet_mdio_clock_out(int bit)
{
    gpio_put(PICO_RMII_ETHERNET_MDC_PIN, 0);
    sleep_us(1);
    gpio_put(PICO_RMII_ETHERNET_MDIO_PIN, bit);
    gpio_put(PICO_RMII_ETHERNET_MDC_PIN, 1);
    sleep_us(1);
}

static uint netif_rmii_ethernet_mdio_clock_in()
{
    gpio_put(PICO_RMII_ETHERNET_MDC_PIN, 0);
    sleep_us(1);
    gpio_put(PICO_RMII_ETHERNET_MDC_PIN, 1);
    
    int bit = gpio_get(PICO_RMII_ETHERNET_MDIO_PIN);
    sleep_us(1);

    return bit;
}

static uint16_t netif_rmii_ethernet_mdio_read(uint addr, uint reg)
{
    gpio_init(PICO_RMII_ETHERNET_MDIO_PIN);
    gpio_init(PICO_RMII_ETHERNET_MDC_PIN);

    gpio_set_dir(PICO_RMII_ETHERNET_MDIO_PIN, GPIO_OUT);
    gpio_set_dir(PICO_RMII_ETHERNET_MDC_PIN, GPIO_OUT);

    // PRE_32
    for (int i = 0; i < 32; i++) {
        netif_rmii_ethernet_mdio_clock_out(1);
    }

    // ST
    netif_rmii_ethernet_mdio_clock_out(0);
    netif_rmii_ethernet_mdio_clock_out(1);

    // OP
    netif_rmii_ethernet_mdio_clock_out(1);
    netif_rmii_ethernet_mdio_clock_out(0);

    // PA5
    for (int i = 0; i < 5; i++) {
        uint bit = (addr >> (4 - i)) & 0x01;

        netif_rmii_ethernet_mdio_clock_out(bit);
    }

    // RA5
    for (int i = 0; i < 5; i++) {
        uint bit = (reg >> (4 - i)) & 0x01;

        netif_rmii_ethernet_mdio_clock_out(bit);
    }

    // TA
    gpio_set_dir(PICO_RMII_ETHERNET_MDIO_PIN, GPIO_IN);
    netif_rmii_ethernet_mdio_clock_out(0);
    netif_rmii_ethernet_mdio_clock_out(0);

    uint16_t data = 0;

    for (int i = 0; i < 16; i++) {
        data <<= 1;

        data |= netif_rmii_ethernet_mdio_clock_in();
    }

    return data;
}

void netif_rmii_ethernet_mdio_write(int addr, int reg, int val)
{
    gpio_init(PICO_RMII_ETHERNET_MDIO_PIN);
    gpio_init(PICO_RMII_ETHERNET_MDC_PIN);

    gpio_set_dir(PICO_RMII_ETHERNET_MDIO_PIN, GPIO_OUT);
    gpio_set_dir(PICO_RMII_ETHERNET_MDC_PIN, GPIO_OUT);

    // PRE_32
    for (int i = 0; i < 32; i++) {
        netif_rmii_ethernet_mdio_clock_out(1);
    }

    // ST
    netif_rmii_ethernet_mdio_clock_out(0);
    netif_rmii_ethernet_mdio_clock_out(1);

    // OP
    netif_rmii_ethernet_mdio_clock_out(0);
    netif_rmii_ethernet_mdio_clock_out(1);

    // PA5
    for (int i = 0; i < 5; i++) {
        uint bit = (addr >> (4 - i)) & 0x01;

        netif_rmii_ethernet_mdio_clock_out(bit);
    }

    // RA5
    for (int i = 0; i < 5; i++) {
        uint bit = (reg >> (4 - i)) & 0x01;

        netif_rmii_ethernet_mdio_clock_out(bit);
    }

    // TA
    netif_rmii_ethernet_mdio_clock_out(1);
    netif_rmii_ethernet_mdio_clock_out(0);

    for (int i = 0; i < 16; i++) {
        uint bit = (val >> (15 - i)) & 0x01;

        netif_rmii_ethernet_mdio_clock_out(bit);
    }

    gpio_set_dir(PICO_RMII_ETHERNET_MDIO_PIN, GPIO_IN);
}

static err_t netif_rmii_ethernet_output(struct netif *netif, struct pbuf *p)
{
    uint tot_len = 0;

    memset(tx_frame, 0x00, sizeof(tx_frame));

    for (struct pbuf *q = p; q != NULL; q = q->next) {
        memcpy(tx_frame + tot_len, q->payload, q->len);

        tot_len += q->len;

        if (q->len == q->tot_len) {
            break;
        }
    }

    if (tot_len < 60) {
        // pad
        tot_len = 60;
    }

    uint crc = ethernet_frame_crc(tx_frame, tot_len);

    // printf("TX: ");
    // for (int i = 0; i < tot_len; i++) {
    //     printf("%02X", tx_frame[i]);
    // }
    // printf("\n");

    dma_channel_wait_for_finish_blocking(tx_dma_chan);

    int index = 0;

    for (int i = 0; i < 31; i++) {
        tx_frame_bits[index++] = 0x05;
    }

    for (int i = 0; i < 1; i++) {
        tx_frame_bits[index++] = 0x07;
    }

    for (int i = 0; i < tot_len; i++) {
        uint8_t b = tx_frame[i];

        tx_frame_bits[index++] = 0x04 | ((b >> 0) & 0x03);
        tx_frame_bits[index++] = 0x04 | ((b >> 2) & 0x03);
        tx_frame_bits[index++] = 0x04 | ((b >> 4) & 0x03);
        tx_frame_bits[index++] = 0x04 | ((b >> 6) & 0x03);
    }

    for (int i = 0; i < 4; i++) {
        uint8_t b = ((uint8_t*)&crc)[i];

        tx_frame_bits[index++] = 0x04 | ((b >> 0) & 0x03);
        tx_frame_bits[index++] = 0x04 | ((b >> 2) & 0x03);
        tx_frame_bits[index++] = 0x04 | ((b >> 4) & 0x03);
        tx_frame_bits[index++] = 0x04 | ((b >> 6) & 0x03);
    }
    
    for (int i = 0; i < (12 * 4); i++) {
        tx_frame_bits[index++] = 0x00;
    }

    dma_channel_configure(
        tx_dma_chan, &tx_dma_channel_config,
        ((uint8_t*)&PICO_RMII_ETHERNET_PIO->txf[PICO_RMII_ETHERNET_SM_TX]) + 3,
        tx_frame_bits,
        index,
        false
    );

    dma_channel_start(tx_dma_chan);

    return ERR_OK;
}

static void netif_rmii_ethernet_rx_dv_falling_callback(uint gpio, uint32_t events) {
    if ((PICO_RMII_ETHERNET_RX_PIN + 2) == gpio) {
        pio_sm_set_enabled(PICO_RMII_ETHERNET_PIO, PICO_RMII_ETHERNET_SM_RX, false);
        dma_channel_abort(rx_dma_chan); //dma_hw->abort = 1u << rx_dma_chan;
        gpio_set_irq_enabled_with_callback(PICO_RMII_ETHERNET_RX_PIN + 2, GPIO_IRQ_EDGE_FALL, false, netif_rmii_ethernet_rx_dv_falling_callback);
    }
}

static err_t netif_rmii_ethernet_low_init(struct netif *netif) {
    rmii_eth_netif = netif;

    netif->linkoutput = netif_rmii_ethernet_output;
    netif->output     = etharp_output;
    netif->mtu        = 1500; 
    netif->flags      = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP | NETIF_FLAG_MLD6;

    if (PICO_RMII_ETHERNET_MAC_ADDR != NULL) {
        memcpy(netif->hwaddr, PICO_RMII_ETHERNET_MAC_ADDR, 6);
    } else {
        // generate one for unique board id
        pico_unique_board_id_t board_id;
    
        pico_get_unique_board_id(&board_id);

        netif->hwaddr[0] = 0xb8;
        netif->hwaddr[1] = 0x27;
        netif->hwaddr[2] = 0xeb;
        memcpy(&netif->hwaddr[3], &board_id.id[5], 3);
    }
    
    
    netif->hwaddr_len = ETH_HWADDR_LEN;
    
    rx_sm_offset = pio_add_program(PICO_RMII_ETHERNET_PIO, &rmii_ethernet_phy_rx_data_program);
    tx_sm_offset = pio_add_program(PICO_RMII_ETHERNET_PIO, &rmii_ethernet_phy_tx_data_program);

    rx_dma_chan = dma_claim_unused_channel(true);
    tx_dma_chan = dma_claim_unused_channel(true);

    rx_dma_channel_config = dma_channel_get_default_config(rx_dma_chan);
        
    channel_config_set_read_increment(&rx_dma_channel_config, false);
    channel_config_set_write_increment(&rx_dma_channel_config, true);
    channel_config_set_dreq(&rx_dma_channel_config, pio_get_dreq(PICO_RMII_ETHERNET_PIO,PICO_RMII_ETHERNET_SM_RX, false));
    channel_config_set_transfer_data_size(&rx_dma_channel_config, DMA_SIZE_8);

    tx_dma_channel_config = dma_channel_get_default_config(tx_dma_chan);

    channel_config_set_read_increment(&tx_dma_channel_config, true);
    channel_config_set_write_increment(&tx_dma_channel_config, false);
    channel_config_set_dreq(&tx_dma_channel_config, pio_get_dreq(PICO_RMII_ETHERNET_PIO, PICO_RMII_ETHERNET_SM_TX, true));
    channel_config_set_transfer_data_size(&tx_dma_channel_config, DMA_SIZE_8);

    rmii_ethernet_phy_tx_init(PICO_RMII_ETHERNET_PIO, PICO_RMII_ETHERNET_SM_TX, tx_sm_offset, PICO_RMII_ETHERNET_TX_PIN);

    for (int i = 0; i < 32; i++) {
        if (netif_rmii_ethernet_mdio_read(i, 0) != 0xffff) {
            phy_address = i;

            break;
        }
    }

    // netif_rmii_ethernet_mdio_write(phy_address, 0, 0x2000); // 10 Mbps, auto negeotiate disabled

    netif_rmii_ethernet_mdio_write(phy_address, 4, 0x61);
    netif_rmii_ethernet_mdio_write(phy_address, 0, 0x1000);

    return ERR_OK;
}

err_t netif_rmii_ethernet_init(struct netif *netif, struct netif_rmii_ethernet_config *config) {
    if (config != NULL) {
        memcpy(&rmii_eth_netif_config, config, sizeof(rmii_eth_netif_config));
    }

    netif_add(netif, IP4_ADDR_ANY, IP4_ADDR_ANY, IP4_ADDR_ANY, NULL, netif_rmii_ethernet_low_init, netif_input);

    netif->name[0] = 'e';
    netif->name[1] = '0';
}

void netif_rmii_ethernet_poll() {
    uint16_t link_status = (netif_rmii_ethernet_mdio_read(phy_address, 1) & 0x04) >> 2;

    if (netif_is_link_up(rmii_eth_netif) ^ link_status) {
        if (link_status) {
            // printf("netif_set_link_up\n");
            netif_set_link_up(rmii_eth_netif);
        } else {
            // printf("netif_set_link_down\n");
            netif_set_link_down(rmii_eth_netif);
        }
    }

    if (dma_channel_is_busy(rx_dma_chan)) {

    } else {
        uint rx_frame_length = ethernet_frame_length(rx_frame, sizeof(rx_frame));

        if (rx_frame_length) {
            // printf("RX: ");
            // for (int i = 0; i < rx_frame_length + 4; i++) {
            //     printf("%02X", rx_frame[i]);
            // }
            // printf("\n");

            struct pbuf* p = pbuf_alloc(PBUF_RAW, rx_frame_length, PBUF_POOL);

            pbuf_take(p, rx_frame, rx_frame_length);

            if (rmii_eth_netif->input(p, rmii_eth_netif) != ERR_OK) {
                pbuf_free(p);
            }
        }

        memset(rx_frame, 0x00, sizeof(rx_frame));

        dma_channel_configure(
            rx_dma_chan, &rx_dma_channel_config,
            rx_frame,
            ((uint8_t*)&PICO_RMII_ETHERNET_PIO->rxf[PICO_RMII_ETHERNET_SM_RX]) + 3,
            1500,
            false
        );

        dma_channel_start(rx_dma_chan);

        rmii_ethernet_phy_rx_init(PICO_RMII_ETHERNET_PIO, PICO_RMII_ETHERNET_SM_RX, rx_sm_offset, PICO_RMII_ETHERNET_RX_PIN);

        gpio_set_irq_enabled_with_callback(PICO_RMII_ETHERNET_RX_PIN + 2, GPIO_IRQ_EDGE_FALL, true, &netif_rmii_ethernet_rx_dv_falling_callback);
    }

    sys_check_timeouts();
}

void netif_rmii_ethernet_loop() {
    while (1) {
        netif_rmii_ethernet_poll();
    }
}

# pico-rmii-ethernet

Enable Ethernet connectivity on your [Raspberry Pi Pico](https://www.raspberrypi.org/products/raspberry-pi-pico/) with an RMII based Ethernet PHY module.

Leverages the Raspberry Pi RP2040 MCU's PIO, DMA, and dual core capabilities to create a Ethernet MAC stack in software!

## Hardware

* [Raspberry Pico](https://www.raspberrypi.org/products/raspberry-pi-pico/)
* Any RMII based Ethernet PHY module, such as the [Waveshare LAN8720 ETH Board](https://www.waveshare.com/lan8720-eth-board.htm)

### Wiring

| RMII Module | Raspberry Pi Pico | Library Default |
| ----------- | ----------------- | --------------- |
| TX1 | TX0 + 1 | 11 |
| TX-EN | TX0 + 2 | 12 |
| TX0 | any GPIO | 10 |
| RX0 | any GPIO | 6 |
| RX1 | RX0 + 1 | 7 |
| nINT / RETCLK | 20 or 22 | 20 |
| CRS | RX0 + 2 | 8 |
| MDIO | any GPIO | 14 |
| MDC | MDIO + 1 | 15 |
| VCC | 3V3 | |
| GND | GND | |

## Examples

See [examples](examples/) folder. [LWIP](https://www.nongnu.org/lwip/) is included as the TCP/IP stack.

# Current Limitations

* RP2040 is underclocked to 50 MHz using the RMII modules reference clock
* Link speed is set to 10 Mbps (there is a issue with TX at 100 Mbps)
* Built-in LWIP stack is compiled with `NO_SYS` so LWIP Netcon and Socket API's are not enabled

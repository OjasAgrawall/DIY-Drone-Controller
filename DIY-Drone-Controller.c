#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// SPI Defines
#define SPI_PORT spi0
#define PIN_CE   15
#define PIN_MISO 16
#define PIN_CSn  17
#define PIN_SCK  18
#define PIN_MOSI 19

// Core nRF24L01 Commands
#define R_REGISTER    0x00 // useless but makes it more readable
#define W_REGISTER    0x20
#define W_TX_PAYLOAD  0xA0
#define FLUSH_TX      0xE1

// Core nRF24L01 Registers
#define CONFIG        0x00
#define RF_CH         0x05 // Radio Channel Register
#define RF_SETUP      0x06 // Data Rate and Power Register
#define EN_AA         0x01 // auto acknowledgement 0x3F - on for all 6 channels
#define RX_PW_P0      0x11 // receive packet size on pipe 0
#define NRF_STATUS    0x07 // b6 data rx received, b5 data tx sent, b4 max retries | write 0x70 to reset (write 1 to clear)

#define CHANNEL 76
#define DATA_RATE 0x0E //0000 1110 -> 2Mbps 0dBm
#define DATA_SIZE 0x03
#define CONFIG_TX 0x02
#define CONFIG_RX 0x03

void nrf_write_reg(uint8_t reg, uint8_t value){
    uint8_t buffer[2] = { (W_REGISTER | reg), value};

    gpio_put(PIN_CSn, 0);
    spi_write_blocking(SPI_PORT, buffer, 2);
    gpio_put(PIN_CSn, 1);

}

uint8_t nrf_read_reg(uint8_t reg) {
    //send the register and a blank byte
    uint8_t buffer[2] = { (R_REGISTER | reg), 0x00};
    uint8_t data[2] = {0};

    // This transmits buffer and fills rx_buf at the exact same time.
    gpio_put(PIN_CSn, 0);
    spi_write_read_blocking(SPI_PORT, buffer, data, 2);
    gpio_put(PIN_CSn, 1);

    // rx_buf[0] contains the nRF Status byte
    // rx_buf[1] contains the actual register value we asked for
    return data[1];
}

void nrf_send_cmd(uint8_t cmd){
    gpio_put(PIN_CSn, 0);
    spi_write_blocking(SPI_PORT, &cmd, 1);
    gpio_put(PIN_CSn, 1);

}

void nrf_send_data(uint8_t data[DATA_SIZE]){
    //construct the bullet
    uint8_t tx_buffer[DATA_SIZE + 1];
    tx_buffer[0] = W_TX_PAYLOAD;
    
    //add the message
    for (int i = 0; i < DATA_SIZE; i++) {
        tx_buffer[i + 1] = data[i];
    }

    //load the gun
    gpio_put(PIN_CSn, 0);
    spi_write_blocking(SPI_PORT, tx_buffer, 4); 
    gpio_put(PIN_CSn, 1);

    //fire
    gpio_put(PIN_CE, 1);
    sleep_us(15);
    gpio_put(PIN_CE, 0);


    //read status flags rq
    uint8_t status = 0;
    int timeout = 1000; // Prevent infinite loop if hardware detaches
    while (timeout > 0) {
        status = nrf_read_reg(NRF_STATUS);
        if ((status & 0x30) != 0) { // Check if either MAX_RT (0x10) or TX_DS (0x20) is set
            break;
        }
        sleep_us(100);
        timeout--;
    }

    if ((status & 0x10) == 0x10){
        printf("message not sent :(");
    }
    if ((status & 0x20) == 0x20){
        printf("message send successfully :)");
    }

    // reset status flags and flush the transmission buffer
    nrf_write_reg(NRF_STATUS, 0x70); 
    nrf_send_cmd(FLUSH_TX);
}

uint8_t message[3] = {0x10, 0x20, 0x30};

int main()
{
    stdio_init_all();

    // SPI initialisation. This example will use SPI at 1MHz.
    spi_init(SPI_PORT, 1000*1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CSn,  GPIO_FUNC_SIO);
    gpio_set_function(PIN_CE,   GPIO_FUNC_SIO);

    // Configure SPI to 8-bit, Mode 3, MSB-first to match nRF24L01 requirements
    spi_set_format(SPI_PORT, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);

    //CE active high
    gpio_set_dir(PIN_CE, GPIO_OUT);
    gpio_put(PIN_CE, 0);

    //CSn active low when sending message
    gpio_set_dir(PIN_CSn, GPIO_OUT);
    gpio_put(PIN_CSn, 1);

    //init nrf
    nrf_write_reg(CONFIG, CONFIG_TX);
    nrf_write_reg(EN_AA, 0x3F);  // Enable Auto-ACK for all pipes
    nrf_write_reg(RX_PW_P0, DATA_SIZE);  // 3-bytes on Pipe 0 also doesnt require hex

    nrf_write_reg(RF_CH, CHANNEL); // confirm that this is the same as the other nrf
    nrf_write_reg(RF_SETUP, DATA_RATE); // same here as before

    while (true) {
        sleep_ms(1000);

        nrf_send_data(message);

    }
}

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/adc.h"

// SPI Defines
#define NRF_SPI_PORT spi0
#define PIN_CE   15
#define PIN_MISO 4
#define PIN_CSn  17
#define PIN_SCK  6
#define PIN_MOSI 19

#define R_REGISTER    0x00 // useless but makes it more readable
#define W_REGISTER    0x20
#define R_RX_PAYLOAD  0x61
#define FLUSH_RX      0xE2
#define W_TX_PAYLOAD  0xA0
#define FLUSH_TX      0xE1
#define R_RX_PL_WID   0x60

// Core nRF24L01 Registers
#define CONFIG        0x00 // xxxxxxAB | A -  1 = PWR_UP 0 = PWR_DOWN | B -  1 = PRX 0 = PTX
#define EN_AA         0x01 // xxABCDEF | A - F Enable AutoAck on Pipe 5 - 0 | ENAA_P# Required when using DYNPD
#define EN_RXADDR     0x02 // xxABCDEF | A - F Enable data pipe 5 - 0
#define SETUP_AW      0x03 // xxxxxxAA | A - Address width 01 = 3 bytes, 10 = 4 bytes, 11 = 5 bytes
#define SETUP_RETR    0x04 // AAAABBBB | Auto Retransmit Delay (A) - 0000 = 250uS delay, each increment increases by 250 uS delay | Auto Retransmit Count (B) - 0000 for 1, 1111 for 15 
#define RF_CH         0x05 // xAAAAAAA | A - Sets the frequency to operate at
#define RF_SETUP      0x06 // AxBCDEEx | A - Continous carrier transmit | B - RF data rate set to 250kbps | C - force PLL lock (testing) | D - data rate: 0 for 1mbps, 1 for 2 mbps, 0 but B=1 for 250kbps| E - RF output power
#define NRF_STATUS    0x07 // xABCDDDE | A - Asserts when data ready in RX FIFO (W1C) | B - Asserts when data sent on TX FIFO, If ACK is EN, only asserts when ACK received (W1C) | C - Asserts when MAX_RT is reached (W1C) | D - Data Pipe number for the payload at RX FIFO | E - TX FIFO Full
#define RX_ADDR_P0    0x0A // 39A | A - RX Address for Pipe 0 | Reset value = 0xE7E7E7E7E7
#define TX_ADDR       0x10 // 39A | A - TX Address for Auto Ack set RX_ADDR_P0 equal to this value | Reset value = 0xE7E7E7E7E7
#define RX_PW_P0      0x11 // xxAAAAAA | A - # of Bytes to expect in RX payload in data pipe 0 | Useless when using dynamic payload width
#define DYNPD         0x1C // xxABCDEF | A - F Enable dynamic payload width on Pipe 5 - 0 | REQUIRES EN_DPL and ENAA_P#
#define NRF_FEATURES  0x1D // xxxxxABC | A - Enables Dynamic Payload width (EN_DPL) | B - Enables Payload with ACK | C - Enables the W_TX_PAYLOAD_NOACK Command
#define FIFO_STATUS   0x17 // xABCxxDE | A - TX_REUSE | B - TX is Full | C - TX is empty | D - RX is full | E - RX is empty
//other values
#define CHANNEL 120
#define DATA_RATE 0x06
#define DATA_SIZE 0x03
#define CONFIG_TX 0x02
#define CONFIG_RX 0x03

//Joysticks and push buttons
#define CHANNEL_FB 0
#define CHANNEL_LR 1
#define CHANNEL_YAWLR 2
#define PIN_UP 13
#define PIN_DOWN 12

#define PIN_SS_INPUT 22

void nrf_send_cmd(uint8_t cmd){
    gpio_put(PIN_CSn, 0);
    sleep_us(1);
    spi_write_blocking(NRF_SPI_PORT, &cmd, 1);
    gpio_put(PIN_CSn, 1);
    sleep_us(1);
}

void nrf_write_reg(uint8_t reg, uint8_t value){
    uint8_t buffer[2] = { (W_REGISTER | reg), value};

    gpio_put(PIN_CSn, 0);
    sleep_us(1);
    spi_write_blocking(NRF_SPI_PORT, buffer, 2);
    gpio_put(PIN_CSn, 1);
    sleep_us(1);
}

void nrf_writeAlot_reg(uint8_t reg, uint8_t *buf, uint8_t len){
    
    uint8_t cmd = W_REGISTER | reg;
    
    gpio_put(PIN_CSn, 0);
    sleep_us(1);
    spi_write_blocking(NRF_SPI_PORT, &cmd, 1); 
    spi_write_blocking(NRF_SPI_PORT, buf, len);
    gpio_put(PIN_CSn, 1);
    sleep_us(1);
}

uint8_t nrf_read_reg(uint8_t reg) {
    //send the register and a blank byte
    uint8_t buffer[2] = { (R_REGISTER | reg), 0x00};
    uint8_t data[2] = {0};

    // This transmits buffer and fills rx_buf at the exact same time.
    gpio_put(PIN_CSn, 0);
    sleep_us(1);
    spi_write_read_blocking(NRF_SPI_PORT, buffer, data, 2);
    gpio_put(PIN_CSn, 1);
    sleep_us(1);

    // rx_buf[0] contains the nRF Status byte
    // rx_buf[1] contains the actual register value we asked for
    return data[1];
}

uint8_t nrf_get_status(){
    uint8_t NO_OP = 0xFF;
    uint8_t status;

    gpio_put(PIN_CSn, 0);
    sleep_us(1);
    spi_write_read_blocking(NRF_SPI_PORT, &NO_OP, &status, 1);
    gpio_put(PIN_CSn, 1);
    sleep_us(1);

    return status;
}


void nrf_send_data(uint8_t data_size, uint8_t data[]){
    uint8_t tx_buffer[data_size + 1];
    tx_buffer[0] = W_TX_PAYLOAD;
    uint8_t status = 0;

    for (int i = 0; i < data_size; i++) {
        tx_buffer[i + 1] = data[i];
    }

    gpio_put(PIN_CSn, 0);
    sleep_us(1);
    spi_write_blocking(NRF_SPI_PORT, tx_buffer, data_size + 1);
    gpio_put(PIN_CSn, 1);
    sleep_us(1);

    gpio_put(PIN_CE, 1);
    sleep_us(15);
    gpio_put(PIN_CE, 0);

    status = nrf_get_status();

    //read status flags rq
    int timeout = 100; // Prevent infinite loop if hardware detaches
    while (timeout > 0) {
        status = nrf_get_status();
        if ((status & 0x30) != 0) { // Check if either MAX_RT (0x10) or TX_DS (0x20) is set
            timeout = 0;
        }
        sleep_ms(2);
        timeout--;
    }
    status = nrf_get_status();

    if ((status & 0x10) == 0x10){
        printf("ACK not received :(\n");
    }
    if ((status & 0x20) == 0x20){
        printf("message send successfully :)\n");
    }

    // reset status flags and flush the transmission buffer
    nrf_write_reg(NRF_STATUS, 0x70); 
    nrf_send_cmd(FLUSH_TX);
}

uint8_t message[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

uint16_t raw_FB;
uint16_t raw_LR;
uint16_t raw_YAWLR;
bool running;
bool up_pressed;
bool down_pressed;


uint8_t joy_fb_msb;
uint8_t joy_fb_lsb;
uint8_t joy_lr_msb;
uint8_t joy_lr_lsb;
uint8_t joy_yawlr_msb;
uint8_t joy_yawlr_lsb;
uint8_t b_up;
uint8_t b_down;


void inputs_to_nrf_data(uint16_t joy_fb, uint16_t joy_lr, uint16_t joy_yawlr, bool b_up_pressed, bool b_down_pressed, uint8_t message[8]){
    joy_fb_msb = (joy_fb >> 8) & 0xFF;
    joy_fb_lsb = joy_fb & 0xFF;

    joy_lr_msb = (joy_lr >> 8) & 0xFF;
    joy_lr_lsb = joy_lr & 0xFF;

    joy_yawlr_msb = (joy_yawlr >> 8) & 0xFF;
    joy_yawlr_lsb = joy_yawlr & 0xFF;

    if (b_up_pressed) b_up = 0xFF; else b_up = 0x00;
    if (b_down_pressed) b_down = 0xFF; else b_down = 0x00;

    uint8_t temp[8] = {joy_fb_msb, joy_fb_lsb, joy_lr_msb, joy_lr_lsb, joy_yawlr_msb, joy_yawlr_lsb, b_up, b_down};

    for (int i = 0; i < 8; i++){
        message[i] = temp[i];
    }
}

int main(){
    stdio_init_all();

    // SPI 1MHz.
    spi_init(NRF_SPI_PORT, 1000*1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_CE);
    gpio_init(PIN_CSn);
    gpio_set_dir(PIN_CE, GPIO_OUT);
    gpio_set_dir(PIN_CSn, GPIO_OUT);

    //CE active high
    gpio_put(PIN_CE, 0);

    //CSn active low when sending message
    gpio_put(PIN_CSn, 1);
    sleep_us(1);

    //init nrf
    nrf_write_reg(CONFIG, CONFIG_TX);
    sleep_ms(2);

    nrf_write_reg(EN_AA, 0x01); 
    nrf_write_reg(RF_CH, 120);
    nrf_write_reg(RF_SETUP, 0x20);
    // nrf_write_reg(RX_PW_P0, DATA_SIZE);
    nrf_write_reg(EN_RXADDR, 0x01);
    nrf_write_reg(SETUP_AW, 0x03);
    nrf_write_reg(SETUP_RETR, 0x0F);

    uint8_t nrf_address_1[5] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};
    uint8_t nrf_address_2[5] = {0xC2, 0xC2, 0xC2, 0xC2, 0xC2};
    nrf_writeAlot_reg(RX_ADDR_P0, nrf_address_1, 5);
    nrf_writeAlot_reg(TX_ADDR, nrf_address_1, 5);

    nrf_write_reg(NRF_FEATURES, 0x04);
    nrf_write_reg(DYNPD, 0x01);

    sleep_ms(5000);


    printf("CONFIG: %02X\n", nrf_read_reg(CONFIG));
    printf("RF_CH: %02X\n", nrf_read_reg(RF_CH));
    printf("RF_SETUP: %02X\n", nrf_read_reg(RF_SETUP));
    printf("STATUS: %02X\n", nrf_get_status());

    //Built in LED
    gpio_init(25);
    gpio_set_dir(25, GPIO_OUT);
    gpio_put(25, 1);

    //Slide Switch
    gpio_init(PIN_SS_INPUT);
    gpio_set_dir(PIN_SS_INPUT, GPIO_IN);
    gpio_pull_up(PIN_SS_INPUT);

    adc_init(); 

    adc_gpio_init(26); 
    adc_gpio_init(27); 
    adc_gpio_init(28); 

    gpio_init(PIN_UP);
    gpio_set_dir(PIN_UP, GPIO_IN);
    gpio_pull_up(PIN_UP);

    gpio_init(PIN_DOWN);
    gpio_set_dir(PIN_DOWN, GPIO_IN);
    gpio_pull_up(PIN_DOWN);

    uint8_t loop_delay_data = 5;
    uint8_t loop_delay_sendData = 50;
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    uint32_t last_time_data = 0;
    uint32_t last_time_sendData = 0;

    while (true) {
        current_time = to_ms_since_boot(get_absolute_time());
        if (current_time - last_time_data > loop_delay_data){
            last_time_data = current_time;
            adc_select_input(CHANNEL_FB);
            raw_FB = adc_read();

            adc_select_input(CHANNEL_LR);
            raw_LR = adc_read();
            
            adc_select_input(CHANNEL_YAWLR);
            raw_YAWLR = adc_read();

            up_pressed = !gpio_get(PIN_UP);
            down_pressed = !gpio_get(PIN_DOWN);
            running = gpio_get(PIN_SS_INPUT);

            // printf("raw values:%d, %d, %d\n", raw_FB, raw_LR, raw_YAWLR);
            inputs_to_nrf_data(raw_FB, raw_LR, raw_YAWLR, up_pressed, down_pressed, message);

        }

        if (current_time - last_time_sendData > loop_delay_sendData){
            last_time_sendData = current_time;
            if (running){
                printf("running %d\n", sizeof(message));

                uint8_t status = nrf_get_status();
                printf("STATUS: %02X\n", status);

                nrf_send_data(sizeof(message), message);
            }
            else{
                printf("off!!!\n");

                uint8_t status = nrf_get_status();
                printf("STATUS: %02X\n", status);

                uint8_t death[8] = {0xF7, 0xF7, 0xF7, 0xF7, 0xF7, 0xF7, 0xF7, 0xF7};
                nrf_send_data(sizeof(death), death);
            }
        }

        
    }
}
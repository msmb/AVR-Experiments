#include <stdint.h>
#include <avr/io.h>
#include <stdio.h>
#include <stdlib.h>
#include <util/delay.h>
#include <avr/interrupt.h>

#include "spi.h"
#include "mcp2515.h"


#include "uart.h"
#include "main.h"

static volatile uint8_t interrupt_received = 0;
void (*int_handler)(void); 

static void extInterruptINIT(void (*handler)(void));
static void interrupt_handler(void);

int main(void) {
	uint8_t raddress[] = {0x55, 0xEB, 0xFA, 0xB1, 0x00};
	uint8_t rexdata[] = {0xC0, 0xD1, 0x00, 0xC0, 0xD1, 0x00, 0xBE, 0xEF};
	
	uint8_t address[] = {0x00, 0x00, 0x00, 0x00, 0x00};
	uint8_t exdata[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	uint8_t status, psize;

	// Initialize serial port for output
	uart_init();
	stdout = &uart_output;
	stdin  = &uart_input;

	extInterruptINIT(interrupt_handler);
	sei(); // Enable interrupts
	
	// Setup SPI
	setup_spi(SPI_MODE_0, SPI_MSB, SPI_NO_INTERRUPT, SPI_MSTR_CLK4);

	mcp2515_simpleStartup(mcp_can_speed_250, 1);
	

	while (1) {

		if(interrupt_received) {
			status = mcp2515_readRegister(MCP2515_REG_CANINTF);
			fprintf(stdout, "RECEIVED INTERRUPT! -> %.2X\n", status);

			if (status & 0x02) {
				psize = mcp2515_readMSG(mcp_rx_rxb1, address, exdata);
				fprintf(stdout, "\tRXB1 (%u) -> %.2X %.2X %.2X %.2X | %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X\n", psize, address[0], address[1], address[2], address[3], exdata[0], exdata[1], exdata[2], exdata[3], exdata[4], exdata[5], exdata[6], exdata[7]);
			}

			if (status & 0x01) {
				psize = mcp2515_readMSG(mcp_rx_rxb0, address, exdata);
				fprintf(stdout, "\tRXB0 (%u) -> %.2X %.2X %.2X %.2X | %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X\n", psize, address[0], address[1], address[2], address[3], exdata[0], exdata[1], exdata[2], exdata[3], exdata[4], exdata[5], exdata[6], exdata[7]);
			}

			mcp2515_writeRegister(MCP2515_REG_CANINTF, 0x00); // Clear interrupt flags
			interrupt_received = 0;
		} else {

			fprintf(stdout, "SENDING MESSAGE!\n");
			mcp2515_setupTX(mcp_tx_txb0, raddress, 8, 0, 0);
			mcp2515_loadMSG(mcp_tx_txb0, rexdata, 8);
			mcp2515_sendMSG(RTS_TXB0);
		//	while(!(mcp2515_readRegister(MCP2515_REG_CANINTF) & 0x04));
		}

		_delay_ms(1000);
	}

    return 0;
}

static void extInterruptINIT(void (*handler)(void)) {
	/* Set function pointer */
	int_handler = handler;
	/* Initialize external interrupt on pin INT0 on failing edge */
	EICRA |= (1 << ISC01);
	EIMSK |= (1 << INT0);
}

static void interrupt_handler(void) {
	interrupt_received = 1;
}

/* System interrupt handler */
SIGNAL(INT0_vect) {
	int_handler();
} 





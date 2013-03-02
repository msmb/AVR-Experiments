#include "twilib.h"

#include <util/twi.h>
#include <stdlib.h>

#include <stdio.h>

#define MAX_RETRIES 20

typedef enum {
	I2C_StartCommand = 0,
	I2C_DataNACKCommand = 1,
	I2C_DataACKCommand = 2,
	I2C_StopCommand = 3
} I2C_CommandType;

uint8_t I2C_internal_sendCommand(I2C_CommandType command);

I2C_Config *I2C_buildDefaultConfig(void) {
	I2C_Config *cfg = (I2C_Config*)malloc(sizeof(I2C_Config));

	cfg->psc = I2C_Psc_1;
	cfg->twbr_val = 0x30;

	return cfg;
}

void I2C_masterBegin(I2C_Config* config) {
	TWSR = (TWSR & 0xFC) | config->psc;
	TWBR = config->twbr_val;
}

// TWINT is the interrupt flag bit
// TWSTA is the start condition bit
// TWEN is the enable-TWI bit
// TWSTO is the stop-TWI bit

// TWCR is the TWI control register

uint8_t I2C_internal_sendCommand(I2C_CommandType command) {
	switch (command) {
		case I2C_StartCommand:
			TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
			break;
		case I2C_DataNACKCommand:
			TWCR = (1 << TWINT) | (1 << TWEN);
			break;
		case I2C_DataACKCommand:
			TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
			break;
		case I2C_StopCommand:
		default:
			TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWSTO);
			return 0;		
	}

	while (!(TWCR & (1 << TWINT))); // Wait until TWINT flag gets set in TWCR, meaning that the command was received.

	// Return the value in TWSR, masking the last bits (the prescaling)
	return (TWSR & 0xF8);
}

int I2C_masterWriteRegisterByte(I2C_Device *dev, uint8_t reg, uint8_t *data, uint8_t dLen) {
	uint8_t twiStatus;
	uint8_t retryCount = 0;
	int retval = 1;

	while (retryCount < MAX_RETRIES) {
		// Start command
		twiStatus = I2C_internal_sendCommand(I2C_StartCommand);
		if (twiStatus == TW_MT_ARB_LOST) { // We need to retry...
			retryCount++;
			continue; // Begin to loop again
		} else if ((twiStatus != TW_START) && (twiStatus != TW_REP_START)) {
			retval = -1;
			break;
		}

		// Now send slave address... asking for a write
		TWDR = ((dev->id << 4) & 0xF0) | ((dev->address << 1) & 0x0E) | TW_WRITE;
		twiStatus = I2C_internal_sendCommand(I2C_DataNACKCommand);
		if ((twiStatus == TW_MT_SLA_NACK) || (twiStatus == TW_MT_ARB_LOST)) {
			retryCount++;
			continue;
		} else if (twiStatus != TW_MT_SLA_ACK) {
			retval = -1;
			break;
		}

		// 8 bit register address
		TWDR = reg;
		twiStatus = I2C_internal_sendCommand(I2C_DataNACKCommand);
		if (twiStatus != TW_MT_DATA_ACK) {
			retval = -1;
			break;
		}

	
		// Data transmission
		for (int curData = 0; curData < dLen; curData++) {
			// Put data into register and transmit
			TWDR = data[curData];

			if (curData == (dLen - 1)) { // Last write... we need to send a NACK
				twiStatus = I2C_internal_sendCommand(I2C_DataNACKCommand);
				if (twiStatus != TW_MT_DATA_NACK) {
					retval = -1;
					break;
				}
			} else { // We have to perform other reads... send an ACK
				twiStatus = I2C_internal_sendCommand(I2C_DataACKCommand);
				if (twiStatus != TW_MT_DATA_ACK) {
					retval = -1;
					break;
				}
			}
		}

		// Transmission complete!
		break;
	}

	twiStatus = I2C_internal_sendCommand(I2C_StopCommand);	
	return retval; 
}

int I2C_masterReadRegisterByte(I2C_Device *dev, uint8_t reg, uint8_t *data, uint8_t dLen) {
	uint8_t twiStatus;
	uint8_t retryCount = 0;
	int retval = 1;

	while (retryCount < MAX_RETRIES) {
		// Start command
		twiStatus = I2C_internal_sendCommand(I2C_StartCommand);
		if (twiStatus == TW_MT_ARB_LOST) { // We need to retry...
			retryCount++;
			continue; // Begin to loop again
		} else if ((twiStatus != TW_START) && (twiStatus != TW_REP_START)) {
			retval = -1;
			break;
		}

		// Now send slave address... asking for a write
		TWDR = ((dev->id << 4) & 0xF0) | ((dev->address << 1) & 0x0E) | TW_WRITE;
		twiStatus = I2C_internal_sendCommand(I2C_DataNACKCommand);
		if ((twiStatus == TW_MT_SLA_NACK) || (twiStatus == TW_MT_ARB_LOST)) {
			retryCount++;
			continue;
		} else if (twiStatus != TW_MT_SLA_ACK) {
			retval = -1;
			break;
		}


/*
   		// 16 bit register address, maybe move this to a different function?

		// Send MSB of register
		TWDR = (reg >> 8) & 0xFF;
		twiStatus = I2C_internal_sendCommand(I2C_DataNACKCommand);
		if (twiStatus != TW_MT_DATA_ACK) {
			retval = -1;
			break;
		}

		// Send LSB of register
		TWDR = (reg & 0xFF);
		twiStatus = I2C_internal_sendCommand(I2C_DataNACKCommand);		
		if (twiStatus != TW_MT_DATA_ACK) {
			retval = -1;
			break;
		}
*/
	
		// 8 bit register address
		TWDR = reg;
		twiStatus = I2C_internal_sendCommand(I2C_DataNACKCommand);
		if (twiStatus != TW_MT_DATA_ACK) {
			retval = -1;
			break;
		}


		// Send start command again.
		twiStatus = I2C_internal_sendCommand(I2C_StartCommand);
		if (twiStatus == TW_MT_ARB_LOST) { // We need to retry...
			retryCount++;
			continue; // Begin to loop again
		} else if ((twiStatus != TW_START) && (twiStatus != TW_REP_START)) {
			retval = -1;
			break;
		}

		// Now send slave address... asking for a read
		TWDR = ((dev->id << 4) & 0xF0) | ((dev->address << 1) & 0x0E) | TW_READ;
		twiStatus = I2C_internal_sendCommand(I2C_DataNACKCommand);
		if ((twiStatus == TW_MR_SLA_NACK) || (twiStatus == TW_MR_ARB_LOST)) {
			retryCount++;
			continue;
		} else if (twiStatus != TW_MR_SLA_ACK) {
			retval = -1;
			break;
		}

		for (int curData = 0; curData < dLen; curData++) {
			// Read the data

			if (curData == (dLen - 1)) { // Last read... we need to send a NACK
				twiStatus = I2C_internal_sendCommand(I2C_DataNACKCommand);
				if (twiStatus != TW_MR_DATA_NACK) {
					retval = -1;
					break;
				}
			} else { // We have to perform other reads... send an ACK
				twiStatus = I2C_internal_sendCommand(I2C_DataACKCommand);
				if (twiStatus != TW_MR_DATA_ACK) {
					retval = -1;
					break;
				}
			}
		
			// Read the data!
			data[curData] = TWDR;
		}

		break;
	}

	twiStatus = I2C_internal_sendCommand(I2C_StopCommand);	
	return retval; 
}
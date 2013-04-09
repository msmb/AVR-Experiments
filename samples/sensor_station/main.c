#include <stdint.h>

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>

#include <stdio.h>
#include <stdlib.h>

#include <util/delay.h>

#include "main.h"
#include "uart.h"

#include "shifter_74595.h"
#include "hd44780-74595-interface.h"
#include "hd44780-highlevel.h"

#include "twilib.h"
#include "ds1307.h"

#include "owilib.h"
#include "ds18b20.h"

#define  SRAM_CHKVAL 0xDEAD

typedef struct {
	int8_t minInt, maxInt;
	uint16_t minDec, maxDec;
	uint8_t minSens, maxSens;
	DS1307_ToD minTime, maxTime;
} minmax_temp;

static hd44780_driver *lcdDriver = NULL;
static owi_conn *dsconn;
static uint8_t *sensor_addrs, sensor_count;

static minmax_temp mmsens;

static volatile int8_t pressedKey = -1;
static volatile int8_t clock_sec = 0;

static uint8_t lcd_graphics[5][8] = {{
		0b00001110,
		0b00011011,
		0b00011111,
		0b00001110,
		0b00000110,
		0b00001110,
		0b00000110,
		0b00001110
	},
	{
		0b00001110,
		0b00010001,
		0b00010001,
		0b00010001,
		0b00001110,
		0b00001110,
		0b00001110,
		0b00000100
	},
	{
		0b00011100,
		0b00011110,
		0b00011110,
		0b00000110,
		0b00000110,
		0b00011110,
		0b00011110,
		0b00011100
	},
	{
		0b00000011,
		0b00000011,
		0b00000011,
		0b00000011,
		0b00001011,
		0b00011111,
		0b00011111,
		0b00001000
	},
	{
		0b00000100,
		0b00001110,
		0b00011111,
		0b00000100,
		0b00000100,
		0b00000100,
		0b00000100,
		0b00000000
	}
};

void sys_setup(void);
void clock_setup(uint8_t backEnabled);
int clock_checkAndSet(int8_t *data);
void print_standardClock(void);
void send_to_sleep(void);
void print_sensors(void);
void get_minmaxTemp(void);
uint16_t bsd_chksum(uint8_t *buf, uint8_t len);

int main(void) {
	char str_buffer[21];

	sys_setup();

	// First temp conversion
	ds18b20_startTempConversion(dsconn, NULL);	
	get_minmaxTemp(); 

	print_standardClock();
	print_sensors();

	uint8_t curSensor = 0;

	uint8_t clkCounter = 0;
	int8_t curKey;
	while (1) {
		if (clock_sec) {
			clock_sec = 0;
			clkCounter++;

			 hd44780_hl_printChar(lcdDriver, 0, 15, clkCounter % 2 ? ':' : ' ');
		}

		if (!(clkCounter % 30)) {
			clkCounter = 0;

			print_standardClock();
			get_minmaxTemp();
		}

		if (pressedKey >= 0) {
			curKey = pressedKey;
			pressedKey = -1;

			switch (curKey) {
			case 0:
			case 1:
			case 2:
				if (curKey < sensor_count)
					curSensor = curKey;
				break;
			case 4:
			case 5:
			case 6:
				if (curKey - 1 < sensor_count)
					curSensor = curKey - 1;				
				break;
			case 8:
			case 9:
			case 10:
				if (curKey - 2 < sensor_count)				
					curSensor = curKey - 2;				
				break;
			case 13:
				if (curKey - 3 < sensor_count)				
					curSensor = curKey - 3;
				break;
			case 11:
				clock_setup(1);
				print_standardClock();
				print_sensors();
				hd44780_hl_setCursor(lcdDriver, 0, 0);	
				break;
			default:
				break;
			}
		}

		if (sensor_count && curSensor < sensor_count) {
			int8_t integ;
			uint16_t decim, crc;

			//sprintf(str_buffer, "%.2u: %.2X%.2X%.2X%.2X%.2X%.2X%.2X%.2X", curSensor + 1, sensor_addrs[(8 * curSensor) + 0], sensor_addrs[(8 * curSensor) + 1], sensor_addrs[(8 * curSensor) + 2], sensor_addrs[(8 * curSensor) + 3], sensor_addrs[(8 * curSensor) + 4], sensor_addrs[(8 * curSensor) + 5], sensor_addrs[(8 * curSensor) + 6], sensor_addrs[(8 * curSensor) + 7]);
			
			crc = bsd_chksum(sensor_addrs + (8 * curSensor) + 1, 7);
			sprintf(str_buffer, "ID:%.4X", crc);
			hd44780_hl_printText(lcdDriver, 2, 0, str_buffer);

			for (uint8_t idx = 0; idx < 21; idx++)
				str_buffer[idx] = ' ';
			str_buffer[13] = 0;
			str_buffer[2 + curSensor] = 3;

			hd44780_hl_printText(lcdDriver, 2, 7, str_buffer);

			ds18b20_getTemp(dsconn, sensor_addrs + (8 * curSensor), &integ, &decim);			
			sprintf(str_buffer, "Temperatura %c%.2d.%.4u", integ < 0 ? '-' : '+', integ < 0 ? -integ : integ, decim);
			hd44780_hl_printText(lcdDriver, 3, 0, str_buffer);			
		}

		// Ask for temperature conversion
		ds18b20_startTempConversion(dsconn, NULL);
		
		// Sleep time...		
		send_to_sleep();
	}

	return 0;
}

void send_to_sleep(void) {
	sleep_enable();		
	set_sleep_mode(SLEEP_MODE_PWR_DOWN);		
	cli();
	sleep_bod_disable();
	sei();
	sleep_cpu();
	sleep_disable();
}

void print_standardClock(void) {
	char timeStrBuffer[21];
	DS1307_ToD time;

	DS1307_readToD(&time);
	sprintf(timeStrBuffer, "%.2u/%.2u/%.4u   %.2u:%.2u \x06", time.dayOfMonth, time.month, time.year, time.hours, time.minutes);
	hd44780_hl_printText(lcdDriver, 0, 0, timeStrBuffer);
}

void print_sensors(void) {
	char strBuffer[21];

	sprintf(strBuffer, "Sensori:");
	uint8_t idx = 9;

	strBuffer[idx - 1] = ' ';	
	strBuffer[idx + 0] = sensor_count > 0 ? '1' : '_';
	strBuffer[idx + 1] = sensor_count > 1 ? '2' : '_';
	strBuffer[idx + 2] = sensor_count > 2 ? '3' : '_';
	strBuffer[idx + 3] = sensor_count > 3 ? '4' : '_';
	strBuffer[idx + 4] = sensor_count > 4 ? '5' : '_';
	strBuffer[idx + 5] = sensor_count > 5 ? '6' : '_';
	strBuffer[idx + 6] = sensor_count > 6 ? '7' : '_';
	strBuffer[idx + 7] = sensor_count > 7 ? '8' : '_';
	strBuffer[idx + 8] = sensor_count > 8 ? '9' : '_';
	strBuffer[idx + 9] = sensor_count > 9 ? '0' : '_';
	strBuffer[idx + 10] = 0;

	hd44780_hl_printText(lcdDriver, 1, 0, strBuffer);
}

void get_minmaxTemp(void) {
	int8_t integ;
	uint16_t decim;

	DS1307_ToD time;
	DS1307_readToD(&time);	

	for (uint8_t idx = 0; idx < sensor_count; idx++) {
			ds18b20_getTemp(dsconn, sensor_addrs + (8 * idx), &integ, &decim);

			if (integ < mmsens.minInt || ((integ == mmsens.minInt) && (decim < mmsens.minDec))) {
				mmsens.minInt = integ;
				mmsens.minDec = decim;
				mmsens.minSens = idx;
				mmsens.minTime = time;
			}
		
			if (integ > mmsens.maxInt || ((integ == mmsens.maxInt) && (decim > mmsens.maxDec))) {
				mmsens.maxInt = integ;
				mmsens.maxDec = decim;
				mmsens.maxSens = idx;
				mmsens.maxTime = time;				
			}					
	}
}

void sys_setup(void) {
	char strbuf[40];

	// Initialize serial port for output
	uart_init();
	stdout = &uart_output;
	stdin  = &uart_input;

	// Shifter connections
	DDRC |= 0x0F; // First 4 pins of Port C set as output
	PORTC &= 0xF0;

	// Key encoder connections
	DDRB &= 0xF0; // First 4 pins of Port B set as Input
	PORTB &= 0xF0;
	DDRD &= 0x7F; // Last pin of Port D set as Input (will be used as interrupt)
	PORTD &= 0x7F;

	dsconn = (owi_conn*)malloc(sizeof(owi_conn));
	dsconn->port = &PORTD;
	dsconn->pin = &PIND;
	dsconn->ddr = &DDRD;
	dsconn->pinNum = 7;

	// Enable interrupts
	EIMSK |= 1 << INT1;  //Enable INT0
	EICRA |= (1 << ISC10) | (1 << ISC11); //Trigger on rising edge of INT1

	sei();

	// Init LCD
	shifter_74595_conn *shfConn = shf74595_createConnection(&PORTC, 3, &PORTC, 2, &PORTC, 1, &PORTC, 0);
	shf74595_initConnection(shfConn);

	hd44780_74595_connection *conn_struct = hd44780_74595_createConnection(shfConn, 0, 5, 4);
	lcdDriver = hd44780_hl_createDriver(TMBC20464BSP_20x4, conn_struct, (uint8_t ( *)(void *))hd44780_74595_initLCD4Bit, (void ( *)(void *, uint16_t))hd44780_74595_sendCommand);
	hd44780_hl_init(lcdDriver, 0, 0);

	hd44780_hl_setCustomFont(lcdDriver, 3, lcd_graphics[4]);	
	hd44780_hl_setCustomFont(lcdDriver, 4, lcd_graphics[0]);
	hd44780_hl_setCustomFont(lcdDriver, 5, lcd_graphics[1]);
	hd44780_hl_setCustomFont(lcdDriver, 6, lcd_graphics[2]);
	hd44780_hl_setCustomFont(lcdDriver, 7, lcd_graphics[3]);


	// Init TWI
	I2C_Config *masterConfig = I2C_buildDefaultConfig();
	I2C_masterBegin(masterConfig);

	// Print first line
	hd44780_hl_clear(lcdDriver);

	// Check the clock
	DDRD &= 0xFB; // Set pin 2 of Port D as input. Will be used as interrupt
	PORTD &= 0xFB;

	fprintf(stdout, "Checking DS1307 status...\n");
	uint16_t ds_chkval;
	DS1307_readSRAM((uint8_t *)&ds_chkval, 2);
	if (ds_chkval != SRAM_CHKVAL) { // The clock lost power!
		fprintf(stdout, "Clock needs configuration!!!\n");

		ds_chkval = SRAM_CHKVAL;

		clock_setup(0);

		DS1307_writeSRAM((uint8_t *)&ds_chkval, 2); // Mark clock SRAM
	} else { // Clock is fine
		fprintf(stdout, "Clock check passed!!!\n");
	}
	DS1307_setSQW(1, 0, DS1307_SQW_1Hz);

	EIMSK |= 1 << INT0;  //Enable INT0
	EICRA |= 1 << (1 << ISC00) | (1 << ISC01); //Trigger on rising edge of INT0

	hd44780_hl_clear(lcdDriver);

	owi_reset(dsconn);
	owi_searchROM(dsconn, NULL, &sensor_count, 0);

	sprintf(strbuf, "Sensori individuati\n%u\n", sensor_count);
	hd44780_hl_printText(lcdDriver, 0, 0, strbuf);

	if (sensor_count) {
		sensor_addrs = (uint8_t*)malloc(8 * sensor_count);
		owi_searchROM(dsconn, sensor_addrs, &sensor_count, 0);		

		ds18b20_cfg dscfg;
		dscfg.thrmcfg = DS_THRM_12BIT;
		dscfg.lT = 0;
		dscfg.hT = 0;
		ds18b20_setCFG(dsconn, NULL, &dscfg);
	}

	_delay_ms(2000);

	hd44780_hl_clear(lcdDriver);	

	// Ensure we'll update the values
	mmsens.minInt = 127;
	mmsens.maxInt = -127;

	fprintf(stdout, "Starting up.\n");
}

int clock_checkAndSet(int8_t *data) {
	DS1307_ToD time;

	for (uint8_t counter = 0; counter < 10; counter++) {
		if (data[counter] < 0) return 0;
	}

	time.seconds = 0;
	time.minutes = data[8] * 10 + data[9];
	if (time.minutes > 59) return 0;
	time.hours = data[6] * 10 + data[7];
	if (time.hours > 23) return 0;

	time.dayOfMonth = data[0] * 10 + data[1];
	if (time.dayOfMonth > 31) return 0;
	time.month = data[2] * 10 + data[3];
	if (time.month > 12) return 0;
	time.year = data[4] * 10 + data[5] + 2000;

	time.dayOfWeek = dayOfWeek(time.dayOfMonth, time.month, time.year);
	time.day12 = 0;

	fprintf(stdout, "Setting time:\n time.minutes = %.2d\n time.hours = %.2d\n time.dayOfMonth = %.2d\n time.month = %.2d\n time.year = %.4d\n time.dayOfWeek = %.2d\n", time.minutes, time.hours, time.dayOfMonth, time.month, time.year, time.dayOfWeek);

	DS1307_writeToD(&time);
	DS1307_setSQW(1, 0, DS1307_SQW_1Hz);

	return 1;
}

void clock_setup(uint8_t backEnabled) {
	uint8_t key_table[] = {0x31, 0x32, 0x33, 0x04, 0x34, 0x35, 0x36, 0x05, 0x37, 0x38, 0x39, 0x06, 0x23, 0x30, 0x2a, 0x07};
	uint8_t skip_table[] = {0, 1, 3, 4, 8, 9, 15, 16, 18, 19};
	int8_t timeData[10] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
	uint8_t cur_pos = 0;

	hd44780_hl_clear(lcdDriver);
	hd44780_hl_printText(lcdDriver, 0, 0, "Config. orario:\nDD/MM/20YY     hh:mm");

	if (backEnabled)
		hd44780_hl_printText(lcdDriver, 3, 0, "\x7F#    \x06NO  \x07OK    *\x7E");
	else
		hd44780_hl_printText(lcdDriver, 3, 0, "\x7F#      \x07OK       *\x7E");

	hd44780_hl_moveCursor(lcdDriver, 1, skip_table[cur_pos]);
	hd44780_hl_setCursor(lcdDriver, 1, 1);

	uint8_t looping = 1;
	int8_t curKey = -1;

	while (looping) {

		if (pressedKey >= 0) {
			curKey = pressedKey;
			pressedKey = -1;

			switch (curKey) {
			case -1:
				continue;
			case 11:
				if (backEnabled)
					looping = 0;
				break;
			case 0:
			case 1:
			case 2:
			case 4:
			case 5:
			case 6:
			case 8:
			case 9:
			case 10:
			case 13:
				hd44780_hl_printCharAtCurrentPosition(lcdDriver, key_table[curKey]);
				timeData[cur_pos] = key_table[curKey] - 0x30;

				cur_pos = (cur_pos + 1) % sizeof(skip_table);
				hd44780_hl_moveCursor(lcdDriver, 1, skip_table[cur_pos]);
				break;
			case 12: // Left
				cur_pos = (((cur_pos - 1) < 0) ? (sizeof(skip_table) - 1) : (cur_pos - 1));
				hd44780_hl_moveCursor(lcdDriver, 1, skip_table[cur_pos]);
				break;
			case 14: // Right
				cur_pos = (cur_pos + 1) % sizeof(skip_table);
				hd44780_hl_moveCursor(lcdDriver, 1, skip_table[cur_pos]);
				break;
			case 15: // Enter
				if (clock_checkAndSet(timeData)) {
					looping = 0;
				} else {
					hd44780_hl_printText(lcdDriver, 2, 0, "    ** ERRORE **");
					hd44780_hl_moveCursor(lcdDriver, 1, skip_table[cur_pos]);
				}
				break;
			default:
				break;
			}
		}
	}

	hd44780_hl_clear(lcdDriver);
	hd44780_hl_setCursor(lcdDriver, 0, 0);
}

// INTERRUPTS
ISR(INT0_vect) {
	sleep_disable();
	clock_sec = 1;
}

ISR(INT1_vect) {
	sleep_disable();
	pressedKey = PINB & 0xF;
}

uint16_t bsd_chksum(uint8_t *buf, uint8_t len) {
	uint16_t chk = 0;

	for (uint8_t idx = 0; idx < len; idx++) {
		chk = (chk >> 1) + ((chk & 1) << 15);
		chk += buf[idx]; 
	}

	return chk;
}


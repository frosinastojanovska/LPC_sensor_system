/*****************************************************************************
 *   Value read from BNC is written to the OLED display (nothing graphical
 *   yet only value).
 *
 *   Copyright(C) 2010, Embedded Artists AB
 *   All rights reserved.
 *
 ******************************************************************************/

#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_i2c.h"
#include "lpc17xx_ssp.h"
#include "lpc17xx_adc.h"
#include "lpc17xx_timer.h"

#include "oled.h"
#include "temp.h"
#include "joystick.h"
#include "rotary.h"
#include "led7seg.h"
#include "light.h"
#include "pca9532.h"
#include "eeprom.h"

#define BUFF_LEN 20
#define TEMP_ADD 0
#define LIGHT_ADD 321
#define POTEN_ADD 641


static uint32_t msTicks = 0;
static uint8_t buf[10];
static uint16_t data_temp[20];
static uint16_t data_light[20];
static uint16_t data_poten[20];
static int data_type;
static int mode;
static uint8_t ch7seg = '0';
static int draw_graph;

static void intToString(int value, uint8_t* pBuf, uint32_t len, uint32_t base)
{
    static const char* pAscii = "0123456789abcdefghijklmnopqrstuvwxyz";
    int pos = 0;
    int tmpValue = value;

    // the buffer must not be null and at least have a length of 2 to handle one
    // digit and null-terminator
    if (pBuf == NULL || len < 2)
    {
        return;
    }

    // a valid base cannot be less than 2 or larger than 36
    // a base value of 2 means binary representation. A value of 1 would mean only zeros
    // a base larger than 36 can only be used if a larger alphabet were used.
    if (base < 2 || base > 36)
    {
        return;
    }

    // negative value
    if (value < 0)
    {
        tmpValue = -tmpValue;
        value    = -value;
        pBuf[pos++] = '-';
    }

    // calculate the required length of the buffer
    do {
        pos++;
        tmpValue /= base;
    } while(tmpValue > 0);


    if (pos > len)
    {
        // the len parameter is invalid.
        return;
    }

    pBuf[pos] = '\0';

    do {
        pBuf[--pos] = pAscii[value % base];
        value /= base;
    } while(value > 0);

    return;
}

static void init_ssp(void)
{
	SSP_CFG_Type SSP_ConfigStruct;
	PINSEL_CFG_Type PinCfg;

	/*
	 * Initialize SPI pin connect
	 * P0.7 - SCK;
	 * P0.8 - MISO
	 * P0.9 - MOSI
	 * P2.2 - SSEL - used as GPIO
	 */
	PinCfg.Funcnum = 2;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 7;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 8;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 9;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Funcnum = 0;
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 2;
	PINSEL_ConfigPin(&PinCfg);

	SSP_ConfigStructInit(&SSP_ConfigStruct);

	// Initialize SSP peripheral with parameter given in structure above
	SSP_Init(LPC_SSP1, &SSP_ConfigStruct);

	// Enable SSP peripheral
	SSP_Cmd(LPC_SSP1, ENABLE);

}

static void init_i2c(void)
{
	PINSEL_CFG_Type PinCfg;

	/* Initialize I2C2 pin connect */
	PinCfg.Funcnum = 2;
	PinCfg.Pinnum = 10;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 11;
	PINSEL_ConfigPin(&PinCfg);

	// Initialize I2C peripheral
	I2C_Init(LPC_I2C2, 100000);

	/* Enable I2C1 operation */
	I2C_Cmd(LPC_I2C2, ENABLE);
}

static void init_adc(void)
{
	PINSEL_CFG_Type PinCfg;

	/*
	 * Init ADC pin connect
	 * AD0.0 on P0.23
	 */
	PinCfg.Funcnum = 1;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Pinnum = 23;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);

	/* Configuration for ADC :
	 * 	Frequency at 0.2Mhz
	 *  ADC channel 0, no Interrupt
	 */
	ADC_Init(LPC_ADC, 200000);
	ADC_IntConfig(LPC_ADC,ADC_CHANNEL_0,DISABLE);
	ADC_ChannelCmd(LPC_ADC,ADC_CHANNEL_0,ENABLE);

}


void SysTick_Handler(void) {
    msTicks++;
}

static uint32_t getTicks(void)
{
    return msTicks;
}

static void change7Seg(int value)
{
    if (value == 0)
    	ch7seg = '0';
    else if (value == 1)
    	ch7seg = '1';
    else
    	ch7seg = '2';
    led7seg_setChar(ch7seg, FALSE);
}


void fill_buffer(int32_t new_data, uint16_t* data){
	for(int i=0; i<(BUFF_LEN - 1); i++){
		data[i] = data[i+1];
	}
	data[BUFF_LEN - 1] = new_data;
}

int write_to_eeprom(uint16_t *data, uint16_t offset){
	int16_t len = 0;
	for(int i=0; i<(BUFF_LEN - 1); i++){
		uint8_t temp[2];
		// last 8 bits
		temp[1] = (uint8_t)data[i];
		// first 8 bits
		temp[0] = (uint8_t)(data[i] >> 8);
		len = eeprom_write(temp, (uint16_t)(offset + i*2), (uint16_t)2);
		if(len != 2)
			return 1;
	}
	return 0;
}


int main (void) {
    uint8_t joy = 0;

    init_i2c();
    init_ssp();
    init_adc();

    oled_init();
    temp_init(&getTicks);
    joystick_init();
    rotary_init();
    led7seg_init();
    light_init();
    pca9532_init();
    eeprom_init();

	int32_t light = 0;
    int32_t t = 0;
    int32_t potenc = 0;

    uint8_t btn_1 = 0;
    int result = 0;

    if (SysTick_Config(SystemCoreClock / 1000)) {
    	while (1);  // Capture error
    }

    light_enable();
    light_setRange(LIGHT_RANGE_4000);

	oled_clearScreen(OLED_COLOR_WHITE);
	draw_graph_outline(3, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

    while(1) {

    	joy = joystick_read();

		if ((joy & JOYSTICK_LEFT) != 0) {
			// temperature
			data_type = 0;
			oled_clearScreen(OLED_COLOR_WHITE);
			draw_graph_outline(3, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
		}

		if ((joy & JOYSTICK_UP) != 0) {
			// light
			data_type = 1;
			oled_clearScreen(OLED_COLOR_WHITE);
			draw_graph_outline(5, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
		}

		if ((joy & JOYSTICK_RIGHT) != 0) {
			// potenciometer
			data_type = 2;
			oled_clearScreen(OLED_COLOR_WHITE);
			draw_graph_outline(4, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    	}

		btn_1 = ((GPIO_ReadValue(0) >> 4) & 0x01);

		if (btn_1 == 0){
			mode++;
			if(mode > 2)
				mode = 0;
			draw_graph = 1;
		}

		change7Seg(mode);

		if(mode == 0){

			// real - time buffer
			if (data_type == 0){
				if (draw_graph == 1){
					draw_graph = 0;
					oled_clearScreen(OLED_COLOR_WHITE);
					draw_graph_outline(3, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				}
				/* Temperature */
				oled_fillRect(75, 1, 90, 13, OLED_COLOR_WHITE);
				oled_putString(1, 1, "Temperature:  ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				t = temp_read() / 10;
				intToString(t, buf, 10, 10);
				oled_fillRect(80, 0, 90, 8, OLED_COLOR_WHITE);
				oled_putString(80, 1, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				fill_buffer(t, data_temp);
				draw_data(25, 35, data_temp, BUFF_LEN);
			}
			else if (data_type == 1){
				if (draw_graph == 1){
					draw_graph = 0;
					oled_clearScreen(OLED_COLOR_WHITE);
					draw_graph_outline(5, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				}
				/* Light */
				oled_fillRect(75, 1, 90, 13, OLED_COLOR_WHITE);
				oled_putString(1, 1, "Light:  ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				light = light_read();
				intToString(light, buf, 10, 10);
				oled_fillRect(60, 0, 90, 8, OLED_COLOR_WHITE);
				oled_putString(60, 1, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				fill_buffer(light, data_light);
				draw_data(0, 500, data_light, BUFF_LEN);
			}
			else{
				if (draw_graph == 1){
					draw_graph = 0;
					oled_clearScreen(OLED_COLOR_WHITE);
					draw_graph_outline(4, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				}
				/* Trimpot */
				oled_fillRect(75, 1, 90, 13, OLED_COLOR_WHITE);
				oled_putString(1, 1, "Poten:  ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				ADC_StartCmd(LPC_ADC,ADC_START_NOW);
				//Wait conversion complete
				while (!(ADC_ChannelGetStatus(LPC_ADC,ADC_CHANNEL_0,ADC_DATA_DONE)));
				potenc = ADC_ChannelGetData(LPC_ADC,ADC_CHANNEL_0);
				intToString(potenc, buf, 10, 10);
				oled_fillRect(60, 0, 90, 8, OLED_COLOR_WHITE);
				oled_putString(60, 1, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				fill_buffer(potenc, data_poten);
				draw_data(99, 4100, data_poten, BUFF_LEN);
			}
		}
		else if(mode == 1){
			// zachuvaj vo memorija
			oled_clearScreen(OLED_COLOR_WHITE);
			oled_putString(1, 1, "Write to EEPROM:  ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			if(data_type == 0){
				t = temp_read() / 10;
				fill_buffer(t, data_temp);
				result = write_to_eeprom(data_temp, (uint16_t)TEMP_ADD);
				oled_putString(20, 35, "Temperature", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				if(result == 1)
					return 1;
			}
			else if(data_type == 1){
				light = light_read();
				fill_buffer(light, data_light);
				result = write_to_eeprom(data_light, (uint16_t)LIGHT_ADD);
				oled_putString(20, 35, "Light", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				if(result == 1)
					return 1;
			}
			else{
				ADC_StartCmd(LPC_ADC,ADC_START_NOW);
				//Wait conversion complete
				while (!(ADC_ChannelGetStatus(LPC_ADC,ADC_CHANNEL_0,ADC_DATA_DONE)));
				potenc = ADC_ChannelGetData(LPC_ADC,ADC_CHANNEL_0);
				fill_buffer(potenc, data_poten);
				result = write_to_eeprom(data_poten, (uint16_t)POTEN_ADD);
				oled_putString(20, 35, "Potentiometer", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				if(result == 1)
					return 1;
			}
		}
		else{
			// prikazhi snimeno
			oled_clearScreen(OLED_COLOR_WHITE);
			oled_putString(1, 1, "Snimeno:  ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

		}
    	/* delay */
    	Timer0_Wait(1000);
    }

}

void check_failed(uint8_t *file, uint32_t line)
{
	/* User can add his own implementation to report the file name and line number,
	 ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

	/* Infinite loop */
	while(1);
}

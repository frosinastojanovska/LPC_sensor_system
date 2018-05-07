#include <string.h>
#include "lpc17xx_gpio.h"
#include "lpc17xx_i2c.h"
#include "lpc17xx_ssp.h"
#include "oled.h"
#include "font5x7.h"

oled_color_t color_data;
oled_color_t color_bg_data;


/******************************************************************************
 *
 * Description:
 *    Draw a initial graph with title
 *
 * Params:
 *   [in] x0 - start x position
 *   [in] y0 - start y position
 *   [in] x1 - end x position
 *   [in] y1 - end y position
 *   [in] color - color of the line
 *
 *****************************************************************************/
void draw_graph_outline(uint8_t delimiter, oled_color_t color, oled_color_t color_bg)
{
	color_data = color;
	color_bg_data = color_bg;
	oled_clearScreen(color_bg);
	oled_line(10, 15, 10, 57, color);
	oled_line(10, 57, 90, 57, color);
	// vertical arrow
	oled_line(10, 11, 8, 14, color);
	oled_line(10, 11, 12, 14, color);
	// horizontal arrow
	oled_line(94, 57, 91, 55, color);
	oled_line(94, 57, 91, 59, color);
	int slot = 40 / delimiter;
	for (int i=0; i < delimiter; i++){
		oled_line(8, 17+slot*i, 12, 17+slot*i, color);
	}
}


void draw_data(uint16_t min, uint16_t max, uint16_t* data, uint16_t data_size){
	oled_fillRect(11, 15, 90, 56, color_bg_data);
	int offset_x = 80 / data_size;
	for(int i=0; i< data_size; i++){
		float temp = ((float)(data[i]-min))/(max-min) * 40;
		if(temp < 0)
			temp=0;
		if(temp > 40)
			temp = 40;
		uint16_t norm_data = (uint16_t) temp;
		int offset_y = 40 - norm_data;
		if(i>0){
			float temp2 = ((float)(data[i-1]-min))/(max-min) * 40;
			if(temp2 < 0)
				temp2=0;
			if(temp2 > 40)
				temp2 = 40;
			uint16_t norm_data2 = (uint16_t) temp2;
			int offset_y2 = 40 - norm_data2;
			oled_line(10 + offset_x*(i-1), 17 + offset_y2, 10 + offset_x*i, 17 + offset_y, color_data);
		}
		if(offset_y < 40)
			oled_circle(10 + offset_x*i, 17 + offset_y, 1, color_data);
	}
}

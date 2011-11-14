/*
 * sensor_gpio.h
 *
 * Copyright Peter Madsen, 2011
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define SDA1	21
#define DIR1	13
#define CLK1	73
#define ADC1	0

#define SDA2	20
#define DIR2	18
#define CLK2	75
#define ADC2	1

#define SDA3	23
#define DIR3	14
#define CLK3	72
#define ADC3	2

#define SDA4	19
#define DIR4	22
#define CLK4	74
#define ADC4	3

struct sensor_port_struct {
	int sda;
	int sda_dir;
	int scl;
	int adc_channel;
} sensorport[] = {
	{SDA1, DIR1, CLK1, ADC1},
	{SDA2, DIR2, CLK2, ADC2},
	{SDA3, DIR3, CLK3, ADC3},
	{SDA4, DIR4, CLK4, ADC4}
};

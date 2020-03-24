/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#ifndef _MAINWND_H_
#define _MAINWND_H_


#include <ctype.h>
#include "main.h"
//#include <stdbool.h>
//#include "stm32f7xx_hal_adc.h"

void MainWnd(void);
static char    fileNames[13][13];
static uint16_t  FileNo;
volatile int   Page;
static uint32_t date, time;
uint32_t RTCpresent;
volatile int NoDate;
ADC_HandleTypeDef Adc3Handle;
#endif

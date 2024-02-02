#ifndef LIB_COMMON_H_
#define LIB_COMMON_H_
#include "esp_err.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

typedef struct
{
	int time_year;
	int time_month;
	int time_day;
	int time_hour;
	int time_min;
	int time_sec;

	float temp;
	float humi;
	float PM2_5;
	float PM10;

} data_type;
#endif

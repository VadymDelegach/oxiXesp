#include "esp_sntp.h"
#include "driver/uart.h"
#include "oxiXesp.h"

struct tm timeinfo = {0}; // calendar time structure
bool rtc_req = false; // request for RTC synchronization

void rtc_sync_task(void* pvParameter)
{
	char rtc_msg[64] = {0};
	char num[5] = {0};

	strcpy(rtc_msg, TAG_OXI);
	strcat(rtc_msg, "RTC:");
	strcat(rtc_msg, itoa(timeinfo.tm_year + 1900, num, 10));
	strcat(rtc_msg, ";");
	strcat(rtc_msg, itoa(timeinfo.tm_mon + 1, num, 10));
	strcat(rtc_msg, ";");
	strcat(rtc_msg, itoa(timeinfo.tm_mday, num, 10));
	strcat(rtc_msg, ";");
	strcat(rtc_msg, itoa(timeinfo.tm_hour, num, 10));
	strcat(rtc_msg, ";");
	strcat(rtc_msg, itoa(timeinfo.tm_min, num, 10));
	strcat(rtc_msg, "\n");
	uart_write_bytes(UART_NUM_0, rtc_msg, strlen(rtc_msg));
	vTaskDelete(NULL);
}

/**
 * @brief Time sync notification callback, called every hour in this application
 * 
 * Converts seconds to calendar time. If RTC synchronization request is present,
 * then create task for send calendar time to STM32
 * 
 * @param tv pointer to struct timeval with current time
 */
static void time_sync_notification_cb(struct timeval *tv)
{
	localtime_r(&tv->tv_sec, &timeinfo);
	/*printf("Current time: %02d:%02d:%02d, Date: %02d.%02d.%04d\n",
		timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
		timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);*/
	if (rtc_req) {
		rtc_req = false;
		xTaskCreate(rtc_sync_task, "RTC sync", 2048, NULL, tskIDLE_PRIORITY + 1,
																		NULL);
	}
}

/**
 * @brief Initialize SNTP
 * 
 * Set timezone with daylight, SNTP operating mode to pool, SNTP server is
 * pool.ntp.org, sync mode to immed, and set time sync notification callback. 
 * 
 */
void init_sntp(void)
{
	setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
	tzset();
	esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
	esp_sntp_setservername(0, "pool.ntp.org");
	//esp_sntp_servermode_dhcp(1);
	esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
	esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
	esp_sntp_init();
}

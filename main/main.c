#include <stdio.h>
#include "nvs_flash.h"
#include "wifi_provisioning/manager.h"
#include "driver/uart.h"
#include "oxiXesp.h"

QueueHandle_t uart0_queue;

void app_main(void)
{
	//Initialize NVS
    esp_err_t er = nvs_flash_init();
	if (er == ESP_ERR_NVS_NO_FREE_PAGES ||
									    er == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        if ((er = nvs_flash_init()) != ESP_OK) {
			oxi_err_check("Initializing NVS after erase", er);
            esp_restart();
        }
    } else if (er != ESP_OK) {
		oxi_err_check("Initializing NVS", er);
        esp_restart();
    }
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
	er = uart_driver_install(UART_NUM_0, BUF_SIZE, BUF_SIZE, 20,
															&uart0_queue, 0);
	if (er != ESP_OK) {
		oxi_err_check("Initializing UART", er);
		esp_restart();
	}
	er = uart_param_config(UART_NUM_0, &uart_config);
	if (er != ESP_OK) {
		oxi_err_check("Setting UART parameters", er);
		esp_restart();
	}
	er = uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
									UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
	if (er != ESP_OK) {
		oxi_err_check("Setting UART pins", er);
		esp_restart();
	}
	xTaskCreate(uart_event_task, "uart_event_task", 4096, NULL,
													tskIDLE_PRIORITY + 1, NULL);
	/* Init WiFi */
	if (wifi_init() != ESP_OK)
		esp_restart();
	/* Registration BLE provisioning event handler */
	register_ble_ev_hndl();
}

void oxi_err_check(const char *dscr, esp_err_t er)
{
	if (er != ESP_OK) {
		printf("%s%s%s - %s\n", TAG_OXI, OXI_ERR, dscr, esp_err_to_name(er));
	}
}
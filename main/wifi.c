#include "esp_wifi.h"
#include "wifi_provisioning/manager.h"
#include "oxiXesp.h"

esp_netif_t* sta_netif;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
											int32_t event_id, void *event_data)
{
	bool provisioned = false;

	switch (event_id) {
		case WIFI_EVENT_STA_START:
			printf("%sWIFI start\n", TAG_OXI);
			wifi_prov_mgr_is_provisioned(&provisioned);
			if (provisioned)
				oxi_err_check("WIFI connect", esp_wifi_connect());
			break;
		case WIFI_EVENT_STA_DISCONNECTED:
			printf("OXI:WIFI stantion disconnected\n");
			break;
		case WIFI_EVENT_STA_STOP:
			printf("OXI:WIFI stopped\n");
	}
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
											int32_t event_id, void *event_data)
{
	if (event_id == IP_EVENT_STA_GOT_IP) {
		wifi_config_t cfg = {0};
		oxi_err_check("WIFI get config",esp_wifi_get_config(WIFI_IF_STA, &cfg));
		printf("OXI:SSID:%s IP:%d.%d.%d.%d\n", (char *)cfg.sta.ssid,
					IP2STR(&((ip_event_got_ip_t *)event_data)->ip_info.ip));
	}
}

esp_err_t wifi_init(void)
{
	esp_err_t er;

	if ((er = esp_netif_init()) != ESP_OK) {
		oxi_err_check("NETIF init", er);
		return er;
	}
	if ((er = esp_event_loop_create_default()) != ESP_OK) {
		oxi_err_check("Event loop", er);
		return er;
	}
	sta_netif = esp_netif_create_default_wifi_sta();
	assert(sta_netif);
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	if ((er = esp_wifi_init(&cfg)) != ESP_OK) {
		oxi_err_check("WIFI init", er);
		return er;
	}
	if ((er = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
									&ip_event_handler, NULL, NULL)) != ESP_OK)
	{
		oxi_err_check("IP event registration", er);
		return er;
	}
	if ((er = esp_event_handler_instance_register(WIFI_EVENT,
				ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL)) != ESP_OK)
	{
		oxi_err_check("WIFI event registration", er);
		return er;
	}
	if ((er = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) {
		oxi_err_check("WIFI set mode", er);
		return er;
	}
	bool provisioned = false;
	oxi_err_check("Is provisioned", wifi_prov_mgr_is_provisioned(&provisioned));
	if (provisioned)
		oxi_err_check("WIFI start", esp_wifi_start());
	else
		printf("%sWiFi initialized, not provisioned\n", TAG_OXI);
	return ESP_OK;
}
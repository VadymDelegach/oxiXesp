#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include "esp_wifi.h"
//#include "esp_log.h"
#include "oxiXesp.h"

#define BLE_PROV_TAG "BLE PROV:"

static void prov_event_handler(void *arg, esp_event_base_t event_base,
											int32_t event_id, void *event_data)
{
	if (event_base != WIFI_PROV_EVENT)
        return;
	switch (event_id) {
		case WIFI_PROV_START:
			printf("%s%sProvisioning started\n", TAG_OXI, BLE_PROV_TAG);
			break;
		case WIFI_PROV_CRED_RECV: {
			wifi_sta_config_t *wifi_cfg = (wifi_sta_config_t *)event_data;
			printf("%s%sReceived Wi-Fi credentials:SSID: %s, PASS: %s\n",
					TAG_OXI, BLE_PROV_TAG, (char *)wifi_cfg->ssid,
					(char *)wifi_cfg->password);
        	break;
		}
		case WIFI_PROV_CRED_FAIL: {
			wifi_prov_sta_fail_reason_t *reason = event_data;
			char *reason_msg;
			if (*reason == WIFI_PROV_STA_AUTH_ERROR) {
				reason_msg = "Authentication failed";
				esp_restart();
			} else {
				reason_msg = "AP not found";
			}
			printf("%s%s%s", TAG_OXI, BLE_PROV_TAG, reason_msg);
			// provisioning НЕ завершается автоматически
			break;
		}
		case WIFI_PROV_CRED_SUCCESS:
			printf("%s%sWi-Fi connected successfully!\n", TAG_OXI, BLE_PROV_TAG);
			break;
		case WIFI_PROV_END:
			printf("%s%sProvisioning finished\n", TAG_OXI, BLE_PROV_TAG);
			wifi_prov_mgr_deinit();
			esp_restart();
			break;
		default:
			break;
	}
}

void register_ble_ev_hndl(void)
{
	oxi_err_check("Provision event register",
		esp_event_handler_register(
			WIFI_PROV_EVENT,
			ESP_EVENT_ANY_ID,
			&prov_event_handler,
			NULL
		)
	);
}

void start_prov(void* pvParameter)
{
    bool provisioned = false;
	oxi_err_check("Is provisioned", wifi_prov_mgr_is_provisioned(&provisioned));
	if (provisioned) {
		oxi_err_check("WiFi disconnect", esp_wifi_disconnect());
		oxi_err_check("WiFi restore", esp_wifi_restore());
	}
	wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BLE
        };
    oxi_err_check("BLE_PROV:Provision manager init",
                                                wifi_prov_mgr_init(config));
    oxi_err_check("BLE_PROV:Reset BLE provision",
                                        wifi_prov_mgr_reset_provisioning());
	/*esp_log_level_set("*", ESP_LOG_DEBUG);
	esp_log_level_set("protocomm_nimble", ESP_LOG_DEBUG);
	esp_log_level_set("NimBLE", ESP_LOG_DEBUG);
	esp_log_level_set("wifi_prov_mgr", ESP_LOG_DEBUG);*/
    oxi_err_check("BLE_PROV:Provision manager start",
        wifi_prov_mgr_start_provisioning(
            WIFI_PROV_SECURITY_0, // без шифрования, для простоты
            NULL,                 // PoP
            "OXI_DEVICE",     // имя BLE устройства
            NULL
        ));
	vTaskDelay(pdMS_TO_TICKS(50));
    vTaskDelete(NULL);
}
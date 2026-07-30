#include "nvs.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "hal/efuse_hal.h"
#include "oxiXesp.h"

#define HOST_NAME "rc.oxi.ua"
#define PORT_HTTP 8080
#define PROVISION_PATH "/api/v1/provision"
#define POST_REQUEST "{\"deviceName\":\"OXI%02x%02x%02x\",\
\"provisionDeviceKey\":\"%s\",\"provisionDeviceSecret\":\"%s\"}"
#define PROVISION_KEY "bkot04ca4b3b1xhhhilk"
#define PROVISION_SECRET "srsn3w2dnr1kf8edeoi7"

ESP_EVENT_DEFINE_BASE(PROVISION_EVENT); // event base for OXI provisioning event
/**
 * @brief Enum for OXI provisioning events
 */
typedef enum {
	// event posted when access token is got from OXI cloud
	PROVISION_EVENT_GOT_ACCESS_TOKEN
} ProvisionEventId;


char access_token[33];
char uid[10];
// this pointer need for unregistering event handler for OXI provisioning events
// after use
esp_event_handler_instance_t provision_handler_instance = NULL;

/**
 * @brief Event handler for OXI provisioning event
 * 
 * Unregister this event handler after use, because this event will be posted
 * only once, save gotten access token to Non Volitale Storage and start MQTT
 * clietn if access token successfully saved to NVS
 * 
 * @param args not used
 * @param base have to be PROVISION_EVENT
 * @param id not used, because only one event in provision event enum
 * @param event_data not used
 */
static void provision_event_handler(void *args, esp_event_base_t base,
												int32_t id, void *event_data)
{
	nvs_handle_t access_nvs = 0;
	esp_err_t er;

	esp_event_handler_instance_unregister(PROVISION_EVENT,
								ESP_EVENT_ANY_ID, &provision_handler_instance);
	if ((er = nvs_open("access", NVS_READWRITE, &access_nvs)) != ESP_OK) {
		printf("Error (%s) opening \"access\" NVS!\n", esp_err_to_name(er));
		return;
	}
	do {
		if ((er = nvs_set_str(access_nvs, "access_token", access_token)))
			break;
		if ((er = nvs_commit(access_nvs)))
			break;
	} while (false);
	nvs_close(access_nvs);
	if (er) {
		printf("Error (%s) saving access token!\n", esp_err_to_name(er));
	} else {
		printf("Access token - %s\n", access_token);
		mqtt_start();
	}
}

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
	esp_err_t er;

	switch (evt->event_id) {
	case HTTP_EVENT_ERROR:
		printf("%sHTTP_EVENT_ERROR\n", TAG_OXI);
		break;
	case HTTP_EVENT_ON_CONNECTED:
		printf("%sHTTP_EVENT_ON_CONNECTED\n", TAG_OXI);
		break;
	case HTTP_EVENT_HEADER_SENT:
		printf("%sHTTP_EVENT_HEADER_SENT\n", TAG_OXI);
		break;
	case HTTP_EVENT_ON_HEADER:
		printf("%sHTTP_EVENT_ON_HEADER\n", TAG_OXI);
		break;
	case HTTP_EVENT_ON_DATA:
	// parse this string as JSON, get OXI access token from JSON and post OXI
	//provisioning event to save access token to NVS after finish HTTP request
	//handle
		do {
			cJSON *root = cJSON_ParseWithLength(evt->data, evt->data_len);
			if (root == NULL) {
				printf("Error parsing credentials HTTP data\n");
				er = ESP_FAIL;
				break;
			}
			cJSON *credentials =
					cJSON_GetObjectItemCaseSensitive(root, "credentialsValue");
			if (!cJSON_IsString(credentials) ||
			(credentials->valuestring == NULL)) {
				cJSON_Delete(root);
				printf("Error: Invalid credentials format\n");
				er = ESP_FAIL;
				break;
			}
			if (strlen(credentials->valuestring) > sizeof(access_token) - 1) {
				cJSON_Delete(root);
				printf("Error: Credentials value too long\n");
				er = ESP_FAIL;
				break;
			}
			strcpy(access_token, credentials->valuestring);
			cJSON_Delete(root);
			if ((er = esp_event_handler_instance_register(PROVISION_EVENT,
			ESP_EVENT_ANY_ID, &provision_event_handler, NULL,
			&provision_handler_instance))) {
				printf("Error (%s) registering provision event handler!\n",
														esp_err_to_name(er));
				break;
			}
			if ((er = esp_event_post(PROVISION_EVENT,
			PROVISION_EVENT_GOT_ACCESS_TOKEN, NULL, 0, portMAX_DELAY))) {
				printf("Error (%s) posting provision event!\n",
														esp_err_to_name(er));
				esp_event_handler_instance_unregister(PROVISION_EVENT,
								ESP_EVENT_ANY_ID, &provision_handler_instance);
			}
		} while (false);
		if (er)
			printf("%s%sProvisioning failed\n", TAG_OXI, OXI_ERR);
		break;
	case HTTP_EVENT_ON_FINISH:
		printf("%sHTTP_EVENT_ON_FINISH\n", TAG_OXI);
		break;
	case HTTP_EVENT_DISCONNECTED:
		printf("%sHTTP_EVENT_DISCONNECTED\n", TAG_OXI);
		break;
	default:
		break;
	}
	return ESP_OK;
}

static void provisioning(void)
{
	esp_err_t er;
	char *post_data = NULL;
	uint8_t mac[6];

	/* Perform provisioning and get access token */
	esp_http_client_config_t config = {
		.host = HOST_NAME,
		.port = PORT_HTTP,
		.path = PROVISION_PATH,
		.transport_type = HTTP_TRANSPORT_OVER_TCP,
		.method = HTTP_METHOD_POST,
		.event_handler = _http_event_handler
	};
	esp_http_client_handle_t client = esp_http_client_init(&config);
	if (client == NULL) { //HTTP client not init
		printf("%s%sHTTP client init\n", TAG_OXI, OXI_ERR);
		return;
	}
	do {
		if ((er = esp_http_client_set_header(client, "Content-Type",
											"application/json")) != ESP_OK) {
			oxi_err_check("HTTP client set header",	er);
			break;
		}
		post_data = malloc(strlen(POST_REQUEST) + 6 + strlen(PROVISION_KEY) +
													strlen(PROVISION_SECRET));
		if (post_data == NULL) { //not enough memory for post request
			printf("%s%sHTTP client not memory for POST request", TAG_OXI,
																	OXI_ERR);
			break;
		}
		efuse_hal_get_mac(mac);
		sprintf(uid, "OXI%02x%02x%02x", mac[0], mac[1], mac[2]);
		sprintf(post_data, POST_REQUEST, mac[0], mac[1], mac[2], PROVISION_KEY,
															PROVISION_SECRET);
		//printf("%sDebug:Token request:%s\n", TAG_OXI, post_data);
		esp_http_client_set_post_field(client, post_data, strlen(post_data));
		if ((er = esp_http_client_perform(client)) != ESP_OK) {
			oxi_err_check("HTTP client not perform", er);
			break;
		}
		if (esp_http_client_get_status_code(client) != HttpStatus_Ok)
			printf("%s%sHTTP responce not OK\n", TAG_OXI, OXI_ERR);
	} while (false);
	if (post_data != NULL)
		free(post_data);
	oxi_err_check("HTTP client cleanup", esp_http_client_cleanup(client));
}

void get_access_token(void)
{
	nvs_handle_t access_nvs = 0;
	size_t sl;
	esp_err_t er;
	uint8_t mac[6];

	/* Read saved access_token */
	do {
		if ((er = nvs_open("access", NVS_READWRITE, &access_nvs)) != ESP_OK) {
			oxi_err_check("NVS not open",er);
			break;
		}
		if ((er = nvs_get_str(access_nvs, "access_token", NULL, &sl))) {
			oxi_err_check("Access token key not found", er);
			break;
		}
		if ((er = nvs_get_str(access_nvs, "access_token", access_token, &sl)))
			oxi_err_check("Access token not read", er);
	} while(0);
	nvs_close(access_nvs);
	if (er) {
		provisioning();
	} else {
		printf("%sAccess token - %s\n", TAG_OXI, access_token);
		efuse_hal_get_mac(mac);
		sprintf(uid, "OXI%02x%02x%02x", mac[0], mac[1], mac[2]);
		printf("%sDebug:UID - %s\n", TAG_OXI, uid);
		mqtt_start();
	}
}
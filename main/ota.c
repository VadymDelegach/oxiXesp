#include "cJSON.h"
#include "oxiXesp.h"

#define FW_REQUEST "http://rc.oxi.ua:8080/api/v1/%s/attributes\
?sharedKeys=%s_checksum,%s_checksum_algorithm,%s_size,%s_title,%s_version"
#define BIN_REQUEST "http://rc.oxi.ua:8080/api/v1/%s/%sware?title=%s&version=%s"
#define CONST_64K	0x10000
/* OTA alarms list */
#define NOT_MEM_URL	1
#define HTTP_NOT_INIT 2
#define HTTP_NOT_OPEN_INFO 3
#define HTTP_NO_INFO 4
#define HTTP_NOT_CLOSE_INFO_REDIRECT 5
#define SET_REDIRECT_INFO_URL_ERROR 6
#define HTTP_NOT_OPEN_INFO_REDIRECT	7
#define HTTP_ERROR_STATUS_INFO 8
#define HTTP_NO_INFO_REDIRECT 9
#define NOT_MEM_INFO 10
#define HTTP_READ_INFO_ERROR 11
#define HTTP_NOT_CLOSE_INFO	12
#define INFO_JSON_ERROR	13
#define NOT_REALLOC_URL	15
#define SET_DATA_URL_ERROR 16
#define SMALL_CHUNK	24
#define NOT_REALLOC_DATA 25
#define HTTP_READ_DATA_ERROR 28
#define WRONG_OTA_TYPE 33
#define SWD_NOT_INIT 34
#define STM_OLD_PROG_SIZE_WRONG 35
#define NOT_UPGRADE_PARTITION 36
#define STM_OLD_PROG_NOT_SAVED 37
#define STM_NEW_PROG_NOT_SAVED 38
#define STM_NOT_WRITE 39

extern const uint8_t _binary_rc_oxi_cert_pem_start[];
extern char access_token[];

TaskHandle_t ota_task_handle = NULL;
char location[1024] = {0};
bool new_location = false;

static void upgrade_exit(upgrade_info *upinf)
{
	if (upinf->swdinf.libswdctx)
		oxi_err_check("LibSWD deinit",
								libswd_deinit_ctx(upinf->swdinf.libswdctx));
	if (upinf->swdinf.deviceSWD)
		oxi_err_check("SWD SPI remove",
							spi_bus_remove_device(upinf->swdinf.deviceSWD));
	if (upinf->swdinf.spi_bus_init)
		oxi_err_check("SWD SPI BUS FREE", spi_bus_free(SPI2_HOST));
	if (upinf->data)
		free(upinf->data);
	if (upinf->client) {
		oxi_err_check("OTA HTTP client close",
										esp_http_client_close(upinf->client));
		oxi_err_check("OTA HTTP client cleanup",
										esp_http_client_cleanup(upinf->client));
	}
	if (upinf->getreq)
		free(upinf->getreq);
	ota_task_handle = NULL;
	vTaskDelete(NULL);
}

static esp_err_t http_ota_handler(esp_http_client_event_t *evt)
{
	if (evt->event_id == HTTP_EVENT_ON_HEADER) {
		/*printf("%sHTTP_EVENT_ON_HEADER\n", TAG_OXI);
		printf("%sHeader key:%s\n", TAG_OXI, evt->header_key);
		printf("%sHeader value:%s\n", TAG_OXI, evt->header_value);*/
		if (strcasecmp(evt->header_key, "location") == 0) {
			strncpy(location, evt->header_value, 1024);
			location[1023] = 0;
			new_location = true;
		}
	}
	return ESP_OK;
}

static bool http_server_connect(upgrade_info *upinf)
{
	/* Send HTTP request GET for info about firmware */
	if (esp_http_client_open(upinf->client, 0) != ESP_OK) {
		ota_err_msg(HTTP_NOT_OPEN_INFO);
		return false;
	}
	/* Read all headers of the responce and data length */
	upinf->data_len = esp_http_client_fetch_headers(upinf->client);
	if (upinf->data_len < 0) {
		ota_err_msg(HTTP_NO_INFO);
		return false;
	}
	/* Check status code in responce for detection redirect */
	int stcode = esp_http_client_get_status_code(upinf->client);
	while (stcode > 300 && stcode < 400) {
		/* If status code corresponds to redirect then HTTP event handler should
	 	* have detected location header and save its value*/
		while (!new_location);
		new_location = false;
		if (esp_http_client_close(upinf->client) != ESP_OK) {
			ota_err_msg(HTTP_NOT_CLOSE_INFO_REDIRECT);
			return false;
		}
		/* Set new URL saved from location header */
		if (esp_http_client_set_url(upinf->client, location) != ESP_OK) {
			ota_err_msg(SET_REDIRECT_INFO_URL_ERROR);
			return false;
		}
		/* Send new HTTP request GET for firmware info */
		if (esp_http_client_open(upinf->client, 0) != ESP_OK) {
			ota_err_msg(HTTP_NOT_OPEN_INFO_REDIRECT);
			return false;
		}
		/* Read all headers of the responce and data length */
		upinf->data_len = esp_http_client_fetch_headers(upinf->client);
		if (upinf->data_len < 0) {
			ota_err_msg(HTTP_NO_INFO_REDIRECT);
			return false;
		}	
	}
	/* Error status code received */
	if (stcode >= 400) {
		ota_err_msg(HTTP_ERROR_STATUS_INFO);
		return false;
	}
	/* Firmware info data not received */
	if (upinf->data_len <= 0) {
		ota_err_msg(HTTP_NO_INFO_REDIRECT);
		return false;
	}
	return true;
}

static bool parse_fw_info(upgrade_info *upinf)
{
	cJSON* fwinfo = cJSON_ParseWithLength(upinf->data, upinf->data_len);
	if (fwinfo == NULL) {
		printf("Error parsing OTA data as JSON\n");
		cJSON_Delete(fwinfo);
		return false;
	}
	cJSON* shared = cJSON_GetObjectItem(fwinfo, "shared");
	if (shared == NULL) {
		printf("Error parsing OTA data: no \"shared\" object\n");
		cJSON_Delete(fwinfo);
		return false;
	}
	cJSON* title = cJSON_GetObjectItem(shared, "fw_title");
	if (title == NULL || !cJSON_IsString(title)) {
		printf("Error parsing OTA data: no \"fw_title\" field\n");
		cJSON_Delete(fwinfo);
		return false;
	}
	strncpy(upinf->title, title->valuestring, TITLE_LEN);
	upinf->title[TITLE_LEN - 1] = 0;
	cJSON* vers = cJSON_GetObjectItem(shared, "fw_version");
	if (vers == NULL || !cJSON_IsString(vers)) {
		printf("Error parsing OTA data: no \"fw_version\" field\n");
		cJSON_Delete(fwinfo);
		return false;
	}
	strncpy(upinf->vers, vers->valuestring, VERSION_LEN);
	upinf->vers[VERSION_LEN - 1] = 0;
	cJSON* size = cJSON_GetObjectItem(shared, "fw_size");
	if (size == NULL || !cJSON_IsNumber(size)) {
		printf("Error parsing OTA data: no \"fw_size\" field\n");
		cJSON_Delete(fwinfo);
		return false;
	}
	upinf->size = cJSON_GetNumberValue(size);
	cJSON_Delete(fwinfo);
	return true;
}

static bool get_upgrade_info(upgrade_info *upinf)
{
	char *cp = upinf->type;

	upinf->getreq = malloc(strlen(FW_REQUEST) +	strlen(access_token) +
														strlen(cp) * 5 + 1);
	if (upinf->getreq == NULL) {
		ota_err_msg(NOT_MEM_URL);
		return false;
	}
	sprintf(upinf->getreq, FW_REQUEST, access_token, cp, cp, cp, cp, cp);
	esp_http_client_config_t config = {
		.url = upinf->getreq,
		.buffer_size = 1024,
		.transport_type = HTTP_TRANSPORT_OVER_TCP,
		.event_handler = http_ota_handler,
		.cert_pem = (const char *)_binary_rc_oxi_cert_pem_start,
		.timeout_ms = 10000
	};
	upinf->client = esp_http_client_init(&config);
	if (upinf->client == NULL) {
		ota_err_msg(HTTP_NOT_INIT);
		return false;
	}
	if (!http_server_connect(upinf))
		return false;
	/* Read and save JSON object with firmware info */
	upinf->data = malloc(upinf->data_len + 1);
	if (upinf->data == NULL) {
		ota_err_msg(NOT_MEM_INFO);
		return false;
	}
	if (esp_http_client_read(upinf->client, upinf->data, upinf->data_len) < 0) {
		ota_err_msg(HTTP_READ_INFO_ERROR);
		return false;
	}
	upinf->data[upinf->data_len] = 0;
	/* Close connection for firmware info */
	if (esp_http_client_close(upinf->client) != ESP_OK) {
		ota_err_msg(HTTP_NOT_CLOSE_INFO);
		return false;
	}
	if (!parse_fw_info(upinf)) {
		ota_err_msg(INFO_JSON_ERROR);
		return false;
	}
	return true;
}

static bool get_upgrade_body(upgrade_info *upinf)
{
	char *cp = upinf->type;

	/* Get new firmware - begin */
	if (!strcmp(upinf->type, "fw")) {
		cp = "firm";
	} else if (!strcmp(upinf->type, "sw")) {
		cp = "soft";
	} else {
		ota_err_msg(WRONG_OTA_TYPE);
		return false;
	}
	upinf->getreq = realloc(upinf->getreq,
							strlen(BIN_REQUEST) + strlen(access_token) +
							strlen(cp) + strlen(upinf->title) +
							strlen(upinf->vers) + 1);
	if (upinf->getreq == NULL) {
		ota_err_msg(NOT_REALLOC_URL);
		return false;
	}
	sprintf(upinf->getreq, BIN_REQUEST, access_token, cp, upinf->title,
																upinf->vers);
	if (esp_http_client_set_url(upinf->client, upinf->getreq) != ESP_OK) {
		ota_err_msg(SET_DATA_URL_ERROR);
		return false;
	}
	if (!http_server_connect(upinf))
		return false;
	/* Get avalable free memory and truncate to power of two */
	upinf->chunk = esp_get_free_heap_size();
	if (upinf->chunk < 256) {
		ota_err_msg(SMALL_CHUNK);
		return false;
	}
	uint8_t i = 0;
	while (upinf->chunk < CONST_64K >> i)
		i++;
	upinf->chunk = CONST_64K >> i;
	/* Reallocate buffer for receive firmware to chunk size */
	upinf->data = realloc(upinf->data, upinf->chunk);
	if (upinf->data == NULL) {
		ota_err_msg(NOT_REALLOC_DATA);
		return false;
	}
	return true;
}

static int save_stm_new(upgrade_info *upinf, int offset)
{
	/* define erase range */
	size_t erase_range = (upinf->size / upinf->stm_part->erase_size) *
												upinf->stm_part->erase_size;
	if (erase_range < upinf->size)
		erase_range += upinf->stm_part->erase_size;
	/* erase ESP flash */
	if (esp_partition_erase_range(upinf->stm_part, offset, erase_range)) {
		//oxi_err_check("ESP erase", -1);
		return -1;
	}
	if (!get_upgrade_body(upinf)) //connect to server for get binary file
		upgrade_exit(upinf);
	/* Cycle of the receive new application and write to flash */
	int binsize = 0;
	int piece = STM_PAGE_SIZE < upinf->chunk ? STM_PAGE_SIZE : upinf->chunk;
	do {
		upinf->data_len = esp_http_client_read(upinf->client, upinf->data,
			piece);
		if (upinf->data_len < 0) {
			ota_err_msg(HTTP_READ_DATA_ERROR);
			upgrade_exit(upinf);
		}
		/*printf("%sSTM new program received %d bytes\n", TAG_OXI,
													binsize + upinf->data_len);*/
		if (esp_partition_write_raw(upinf->stm_part, offset + binsize,
			upinf->data, upinf->data_len) != ESP_OK) {
			//oxi_err_check("ESP save", -1);
			return -1;
		}
		binsize += upinf->data_len;
		printf("%s%sNew saved %d bytes\n", TAG_OXI, SWD_TAG, binsize);
	} while (upinf->data_len > 0 && binsize < upinf->size);
	return binsize;
}

static void upgrade_software(upgrade_info *upinf)
{
	if (!swd_init(&(upinf->swdinf))) {
		ota_err_msg(SWD_NOT_INIT);
		return;
	}
	/* determinate size of the current program for save */
	int stm_size = get_size_stm_prog_old(&(upinf->swdinf));
	if (stm_size < 0) {
		ota_err_msg(STM_OLD_PROG_SIZE_WRONG);
		return;
	}
	//printf("%sOld STM program less or equal %d bytes\n", TAG_OXI, stm_size);
	/* Find flash memory partition for save STM32 software and persistent data */
	upinf->stm_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
							ESP_PARTITION_SUBTYPE_DATA_UNDEFINED, "stm_upgr");
	if (upinf->stm_part == NULL) {
		ota_err_msg(NOT_UPGRADE_PARTITION);
		return;
	}
	printf("%s%sSave old\n", TAG_OXI, SWD_TAG);
	if (save_stm_range(upinf, stm_size) < 0) {
		ota_err_msg(STM_OLD_PROG_NOT_SAVED);
		return;
	}
	printf("%s%sSave new\n", TAG_OXI, SWD_TAG);
	/* align offset to erase size */
	size_t align_offset = ((stm_size + STM_PAGE_SIZE * STM_END_PG) /
					upinf->stm_part->erase_size) * upinf->stm_part->erase_size;
	if (align_offset < stm_size + STM_PAGE_SIZE * STM_END_PG)
		align_offset += upinf->stm_part->erase_size;
	if (save_stm_new(upinf, align_offset) < 0) {
		ota_err_msg(STM_NEW_PROG_NOT_SAVED);
		return;
	}
	printf("%s%sWrite new\n", TAG_OXI, SWD_TAG);
	if (write_stm(upinf, BASE_STM_FLASH, align_offset, upinf->size) < 0) {
		ota_err_msg(STM_NOT_WRITE);
		return;
	}
	/* determinate size of the new program for save */
	if ((stm_size = get_size_stm_prog_old(&(upinf->swdinf))) < 0) {
		ota_err_msg(STM_OLD_PROG_SIZE_WRONG);
		return;
	}
	printf("%s%sSave old\n", TAG_OXI, SWD_TAG);
	if (save_stm_range(upinf, stm_size) < 0) {
		ota_err_msg(STM_OLD_PROG_NOT_SAVED);
		return;
	}
	restartSTM(upinf);
}

void ota_task(void *pvParameter)
{
	upgrade_info upgrinf = {0};

	upgrinf.type = (char *)pvParameter;
	if (!get_upgrade_info(&upgrinf)) //can't get info about uprade
		upgrade_exit(&upgrinf);
	if (!strcmp(upgrinf.type, "sw"))
		upgrade_software(&upgrinf);
	upgrade_exit(&upgrinf);
}
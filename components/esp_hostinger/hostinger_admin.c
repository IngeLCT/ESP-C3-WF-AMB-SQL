#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "hostinger_ingest.h"
#include "Privado.h"

static const char* TAGA = "HOST_ADMIN";

static esp_err_t http_evt(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0) {
                // imprime hasta 256 bytes del body
                int n = evt->data_len > 256 ? 256 : evt->data_len;
                char buf[260]; memcpy(buf, evt->data, n); buf[n] = 0;
                ESP_LOGW("HTTP_BODY", "%s", buf);
            }
            break;
        default: break;
    }
    return ESP_OK;
}

static int post_json(const char* url, const char* json_body) {
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .disable_auto_redirect = true,
        .user_agent = "curl/8.6.0",
        .event_handler = http_evt, 
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return -2;
    esp_http_client_set_method(cli, HTTP_METHOD_POST);
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    esp_http_client_set_header(cli, "X-API-Key", HOSTINGER_API_KEY);
    esp_http_client_set_header(cli, "Connection", "close");
    esp_http_client_set_post_field(cli, json_body, strlen(json_body));

    esp_err_t err = esp_http_client_perform(cli);
    int status = -1;
    if (err == ESP_OK) status = esp_http_client_get_status_code(cli);
    else ESP_LOGE(TAGA, "HTTP error: %s", esp_err_to_name(err));

    esp_http_client_cleanup(cli);
    if (err != ESP_OK) return (int)err;
    if (status < 200 || status >= 300) return -100 - status;
    return 0;
}

int hostinger_delete_all_for_device(const char* device_id) {
    const char* url = "https://ambiente.sensorwatts-energia.com/api/admin.php";
    char body[256];
    snprintf(body, sizeof(body), "{\"op\":\"delete_all\",\"device_id\":\"%s\"}", device_id ? device_id : DEVICE_ID);
    int rc = post_json(url, body);
    ESP_LOGI(TAGA, "DELETE_ALL(%s) => %d", device_id ? device_id : DEVICE_ID, rc);
    return rc;
}

int hostinger_trim_oldest_batch(const char* device_id, int batch_size) {
    const char* url = "https://ambiente.sensorwatts-energia.com/api/admin.php";
    char body[256];
    snprintf(body, sizeof(body), "{\"op\":\"trim_oldest\",\"device_id\":\"%s\",\"batch_size\":%d}",
             device_id ? device_id : DEVICE_ID, batch_size > 0 ? batch_size : 50);
    int rc = post_json(url, body);
    ESP_LOGI(TAGA, "TRIM_OLDEST(%s,%d) => %d", device_id ? device_id : DEVICE_ID, batch_size, rc);
    return rc;
}

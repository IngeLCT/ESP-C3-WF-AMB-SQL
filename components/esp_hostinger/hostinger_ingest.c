#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "hostinger_ingest.h"
#include "Privado.h"

static const char* TAG = "HOST_ING";

// Si el JSON no trae "device_id", lo inyectamos.
static char* ensure_device_id(const char* body_in) {
    if (!body_in) body_in = "{}";
    size_t n = strlen(body_in);
    if (strstr(body_in, "\"device_id\"")) { // ya lo trae
        char* out = (char*)malloc(n + 1);
        if (!out) return NULL;
        memcpy(out, body_in, n + 1);
        return out;
    }
    if (n > 1 && body_in[0] == '{') {
        const char* rest = body_in + 1;
        size_t extra = strlen(DEVICE_ID) + 32;
        char* out = (char*)malloc(n + extra);
        if (!out) return NULL;
        int w = snprintf(out, n + extra, "{\"device_id\":\"%s\",%s", DEVICE_ID, rest);
        if (w < 0) { free(out); return NULL; }
        return out;
    }
    size_t rawlen = strlen(body_in);
    size_t extra = strlen(DEVICE_ID) + rawlen + 64;
    char* out = (char*)malloc(extra);
    if (!out) return NULL;
    int w = snprintf(out, extra, "{\"device_id\":\"%s\",\"raw\":\"%s\"}", DEVICE_ID, body_in);
    if (w < 0) { free(out); return NULL; }
    return out;
}

static int do_post_json(const char* url, const char* json_body) {
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return -2;

    esp_http_client_set_method(cli, HTTP_METHOD_POST);
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    esp_http_client_set_header(cli, "X-API-Key", HOSTINGER_API_KEY);
    esp_http_client_set_post_field(cli, json_body, strlen(json_body));

    esp_err_t err = esp_http_client_perform(cli);
    int status = -1;
    if (err == ESP_OK) status = esp_http_client_get_status_code(cli);
    else ESP_LOGE(TAG, "HTTP error: %s", esp_err_to_name(err));

    esp_http_client_cleanup(cli);
    if (err != ESP_OK) return (int)err;
    if (status < 200 || status >= 300) return -100 - status;
    return 0;
}

int hostinger_ingest_post(const char* json_utf8) {
    char* body = ensure_device_id(json_utf8);
    if (!body) return -1;
    int rc = do_post_json(HOSTINGER_API_URL, body);
    free(body);
    ESP_LOGI(TAG, "INGEST => %d", rc);
    return rc;
}

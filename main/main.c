// Core
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <string.h>

// ESP-IDF
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

// IMPORTANTE: headers para eventos y Wi-Fi
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

// Project
#include "sensors.h"
#include "hostinger_ingest.h"
#include "Privado.h"
#include "captive_manager.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include "wifi_store.h"

void geoapify_fetch_once_wifi_unwired(void);

#define SENSOR_TASK_STACK 10240
#define ENABLE_HTTP_VERBOSE 1
#define LOG_EACH_SAMPLE 1
static inline int64_t minutes_to_us(int m) { return (int64_t)m * 60 * 1000000; }

static const char *TAG = "ESP-WF";

#define WIFI_RETRY_DELAY_MS        2000   // espera entre reintentos
#define WIFI_RECONNECT_WINDOW_MS   30000  // ventana de reconexión (30 s)
#define WIFI_BACKOFF_IDLE_MS       10000  // si no reconecta, descansar 10 s

// Devuelve true si el STA está asociado a un AP.
static bool wifi_is_connected(void) {
    wifi_ap_record_t ap;
    return (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
}

// Intenta reconectar durante max_wait_ms. Devuelve true si quedó conectado.
static bool wifi_reconnect_blocking(uint32_t max_wait_ms) {
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(max_wait_ms);
    while (!wifi_is_connected()) {
        ESP_LOGW(TAG, "WiFi caído: intentando esp_wifi_connect()...");
        esp_err_t err = esp_wifi_connect(); // idempotente si ya está conectando
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_connect() => %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS)); // cede CPU / WDT friendly
        if (xTaskGetTickCount() > deadline) break;
    }
    bool ok = wifi_is_connected();
    ESP_LOGI(TAG, "%s", ok ? "WiFi restaurado" : "Sin WiFi tras ventana de reconexion");
    return ok;
}

char g_ubicacion[64] = {0};
const char *app_get_ubicacion(void) { return g_ubicacion; }

static void app_init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static void app_cargar_ubicacion(void)
{
    g_ubicacion[0] = '\0';
    esp_err_t err = wifi_store_get_location(g_ubicacion, sizeof(g_ubicacion));
    if (err == ESP_OK) {
        sensors_set_city_state(g_ubicacion);
        ESP_LOGI(TAG, "Ubicación guardada: '%s'", g_ubicacion); // puede venir vacía si nunca se guardó
    } else {
        ESP_LOGW(TAG, "No se pudo leer la ubicación (err=0x%x)", err);
    }
}

static void init_sntp_and_time(void) {
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_init();
    setenv("TZ", "UTC6", 1);
    tzset();
    for (int i = 0; i < 100; ++i) {
        time_t now = 0;
        time(&now);
        if (now > 1609459200) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ------------ SENSOR TASK ------------
void sensor_task(void *pv) {
    SensorData data;

    time_t start_epoch;
    struct tm start_tm_info;
    char inicio_str[20];
    time(&start_epoch);
    localtime_r(&start_epoch, &start_tm_info);
    strftime(inicio_str, sizeof(inicio_str), "%H:%M:%S", &start_tm_info);

    bool first_send = true;

    //geoapify_fetch_once_wifi_unwired();
    app_init_nvs();         // 1) NVS listo antes de usar wifi_store_*
    app_cargar_ubicacion(); // 2) Leer y dejar en g_ubicacion

    if (firebase_init() != 0) {
        ESP_LOGE(TAG, "Error inicializando Firebase");
        vTaskDelete(NULL);
        return;
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    hostinger_delete_all_for_device(DEVICE_ID);

    // 1 muestra/minuto, envío cada 5 min
    const int SAMPLE_EVERY_MIN = 1;
    const int SAMPLES_PER_BATCH = 5;
    const TickType_t SAMPLE_DELAY_TICKS = pdMS_TO_TICKS(SAMPLE_EVERY_MIN * 60000);
    int sample_count = 0;

    double sum_pm1p0=0, sum_pm2p5=0, sum_pm4p0=0, sum_pm10p0=0, sum_voc=0, sum_nox=0, sum_avg_temp=0, sum_avg_hum=0;
    uint32_t sum_co2 = 0;
    char last_fecha_str[20] = "";

    const int64_t REFRESH_US = minutes_to_us(50);
    int64_t next_refresh_us = esp_timer_get_time() + REFRESH_US;

    bool refresh_overdue = false; // hubo un refresh vencido mientras no había Wi-Fi

    while (1) {
        // === (A) Gestion de refresh de token ligada a la conectividad ===
        int64_t now_us = esp_timer_get_time();
        bool refresh_due = (now_us >= next_refresh_us);

        if (refresh_due) {
            if (wifi_is_connected()) {
                ESP_LOGI(TAG, "Refrescando token (50m)...");
                int r = 0 /*hostinger: no token*/;
                if (r == 0) {
                    ESP_LOGI(TAG, "Token refresh OK");
                    next_refresh_us = esp_timer_get_time() + REFRESH_US; // reprograma desde ahora
                    refresh_overdue = false;
                } else {
                    ESP_LOGW(TAG, "Fallo refresh token (%d). Reintento cuando haya red.", r);
                    // no movemos el next_refresh_us para reintentar pronto
                }
            } else {
                // no hay Wi-Fi: marca pendiente
                refresh_overdue = true;
            }
        }

        // === (B) Guard de Wi-Fi para sensado/envío ===
        if (!wifi_is_connected()) {
            ESP_LOGW(TAG, "No hay WiFi -> pauso medición/envío y reconecto");
            bool ok = wifi_reconnect_blocking(WIFI_RECONNECT_WINDOW_MS);
            if (!ok) {
                vTaskDelay(pdMS_TO_TICKS(WIFI_BACKOFF_IDLE_MS));
                continue; // NO leer, NO acumular, NO enviar
            }
            // Recién recuperado Wi-Fi: si había refresh pendiente o ya venció, hazlo AHORA
            now_us = esp_timer_get_time();
            if (refresh_overdue || now_us >= next_refresh_us) {
                ESP_LOGI(TAG, "Red restaurada: refrescando token pendiente...");
                int r = 0 /*hostinger: no token*/;
                if (r == 0) {
                    ESP_LOGI(TAG, "Token refresh OK tras reconexión");
                    next_refresh_us = esp_timer_get_time() + REFRESH_US;
                    refresh_overdue = false;
                } else {
                    ESP_LOGW(TAG, "Fallo refresh tras reconexión (%d). Continuo y reintento luego.", r);
                }
            }
        }

        // === (C) Desde aquí solo con Wi-Fi OK ===
        if (sensors_read(&data) == ESP_OK) {
            sample_count++;
            sum_pm1p0 += data.pm1p0;
            sum_pm2p5 += data.pm2p5;
            sum_pm4p0 += data.pm4p0;
            sum_pm10p0 += data.pm10p0;
            sum_voc += data.voc;
            sum_nox += data.nox;
            sum_avg_temp += data.avg_temp;
            sum_avg_hum += data.avg_hum;
            sum_co2 += data.co2;
    #if LOG_EACH_SAMPLE
            ESP_LOGI(TAG,
                "Muestra %d/%d: PM1.0=%.2f PM2.5=%.2f PM4.0=%.2f PM10=%.2f VOC=%.1f NOx=%.1f CO2=%u Temp=%.2fC Hum=%.2f%%",
                sample_count, SAMPLES_PER_BATCH, data.pm1p0, data.pm2p5, data.pm4p0, data.pm10p0,
                data.voc, data.nox, data.co2, data.avg_temp, data.avg_hum);
    #endif
        } else {
            ESP_LOGW(TAG, "Error leyendo sensores (batch %d)", sample_count);
        }

        if (sample_count >= SAMPLES_PER_BATCH) {
            time_t now_epoch;
            struct tm tm_info;
            time(&now_epoch);
            localtime_r(&now_epoch, &tm_info);
            char hora_envio[16];
            strftime(hora_envio, sizeof(hora_envio), "%H:%M:%S", &tm_info);
            char fecha_actual[20];
            strftime(fecha_actual, sizeof(fecha_actual), "%d-%m-%Y", &tm_info);

            SensorData avg = (SensorData){0};
            double denom = (double)sample_count;
            avg.pm1p0   = (float)(sum_pm1p0 / denom);
            avg.pm2p5   = (float)(sum_pm2p5 / denom);
            avg.pm4p0   = (float)(sum_pm4p0 / denom);
            avg.pm10p0  = (float)(sum_pm10p0 / denom);
            avg.voc     = (float)(sum_voc / denom);
            avg.nox     = (float)(sum_nox / denom);
            avg.avg_temp= (float)(sum_avg_temp / denom);
            avg.avg_hum = (float)(sum_avg_hum / denom);
            avg.co2     = (uint16_t)(sum_co2 / sample_count);
            avg.scd_temp= avg.avg_temp;
            avg.scd_hum = avg.avg_hum;
            avg.sen_temp= avg.avg_temp;
            avg.sen_hum = avg.avg_hum;

            char json[384];
            if (first_send) {
                sensors_format_json(&avg, hora_envio, fecha_actual, inicio_str, json, sizeof(json));
                strncpy(last_fecha_str, fecha_actual, sizeof(last_fecha_str)-1);
                last_fecha_str[sizeof(last_fecha_str)-1] = '\0';
                first_send = false;
            } else {
                if (strncmp(last_fecha_str, fecha_actual, sizeof(last_fecha_str)) != 0) {
                    snprintf(json, sizeof(json),
                        "{\"pm1p0\":%.2f,\"pm2p5\":%.2f,\"pm4p0\":%.2f,\"pm10p0\":%.2f,"
                        "\"voc\":%.1f,\"nox\":%.1f,\"cTe\":%.2f,\"cHu\":%.2f,\"co2\":%u,"
                        "\"fecha\":\"%s\",\"hora\":\"%s\"}",
                        avg.pm1p0, avg.pm2p5, avg.pm4p0, avg.pm10p0,
                        avg.voc, avg.nox, avg.avg_temp, avg.avg_hum,
                        avg.co2, fecha_actual, hora_envio);
                    strncpy(last_fecha_str, fecha_actual, sizeof(last_fecha_str)-1);
                    last_fecha_str[sizeof(last_fecha_str)-1] = '\0';
                } else {
                    snprintf(json, sizeof(json),
                        "{\"pm1p0\":%.2f,\"pm2p5\":%.2f,\"pm4p0\":%.2f,\"pm10p0\":%.2f,"
                        "\"voc\":%.1f,\"nox\":%.1f,\"cTe\":%.2f,\"cHu\":%.2f,\"co2\":%u,"
                        "\"hora\":\"%s\"}",
                        avg.pm1p0, avg.pm2p5, avg.pm4p0, avg.pm10p0,
                        avg.voc, avg.nox, avg.avg_temp, avg.avg_hum,
                        avg.co2, hora_envio);
                }
            }

            int batch_minutes = SAMPLES_PER_BATCH * SAMPLE_EVERY_MIN;
            ESP_LOGI(TAG, "JSON promedio %dm: %s", batch_minutes, json);

            char clave_min[20];
            strftime(clave_min, sizeof(clave_min), "%y-%m-%d_%H-%M-%S", &tm_info);
            char path_put[64];
            snprintf(path_put, sizeof(path_put), "/historial_mediciones/%s", clave_min);

            // === (D) Doble-check de Wi-Fi justo antes de enviar (evita perder el batch) ===
            if (!wifi_is_connected()) {
                bool ok = wifi_reconnect_blocking(WIFI_RECONNECT_WINDOW_MS);
                if (!ok) {
                    ESP_LOGW(TAG, "Se perdió WiFi antes de enviar; mantengo batch en RAM (sample_count=%d)", sample_count);
                    vTaskDelay(pdMS_TO_TICKS(WIFI_BACKOFF_IDLE_MS));
                    continue; // NO enviar, NO resetear acumuladores
                }
                // Tras reconectar, si había refresh pendiente, hazlo
                now_us = esp_timer_get_time();
                if (refresh_overdue || now_us >= next_refresh_us) {
                    int r = 0 /*hostinger: no token*/;
                    if (r == 0) {
                        next_refresh_us = esp_timer_get_time() + REFRESH_US;
                        refresh_overdue = false;
                    }
                }
            }

            ESP_LOGI(TAG, "SQL ingest (len=%d)", (int)strlen(json));
            hostinger_ingest_post(json);

            // Retención aproximada (igual que tenías)
            const size_t MAX_BYTES = 10 * 1024 * 1024;
            static double   avg_size = 256.0;
            static uint32_t approx_count = 0;
            size_t item_len = strlen(json);
            avg_size = (avg_size * 0.9) + (0.1 * (double)item_len);
            approx_count++;
            uint32_t max_items  = (uint32_t)(MAX_BYTES / (avg_size > 1.0 ? avg_size : 1.0));
            uint32_t high_water = max_items + 50;
            if (approx_count > high_water) {
                int deleted = hostinger_trim_oldest_batch(DEVICE_ID, 50);
                if (deleted > 0) {
                    approx_count = (approx_count > (uint32_t)deleted) ? (approx_count - (uint32_t)deleted) : 0;
                    ESP_LOGI(TAG, "Retención: borrados %d antiguos. approx_count=%u max_items=%u avg=%.1fB",
                            deleted, approx_count, max_items, avg_size);
                }
            }

            // Reset acumuladores SOLO después de enviar
            sample_count = 0;
            sum_pm1p0=sum_pm2p5=sum_pm4p0=sum_pm10p0=sum_voc=sum_nox=sum_avg_temp=sum_avg_hum=0;
            sum_co2 = 0;
        }

        vTaskDelay(SAMPLE_DELAY_TICKS);
    }

}

// ---- Event handler (asegúrate de que coincide con la firma estándar) ----
static void wifi_event_handler(void *arg,
                               esp_event_base_t base,
                               int32_t id,
                               void *data) {
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        captive_manager_notify_sta_got_ip();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t*)data;
        captive_manager_notify_sta_disconnected(disc ? disc->reason : -1);
    }
}

// ----------- APP MAIN -----------
void app_main(void) {

#if ENABLE_HTTP_VERBOSE
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("tls", ESP_LOG_VERBOSE);
    esp_log_level_set("FirebaseApp", ESP_LOG_VERBOSE);
    esp_log_level_set("RTDB", ESP_LOG_VERBOSE);
#endif

    captive_manager_cfg_t cfg = {
        .ap_ssid = CONFIG_CAPTIVE_MANAGER_AP_SSID,
        .ap_pass = CONFIG_CAPTIVE_MANAGER_AP_PASS,
        .connectivity_url = CONFIG_CAPTIVE_MANAGER_CONNECTIVITY_URL,
        .check_interval_ms = CONFIG_CAPTIVE_MANAGER_CHECK_INTERVAL_MS,
        .verify_success_needed = CONFIG_CAPTIVE_MANAGER_VERIFY_SUCCESS_N,
        .max_scan_aps = CONFIG_CAPTIVE_MANAGER_MAX_SCAN_APS,
        .conn_max_attempts = CONFIG_CAPTIVE_MANAGER_CONN_MAX_ATTEMPTS,
        .conn_retry_delay_ms = CONFIG_CAPTIVE_MANAGER_CONN_RETRY_DELAY_MS,
        .startup_check_delay_ms = CONFIG_CAPTIVE_MANAGER_STARTUP_CHECK_DELAY_MS
    };

    ESP_ERROR_CHECK(captive_manager_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(captive_manager_start());

    bool operational_started = false;

    // Sensores y Firebase solo se inicializan cuando el portal ya no está activo
    while (true) {
        if (!operational_started && captive_manager_get_state() == CAP_STATE_OPERATIONAL) {
            operational_started = true;
            ESP_LOGI(TAG,"Red lista con internet. Iniciando SNTP, sensores y Firebase...");
            init_sntp_and_time();
            esp_err_t ret = sensors_init_all();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Fallo al inicializar sensores: %s", esp_err_to_name(ret));
            } else {
                xTaskCreate(sensor_task, "sensor_task", SENSOR_TASK_STACK, NULL, 5, NULL);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#include "sensors.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "privado.h" //

#define I2C_MASTER_SCL_IO 4
#define I2C_MASTER_SDA_IO 5
#define I2C_MASTER_FREQ_HZ 100000
#define I2C_PORT I2C_NUM_0

#define SCD4X_ADDR 0x62
#define SEN5X_ADDR 0x69

static const char *TAG_SENS = "SENSORS";
static char g_city_state[64] = "----";

// CRC8 (SEN55)
static uint8_t sen5x_crc8(const uint8_t *data, int len) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x31;
            else crc <<= 1;
        }
    }
    return crc;
}

// --- New I2C v2 bus/device handles ---
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_scd4x_dev = NULL;
static i2c_master_dev_handle_t s_sen5x_dev = NULL;

// SCD4x helpers (new API)
static esp_err_t scd4x_start_measurement(void) {
    uint8_t cmd[2] = {0x21, 0xB1};
    return i2c_master_transmit(s_scd4x_dev, cmd, sizeof(cmd), pdMS_TO_TICKS(1000));
}

static esp_err_t scd4x_read_measurement(uint16_t *co2, float *temperature, float *humidity) {
    uint8_t cmd[2] = {0xEC, 0x05};
    esp_err_t ret = i2c_master_transmit(s_scd4x_dev, cmd, sizeof(cmd), pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(1));
    uint8_t data[9];
    ret = i2c_master_receive(s_scd4x_dev, data, sizeof(data), pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) return ret;
    *co2 = (data[0] << 8) | data[1];
    uint16_t raw_temp = (data[3] << 8) | data[4];
    uint16_t raw_hum  = (data[6] << 8) | data[7];
    *temperature = -45 + 175 * ((float)raw_temp / 65535.0f);
    *humidity = 100.0f * ((float)raw_hum / 65535.0f);
    return ESP_OK;
}

// SEN5x helpers (new API)
static esp_err_t sen5x_device_reset(void) {
    uint8_t cmd[2] = {0xD3, 0x04};
    return i2c_master_transmit(s_sen5x_dev, cmd, sizeof(cmd), pdMS_TO_TICKS(1000));
}

static esp_err_t sen5x_start_measurement(void) {
    uint8_t cmd[2] = {0x00, 0x21};
    return i2c_master_transmit(s_sen5x_dev, cmd, sizeof(cmd), pdMS_TO_TICKS(1000));
}

static esp_err_t sen5x_read_data_ready(uint8_t *data_ready) {
    uint8_t cmd[2] = {0x02, 0x02};
    esp_err_t ret = i2c_master_transmit(s_sen5x_dev, cmd, sizeof(cmd), pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(20));
    uint8_t resp[3];
    ret = i2c_master_receive(s_sen5x_dev, resp, sizeof(resp), pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) return ret;
    if (sen5x_crc8(resp, 2) != resp[2]) return ESP_ERR_INVALID_CRC;
    *data_ready = resp[1];
    return ESP_OK;
}

static esp_err_t sen5x_read_measured_values(uint8_t *buf, int buflen) {
    uint8_t cmd[2] = {0x03, 0xC4};
    esp_err_t ret = i2c_master_transmit(s_sen5x_dev, cmd, sizeof(cmd), pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(20));
    return i2c_master_receive(s_sen5x_dev, buf, buflen, pdMS_TO_TICKS(1000));
}

static int sen5x_decode_measurement(const uint8_t *buf, float *pm1, float *pm25, float *pm4,
                                    float *pm10, float *rh, float *temp, float *voc_index, float *nox_index) {
    uint16_t values[8];
    for (int i = 0; i < 8; i++) {
        const uint8_t *data = &buf[i*3];
        if (sen5x_crc8(data, 2) != data[2]) return 0;
        values[i] = ((uint16_t)data[0] << 8) | data[1];
    }
    *pm1      = values[0] / 10.0f;
    *pm25     = values[1] / 10.0f;
    *pm4      = values[2] / 10.0f;
    *pm10     = values[3] / 10.0f;
    *rh       = values[4] / 100.0f;
    *temp     = values[5] / 200.0f;
    *voc_index = values[6] / 10.0f;
    *nox_index = values[7] / 10.0f;
    return 1;
}

esp_err_t sensors_init_all(void) {
    ESP_LOGI(TAG_SENS, "Init I2C + sensors...");
    vTaskDelay(pdMS_TO_TICKS(1500));
    if (s_i2c_bus) {
        ESP_LOGD(TAG_SENS, "I2C bus ya inicializado");
        return ESP_OK;
    }
    vTaskDelay(pdMS_TO_TICKS(1500));

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true }
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (ret != ESP_OK) return ret;

    i2c_device_config_t scd_cfg = {
        .device_address = SCD4X_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(s_i2c_bus, &scd_cfg, &s_scd4x_dev);
    if (ret != ESP_OK) return ret;

    i2c_device_config_t sen_cfg = {
        .device_address = SEN5X_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(s_i2c_bus, &sen_cfg, &s_sen5x_dev);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(200));
    sen5x_device_reset();
    vTaskDelay(pdMS_TO_TICKS(100));
    sen5x_start_measurement();
    vTaskDelay(pdMS_TO_TICKS(50));
    scd4x_start_measurement();
    vTaskDelay(pdMS_TO_TICKS(5000));
    return ESP_OK;
}

esp_err_t sensors_read(SensorData *out) {
    if (!out) return ESP_ERR_INVALID_ARG;

    uint16_t co2;
    float scd_temp, scd_hum;
    uint8_t data_ready = 0;
    uint8_t buf[24];

    esp_err_t ret = scd4x_read_measurement(&co2, &scd_temp, &scd_hum);
    if (ret != ESP_OK) return ret;

    int timeout = 30;
    do {
        ret = sen5x_read_data_ready(&data_ready);
        if (ret != ESP_OK) return ret;
        if (data_ready == 1) break;
    } while (--timeout > 0);
    if (data_ready != 1) return ESP_ERR_TIMEOUT;

    ret = sen5x_read_measured_values(buf, sizeof(buf));
    if (ret != ESP_OK) return ret;

    float pm1, pm25, pm4, pm10, rh, temp, voc, nox;
    if (!sen5x_decode_measurement(buf, &pm1, &pm25, &pm4, &pm10, &rh, &temp, &voc, &nox)) {
        return ESP_ERR_INVALID_CRC;
    }

    out->co2 = co2;
    out->scd_temp = scd_temp;
    out->scd_hum = scd_hum;
    out->pm1p0 = pm1;
    out->pm2p5 = pm25;
    out->pm4p0 = pm4;
    out->pm10p0 = pm10;
    out->voc = voc;
    out->nox = nox;
    out->sen_temp = temp;
    out->sen_hum = rh;
    out->avg_temp = (scd_temp + temp) / 2.0f;
    out->avg_hum = (scd_hum + rh) / 2.0f;
    return ESP_OK;
}

void sensors_format_json(const SensorData *d, const char *time_str, const char *fecha_str, const char *inicio_str, char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;
    int written = snprintf(buf, buf_size,
        "{\"pm1p0\":%.2f,\"pm2p5\":%.2f,\"pm4p0\":%.2f,\"pm10p0\":%.2f,\"voc\":%.1f,\"nox\":%.1f,\"cTe\":%.2f,\"cHu\":%.2f,\"co2\":%u,\"fecha\":\"%s\",\"inicio\":\"%s\",\"ciudad\":\"%s\",\"hora\":\"%s\",\"id\":\"%s\"}",
        d->pm1p0, d->pm2p5, d->pm4p0, d->pm10p0, d->voc, d->nox, d->avg_temp, d->avg_hum, d->co2, fecha_str, inicio_str, g_city_state, time_str, DEVICE_ID);
    if (written < 0 || (size_t)written >= buf_size) {
        if (buf_size) buf[buf_size-1] = '\0';
    }
}

void sensors_set_city_state(const char *city_state) {
    if (!city_state) return;
    size_t len = strlen(city_state);
    if (len >= sizeof(g_city_state)) len = sizeof(g_city_state)-1;
    memcpy(g_city_state, city_state, len);
    g_city_state[len] = '\0';
}


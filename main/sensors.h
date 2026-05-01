#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

typedef struct {
    // SCD4x
    uint16_t co2;
    float scd_temp;
    float scd_hum;
    // SEN5x
    float pm1p0;
    float pm2p5;
    float pm4p0;
    float pm10p0;
    float voc;
    float nox;
    float sen_temp;
    float sen_hum;
    // Derivados
    float avg_temp;
    float avg_hum;
} SensorData;

// Codigos de diagnostico
typedef enum {
    SENSOR_DIAG_OK           = 0,   // 00
    SENSOR_DIAG_CRC          = 1,   // 01
    SENSOR_DIAG_TIMEOUT      = 2,   // 02
    SENSOR_DIAG_OUT_OF_RANGE = 3,   // 03
    SENSOR_DIAG_I2C_TX       = 4,   // 04
    SENSOR_DIAG_I2C_RX       = 5,   // 05
    SENSOR_DIAG_OTHER        = 99   // 99
} sensor_diag_code_t;

// Inicializa I2C y ambos sensores.
esp_err_t sensors_init_all(void);

// Lectura separada por sensor
esp_err_t sensors_read_scd40(SensorData *out);
esp_err_t sensors_read_sen55(SensorData *out);

// Wrapper: lee ambos sensores
esp_err_t sensors_read(SensorData *out);

// Formatea JSON con claves personalizadas.
// time_str e inicio_str deben ir en formato "HH:MM:SS".
// fecha_str debe ir en formato "DD-MM-YYYY".
void sensors_format_json(const SensorData *d,
                         const char *time_str,
                         const char *fecha_str,
                         const char *inicio_str,
                         char *buf,
                         size_t buf_size);

// Establece ciudad (city-state) obtenida externamente
void sensors_set_city_state(const char *city_state);

// Getters de diagnostico del ultimo intento
int sensors_get_last_scd40_diag(void);
int sensors_get_last_sen55_diag(void);
void sensors_reset_diag(void);

#pragma once
#include "esp_err.h"
#include <stdint.h>

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

// Inicializa I2C y ambos sensores (SEN5x y SCD4x).
esp_err_t sensors_init_all(void);

// Lee datos de ambos sensores y calcula promedios. Devuelve ESP_OK si todo OK.
esp_err_t sensors_read(SensorData *out);

// Formatea JSON con claves personalizadas.
// time_str debe ser HH:MM:SS, fecha_str e inicio_str en formato "YYYY-MM-DD HH:MM:SS".
void sensors_format_json(const SensorData *d,
                         const char *time_str,
                         const char *fecha_str,
                         const char *inicio_str,
                         char *buf,
                         size_t buf_size);

// Establece ciudad (city-state) obtenida externamente (Geoapify)
void sensors_set_city_state(const char *city_state);

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// Envío de lecturas (equivale a firebase_putData/postData)
int hostinger_ingest_post(const char* json_utf8);

// Admin (replica "delete on boot" de Firebase)
int hostinger_delete_all_for_device(const char* device_id);

// Admin (replica trim_oldest_batch de Firebase)
int hostinger_trim_oldest_batch(const char* device_id, int batch_size);

#ifdef __cplusplus
}
#endif

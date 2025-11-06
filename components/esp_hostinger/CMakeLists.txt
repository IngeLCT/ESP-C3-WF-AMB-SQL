idf_component_register(
  SRCS "hostinger_ingest.c" "hostinger_admin.c"
  INCLUDE_DIRS "include"
  REQUIRES esp_http_client esp-tls esp_crt_bundle
)
# Para ver Privado.h desde el componente
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/main")
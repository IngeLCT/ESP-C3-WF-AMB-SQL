idf_component_register(
  SRCS "hostinger_ingest.c" "hostinger_admin.c"
  INCLUDE_DIRS "include"
  REQUIRES esp_http_client esp-tls
)
# Para acceder a Privado.h desde este componente
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/main")
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_11)

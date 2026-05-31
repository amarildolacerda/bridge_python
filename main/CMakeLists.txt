idf_component_register(SRC_DIRS          "."
                       PRIV_INCLUDE_DIRS  "."
                       "${ESP_MATTER_PATH}/examples/common/utils"
                       "${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip/src"
                       PRIV_REQUIRES      esp_matter esp_matter_bridge esp_matter_console esp_http_server json driver)

target_add_binary_data(${COMPONENT_LIB} "web/dashboard.html" TEXT)
target_add_binary_data(${COMPONENT_LIB} "web/dashboard.css" TEXT)

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")

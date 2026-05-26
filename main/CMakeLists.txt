idf_component_register(SRC_DIRS          "."
                       PRIV_INCLUDE_DIRS  "."
                       "${ESP_MATTER_PATH}/examples/common/utils"
                       "${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip/src")

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")

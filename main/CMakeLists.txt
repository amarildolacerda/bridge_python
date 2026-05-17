idf_component_register(SRCS "main.cpp" "bridge_devices.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash  esp_netif esp_matter)
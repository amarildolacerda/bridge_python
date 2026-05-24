#pragma once

#include <esp_err.h>

esp_err_t wifi_server_start(void);
esp_err_t wifi_server_stop(void);
void wifi_server_set_bridge_handle(void *bridge_handle);

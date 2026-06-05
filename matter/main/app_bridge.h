#pragma once

#include <esp_err.h>
#include <stdint.h>
#include <esp_matter.h>
#include <app_device_registry.h>

esp_err_t bridge_init(void);
esp_err_t bridge_add_device(const char *id, device_type_t type, const char *name);
esp_err_t bridge_remove_device(const char *id);
esp_err_t bridge_update_matter_state(const char *id, const char *key, const char *value);
uint16_t bridge_get_aggregator_endpoint_id(void);
esp_matter::node_t *bridge_get_node(void);

#include "app_bridge.h"
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_bridge.h>
#include <esp_matter_endpoint.h>
#include <esp_netif.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_bridge";

static esp_matter::node_t *s_node = NULL;
static uint16_t s_aggregator_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

static esp_err_t device_type_callback(endpoint_t *ep, uint32_t device_type_id, void *priv_data)
{
    switch (device_type_id) {
    case ESP_MATTER_ON_OFF_LIGHT_DEVICE_TYPE_ID: {
        on_off_light::config_t config;
        return on_off_light::add(ep, &config);
    }
    case ESP_MATTER_DIMMABLE_LIGHT_DEVICE_TYPE_ID: {
        dimmable_light::config_t config;
        return dimmable_light::add(ep, &config);
    }
    case ESP_MATTER_TEMPERATURE_SENSOR_DEVICE_TYPE_ID: {
        temperature_sensor::config_t config;
        return temperature_sensor::add(ep, &config);
    }
    case ESP_MATTER_HUMIDITY_SENSOR_DEVICE_TYPE_ID: {
        humidity_sensor::config_t config;
        return humidity_sensor::add(ep, &config);
    }
    case ESP_MATTER_CONTACT_SENSOR_DEVICE_TYPE_ID: {
        contact_sensor::config_t config;
        return contact_sensor::add(ep, &config);
    }
    case ESP_MATTER_OCCUPANCY_SENSOR_DEVICE_TYPE_ID: {
        occupancy_sensor::config_t config;
        return occupancy_sensor::add(ep, &config);
    }
    case ESP_MATTER_LIGHT_SENSOR_DEVICE_TYPE_ID: {
        light_sensor::config_t config;
        return light_sensor::add(ep, &config);
    }
    default:
        ESP_LOGE(TAG, "Unsupported device type: 0x%04lx", device_type_id);
        return ESP_ERR_INVALID_ARG;
    }
}

static void handle_matter_to_device_command(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    const char *cluster_str = "";
    const char *command_str = "";
    char data_str[MAX_COMMAND_DATA_LEN] = {0};

    switch (cluster_id) {
    case OnOff::Id:
        cluster_str = "onoff";
        command_str = (attribute_id == OnOff::Attributes::OnOff::Id) ? "set_onoff" : "unknown";
        snprintf(data_str, sizeof(data_str), "%d", val->val.b);
        break;
    case LevelControl::Id:
        cluster_str = "levelcontrol";
        command_str = (attribute_id == LevelControl::Attributes::CurrentLevel::Id) ? "set_level" : "unknown";
        snprintf(data_str, sizeof(data_str), "%d", val->val.u8);
        break;
    default:
        ESP_LOGW(TAG, "Unhandled cluster 0x%08lx attribute 0x%08lx", cluster_id, attribute_id);
        return;
    }

    if (strlen(cluster_str) > 0) {
        device_registry_add_command(endpoint_id, cluster_str, command_str, data_str);
    }
}

esp_err_t bridge_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id,
                                      uint32_t cluster_id, uint32_t attribute_id,
                                      esp_matter_attr_val_t *val, void *priv_data)
{
    if (type == PRE_UPDATE) {
        ESP_LOGI(TAG, "Attribute update: ep=%d cluster=0x%08lx attr=0x%08lx",
                 endpoint_id, cluster_id, attribute_id);

        bridged_device_t *dev = device_registry_get_by_endpoint(endpoint_id);
        if (dev) {
            handle_matter_to_device_command(endpoint_id, cluster_id, attribute_id, val);
        }
    }
    return ESP_OK;
}

esp_err_t bridge_identification_cb(identification::callback_type_t type, uint16_t endpoint_id,
                                    uint8_t effect_id, uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification: type=%d ep=%d effect=%d", type, endpoint_id, effect_id);
    return ESP_OK;
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged: {
        esp_netif_ip_info_t ip_info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("STA_DEF");
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            ESP_LOGI(TAG, "Matter IP: " IPSTR, IP2STR(&ip_info.ip));
        } else {
            ESP_LOGI(TAG, "IP address changed (no address yet)");
        }
        break;
    }
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Failsafe timer expired");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;
    default:
        break;
    }
}

esp_err_t bridge_init(void)
{
    node::config_t node_config;
    s_node = node::create(&node_config, bridge_attribute_update_cb, bridge_identification_cb);
    if (!s_node) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        return ESP_FAIL;
    }

    aggregator::config_t aggregator_config;
    endpoint_t *aggregator = aggregator::create(s_node, &aggregator_config, ENDPOINT_FLAG_NONE, NULL);
    if (!aggregator) {
        ESP_LOGE(TAG, "Failed to create aggregator endpoint");
        return ESP_FAIL;
    }

    s_aggregator_endpoint_id = endpoint::get_id(aggregator);
    ESP_LOGI(TAG, "Aggregator created with endpoint_id %d", s_aggregator_endpoint_id);

    esp_err_t err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_matter_bridge::initialize(s_node, device_type_callback);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bridge: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Bridge initialized successfully");
    return ESP_OK;
}

esp_err_t bridge_add_device(const char *id, device_type_t type, const char *name)
{
    if (!s_node) {
        ESP_LOGE(TAG, "Bridge not initialized");
        return ESP_FAIL;
    }

    uint32_t matter_type_id = device_type_to_matter_id(type);

    // Tanque: sem endpoint Matter, apenas coleta dados via REST
    if (matter_type_id == 0) {
        ESP_LOGI(TAG, "Device %s registered as data-only (no Matter endpoint)", id);
        return ESP_OK;
    }

    // Make a copy of id for priv_data (HTTP buffer may be freed)
    char *id_copy = strdup(id);
    if (!id_copy) {
        ESP_LOGE(TAG, "Failed to allocate id copy for %s", id);
        return ESP_FAIL;
    }

    esp_matter_bridge::device_t *dev = esp_matter_bridge::create_device(
        s_node, s_aggregator_endpoint_id, matter_type_id, (void *)id_copy);
    if (!dev) {
        ESP_LOGE(TAG, "Failed to create bridged device for %s (type 0x%04lx, aggregator ep=%d)",
                 id, matter_type_id, s_aggregator_endpoint_id);
        free(id_copy);
        return ESP_FAIL;
    }

    endpoint::enable(dev->endpoint);
    uint16_t ep_id = endpoint::get_id(dev->endpoint);
    device_registry_set_endpoint_id(id, ep_id);

    ESP_LOGI(TAG, "Bridged device %s created on endpoint %d (Matter type 0x%04lx)",
             id, ep_id, matter_type_id);
    return ESP_OK;
}

esp_err_t bridge_remove_device(const char *id)
{
    bridged_device_t *dev = device_registry_get_by_id(id);
    if (!dev) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_matter_bridge::device_t *bridge_dev = esp_matter_bridge::resume_device(
        s_node, dev->matter_endpoint_id, NULL);
    if (bridge_dev) {
        esp_matter_bridge::remove_device(bridge_dev);
    }

    device_registry_remove_device(id);
    ESP_LOGI(TAG, "Bridged device %s removed", id);
    return ESP_OK;
}

esp_err_t bridge_update_matter_state(const char *id, const char *key, const char *value)
{
    bridged_device_t *dev = device_registry_get_by_id(id);
    if (!dev) {
        return ESP_ERR_NOT_FOUND;
    }

    // Tanque: dados sem endpoint Matter, apenas log
    if (dev->type == DEVICE_TYPE_TANQUE) {
        ESP_LOGI(TAG, "Tanque %s: %s = %s", id, key, value);
        return ESP_OK;
    }

    if (dev->matter_endpoint_id == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    uint16_t ep_id = dev->matter_endpoint_id;

    if (strcmp(key, "on") == 0) {
        bool on = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
        esp_matter_attr_val_t val = esp_matter_bool(on);
        return esp_matter::attribute::update(ep_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &val);
    }
    if (strcmp(key, "level") == 0) {
        uint8_t level = (uint8_t)atoi(value);
        level = (level > 254) ? 254 : level;
        esp_matter_attr_val_t val = esp_matter_uint8(level);
        return esp_matter::attribute::update(ep_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id, &val);
    }
    if (strcmp(key, "temperature") == 0) {
        int16_t temp = (int16_t)(atof(value) * 100);
        esp_matter_attr_val_t val = esp_matter_int16(temp);
        return esp_matter::attribute::update(ep_id, TemperatureMeasurement::Id,
                                             TemperatureMeasurement::Attributes::MeasuredValue::Id, &val);
    }
    if (strcmp(key, "humidity") == 0) {
        uint16_t hum = (uint16_t)(atof(value) * 100);
        esp_matter_attr_val_t val = esp_matter_uint16(hum);
        return esp_matter::attribute::update(ep_id, RelativeHumidityMeasurement::Id,
                                             RelativeHumidityMeasurement::Attributes::MeasuredValue::Id, &val);
    }
    if (strcmp(key, "contact") == 0) {
        bool contact = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
        esp_matter_attr_val_t val = esp_matter_bool(contact);
        return esp_matter::attribute::update(ep_id, BooleanState::Id,
                                             BooleanState::Attributes::StateValue::Id, &val);
    }
    if (strcmp(key, "occupancy") == 0) {
        bool occupied = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
        esp_matter_attr_val_t val = esp_matter_bool(occupied);
        return esp_matter::attribute::update(ep_id, OccupancySensing::Id,
                                             OccupancySensing::Attributes::Occupancy::Id, &val);
    }
    if (strcmp(key, "light_level") == 0) {
        uint16_t lux = (uint16_t)atoi(value);
        esp_matter_attr_val_t val = esp_matter_uint16(lux);
        return esp_matter::attribute::update(ep_id, IlluminanceMeasurement::Id,
                                             IlluminanceMeasurement::Attributes::MeasuredValue::Id, &val);
    }

    ESP_LOGW(TAG, "Unknown state key: %s for device %s", key, id);
    return ESP_ERR_INVALID_ARG;
}

uint16_t bridge_get_aggregator_endpoint_id(void)
{
    return s_aggregator_endpoint_id;
}

esp_matter::node_t *bridge_get_node(void)
{
    return s_node;
}

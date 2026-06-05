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

struct bridge_add_work_t {
    char id[MAX_DEVICE_ID_LEN];
    device_type_t type;
    char name[MAX_DEVICE_NAME_LEN];
};

struct bridge_update_work_t {
    char id[MAX_DEVICE_ID_LEN];
    char key[32];
    char value[64];
};

struct bridge_remove_work_t {
    char id[MAX_DEVICE_ID_LEN];
};

static void set_reachable_work(intptr_t arg)
{
    uint16_t ep_id = (uint16_t)arg;
    esp_matter_attr_val_t reachable = esp_matter_bool(true);
    attribute::update(ep_id, BridgedDeviceBasicInformation::Id,
                      BridgedDeviceBasicInformation::Attributes::Reachable::Id, &reachable);
}

static esp_err_t set_reachable(endpoint_t *ep)
{
    chip::DeviceLayer::PlatformMgr().ScheduleWork(set_reachable_work,
                                                  (intptr_t)endpoint::get_id(ep));
    return ESP_OK;
}

static void bridge_add_device_work(intptr_t arg)
{
    bridge_add_work_t *work = (bridge_add_work_t *)arg;
    if (!work) return;

    uint32_t matter_type_id = device_type_to_matter_id(work->type);
    char *id_copy = strdup(work->id);
    if (!id_copy) {
        ESP_LOGE(TAG, "Failed to allocate id copy for %s", work->id);
        free(work);
        return;
    }

    esp_matter_bridge::device_t *dev = esp_matter_bridge::create_device(
        s_node, s_aggregator_endpoint_id, matter_type_id, (void *)id_copy);
    if (!dev) {
        ESP_LOGE(TAG, "Failed to create bridged device for %s (type 0x%04lx)",
                 work->id, matter_type_id);
        free(id_copy);
        free(work);
        return;
    }

    uint16_t ep_id = endpoint::get_id(dev->endpoint);
    device_registry_set_endpoint_id(work->id, ep_id);
    set_reachable(dev->endpoint);
    ESP_LOGI(TAG, "Bridged device %s created on endpoint %d (Matter type 0x%04lx)",
             work->id, ep_id, matter_type_id);
    free(work);
}

static void bridge_update_matter_state_work(intptr_t arg)
{
    bridge_update_work_t *work = (bridge_update_work_t *)arg;
    if (!work) return;

    bridged_device_t *dev = device_registry_get_by_id(work->id);
    if (!dev) {
        ESP_LOGW(TAG, "Device %s not found for state update", work->id);
        free(work);
        return;
    }

    if (dev->type == DEVICE_TYPE_TANQUE) {
        ESP_LOGI(TAG, "Tanque %s: %s = %s", work->id, work->key, work->value);
        free(work);
        return;
    }

    if (dev->matter_endpoint_id == 0) {
        free(work);
        return;
    }

    uint16_t ep_id = dev->matter_endpoint_id;

    if (strcmp(work->key, "on") == 0) {
        bool on = (strcmp(work->value, "true") == 0 || strcmp(work->value, "1") == 0);
        esp_matter_attr_val_t val = esp_matter_bool(on);
        attribute::update(ep_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &val);
    } else if (strcmp(work->key, "level") == 0) {
        uint8_t level = (uint8_t)atoi(work->value);
        level = (level > 254) ? 254 : level;
        esp_matter_attr_val_t val = esp_matter_uint8(level);
        attribute::update(ep_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id, &val);
    } else if (strcmp(work->key, "temperature") == 0) {
        int16_t temp = (int16_t)(atof(work->value) * 100);
        esp_matter_attr_val_t val = esp_matter_int16(temp);
        attribute::update(ep_id, TemperatureMeasurement::Id,
                          TemperatureMeasurement::Attributes::MeasuredValue::Id, &val);
    } else if (strcmp(work->key, "humidity") == 0) {
        uint16_t hum = (uint16_t)(atof(work->value) * 100);
        esp_matter_attr_val_t val = esp_matter_uint16(hum);
        attribute::update(ep_id, RelativeHumidityMeasurement::Id,
                          RelativeHumidityMeasurement::Attributes::MeasuredValue::Id, &val);
    } else if (strcmp(work->key, "contact") == 0) {
        bool contact = (strcmp(work->value, "true") == 0 || strcmp(work->value, "1") == 0);
        esp_matter_attr_val_t val = esp_matter_bool(contact);
        attribute::update(ep_id, BooleanState::Id,
                          BooleanState::Attributes::StateValue::Id, &val);
    } else if (strcmp(work->key, "occupancy") == 0) {
        bool occupied = (strcmp(work->value, "true") == 0 || strcmp(work->value, "1") == 0);
        esp_matter_attr_val_t val = esp_matter_bool(occupied);
        attribute::update(ep_id, OccupancySensing::Id,
                          OccupancySensing::Attributes::Occupancy::Id, &val);
    } else if (strcmp(work->key, "light_level") == 0) {
        uint16_t lux = (uint16_t)atoi(work->value);
        esp_matter_attr_val_t val = esp_matter_uint16(lux);
        attribute::update(ep_id, IlluminanceMeasurement::Id,
                          IlluminanceMeasurement::Attributes::MeasuredValue::Id, &val);
    } else {
        ESP_LOGW(TAG, "Unknown state key: %s for device %s", work->key, work->id);
    }

    free(work);
}

static void bridge_remove_stale_endpoints_work(intptr_t arg)
{
    int max_devices = MAX_BRIDGED_DEVICE_COUNT;
    uint16_t *endpoint_ids = (uint16_t *)calloc(max_devices, sizeof(uint16_t));
    if (!endpoint_ids) return;

    esp_err_t err = esp_matter_bridge::get_bridged_endpoint_ids(endpoint_ids);
    if (err == ESP_OK) {
        for (int i = 0; i < max_devices; i++) {
            if (endpoint_ids[i] == 0) continue;
            esp_matter_bridge::device_t *dev = esp_matter_bridge::resume_device(
                s_node, endpoint_ids[i], NULL);
            if (dev) {
                esp_matter_bridge::remove_device(dev);
                ESP_LOGI(TAG, "Removed stale bridge endpoint %d", endpoint_ids[i]);
            }
        }
    }
    free(endpoint_ids);
}

static void bridge_remove_device_work(intptr_t arg)
{
    bridge_remove_work_t *work = (bridge_remove_work_t *)arg;
    if (!work) return;

    bridged_device_t *dev = device_registry_get_by_id(work->id);
    if (!dev) {
        free(work);
        return;
    }

    esp_matter_bridge::device_t *bridge_dev = esp_matter_bridge::resume_device(
        s_node, dev->matter_endpoint_id, NULL);
    if (bridge_dev) {
        esp_matter_bridge::remove_device(bridge_dev);
    }

    device_registry_remove_device(work->id);
    ESP_LOGI(TAG, "Bridged device %s removed", work->id);
    free(work);
}

static esp_err_t device_type_callback(endpoint_t *ep, uint32_t device_type_id, void *priv_data)
{
    esp_err_t err = ESP_OK;
    switch (device_type_id) {
    case ESP_MATTER_ON_OFF_LIGHT_DEVICE_TYPE_ID: {
        on_off_light::config_t config;
        err = on_off_light::add(ep, &config);
        break;
    }
    case ESP_MATTER_DIMMABLE_LIGHT_DEVICE_TYPE_ID: {
        dimmable_light::config_t config;
        err = dimmable_light::add(ep, &config);
        break;
    }
    case ESP_MATTER_TEMPERATURE_SENSOR_DEVICE_TYPE_ID: {
        temperature_sensor::config_t config;
        err = temperature_sensor::add(ep, &config);
        break;
    }
    case ESP_MATTER_HUMIDITY_SENSOR_DEVICE_TYPE_ID: {
        humidity_sensor::config_t config;
        err = humidity_sensor::add(ep, &config);
        break;
    }
    case ESP_MATTER_CONTACT_SENSOR_DEVICE_TYPE_ID: {
        contact_sensor::config_t config;
        err = contact_sensor::add(ep, &config);
        break;
    }
    case ESP_MATTER_OCCUPANCY_SENSOR_DEVICE_TYPE_ID: {
        occupancy_sensor::config_t config;
        err = occupancy_sensor::add(ep, &config);
        break;
    }
    case ESP_MATTER_LIGHT_SENSOR_DEVICE_TYPE_ID: {
        light_sensor::config_t config;
        err = light_sensor::add(ep, &config);
        break;
    }
    default:
        ESP_LOGE(TAG, "Unsupported device type: 0x%04lx", device_type_id);
        return ESP_ERR_INVALID_ARG;
    }
    return err;
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

    // Must be called before esp_matter::start()
    esp_err_t err = esp_matter_bridge::initialize(s_node, device_type_callback);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bridge: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter: %s", esp_err_to_name(err));
        return err;
    }

    // Remove stale bridge endpoints restored from NVS on the Matter thread
    chip::DeviceLayer::PlatformMgr().ScheduleWork(bridge_remove_stale_endpoints_work, (intptr_t)NULL);

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

    if (matter_type_id == 0) {
        ESP_LOGI(TAG, "Device %s registered as data-only (no Matter endpoint)", id);
        return ESP_OK;
    }

    bridge_add_work_t *work = (bridge_add_work_t *)malloc(sizeof(bridge_add_work_t));
    if (!work) {
        ESP_LOGE(TAG, "Failed to allocate work item for %s", id);
        return ESP_ERR_NO_MEM;
    }

    strncpy(work->id, id, sizeof(work->id) - 1);
    work->id[sizeof(work->id) - 1] = '\0';
    work->type = type;
    strncpy(work->name, name ? name : id, sizeof(work->name) - 1);
    work->name[sizeof(work->name) - 1] = '\0';

    chip::DeviceLayer::PlatformMgr().ScheduleWork(bridge_add_device_work, (intptr_t)work);
    ESP_LOGI(TAG, "Device %s queued for Matter endpoint creation", id);
    return ESP_OK;
}

esp_err_t bridge_remove_device(const char *id)
{
    bridged_device_t *dev = device_registry_get_by_id(id);
    if (!dev) {
        return ESP_ERR_NOT_FOUND;
    }

    bridge_remove_work_t *work = (bridge_remove_work_t *)malloc(sizeof(bridge_remove_work_t));
    if (!work) {
        ESP_LOGE(TAG, "Failed to allocate remove work item for %s", id);
        return ESP_ERR_NO_MEM;
    }

    strncpy(work->id, id, sizeof(work->id) - 1);
    work->id[sizeof(work->id) - 1] = '\0';

    chip::DeviceLayer::PlatformMgr().ScheduleWork(bridge_remove_device_work, (intptr_t)work);
    ESP_LOGI(TAG, "Device %s queued for removal", id);
    return ESP_OK;
}

esp_err_t bridge_update_matter_state(const char *id, const char *key, const char *value)
{
    bridged_device_t *dev = device_registry_get_by_id(id);
    if (!dev) {
        return ESP_ERR_NOT_FOUND;
    }

    if (dev->type == DEVICE_TYPE_TANQUE) {
        ESP_LOGI(TAG, "Tanque %s: %s = %s", id, key, value);
        return ESP_OK;
    }

    if (dev->matter_endpoint_id == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    bridge_update_work_t *work = (bridge_update_work_t *)malloc(sizeof(bridge_update_work_t));
    if (!work) {
        ESP_LOGE(TAG, "Failed to allocate state update work for %s", id);
        return ESP_ERR_NO_MEM;
    }

    strncpy(work->id, id, sizeof(work->id) - 1);
    work->id[sizeof(work->id) - 1] = '\0';
    strncpy(work->key, key, sizeof(work->key) - 1);
    work->key[sizeof(work->key) - 1] = '\0';
    strncpy(work->value, value, sizeof(work->value) - 1);
    work->value[sizeof(work->value) - 1] = '\0';

    chip::DeviceLayer::PlatformMgr().ScheduleWork(bridge_update_matter_state_work, (intptr_t)work);
    return ESP_OK;
}

uint16_t bridge_get_aggregator_endpoint_id(void)
{
    return s_aggregator_endpoint_id;
}

esp_matter::node_t *bridge_get_node(void)
{
    return s_node;
}

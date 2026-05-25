#include "app_device_registry.h"
#include <esp_log.h>
#include <stdlib.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const char *TAG = "device_registry";

static bridged_device_t s_devices[MAX_BRIDGED_DEVICES];
static int s_device_count = 0;

static SemaphoreHandle_t s_registry_mutex = NULL;

static int find_device_slot(void)
{
    for (int i = 0; i < MAX_BRIDGED_DEVICES; i++) {
        if (!s_devices[i].registered) {
            return i;
        }
    }
    return -1;
}

static int find_device_by_id(const char *id)
{
    for (int i = 0; i < MAX_BRIDGED_DEVICES; i++) {
        if (s_devices[i].registered && strcmp(s_devices[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_device_by_endpoint(uint16_t endpoint_id)
{
    for (int i = 0; i < MAX_BRIDGED_DEVICES; i++) {
        if (s_devices[i].registered && s_devices[i].matter_endpoint_id == endpoint_id) {
            return i;
        }
    }
    return -1;
}

device_type_t device_type_from_string(const char *type_str)
{
    if (strcmp(type_str, "onoff") == 0) return DEVICE_TYPE_ON_OFF;
    if (strcmp(type_str, "dimmable") == 0) return DEVICE_TYPE_DIMMABLE;
    if (strcmp(type_str, "temperature") == 0) return DEVICE_TYPE_TEMPERATURE_SENSOR;
    if (strcmp(type_str, "humidity") == 0) return DEVICE_TYPE_HUMIDITY_SENSOR;
    if (strcmp(type_str, "contact") == 0) return DEVICE_TYPE_CONTACT_SENSOR;
    if (strcmp(type_str, "occupancy") == 0) return DEVICE_TYPE_OCCUPANCY_SENSOR;
    if (strcmp(type_str, "light_sensor") == 0) return DEVICE_TYPE_LIGHT_SENSOR;
    if (strcmp(type_str, "tanque") == 0) return DEVICE_TYPE_TANQUE;
    return DEVICE_TYPE_UNKNOWN;
}

const char *device_type_to_string(device_type_t type)
{
    switch (type) {
    case DEVICE_TYPE_ON_OFF: return "onoff";
    case DEVICE_TYPE_DIMMABLE: return "dimmable";
    case DEVICE_TYPE_TEMPERATURE_SENSOR: return "temperature";
    case DEVICE_TYPE_HUMIDITY_SENSOR: return "humidity";
    case DEVICE_TYPE_CONTACT_SENSOR: return "contact";
    case DEVICE_TYPE_OCCUPANCY_SENSOR: return "occupancy";
    case DEVICE_TYPE_LIGHT_SENSOR: return "light_sensor";
    case DEVICE_TYPE_TANQUE: return "tanque";
    default: return "unknown";
    }
}

uint32_t device_type_to_matter_id(device_type_t type)
{
    switch (type) {
    case DEVICE_TYPE_ON_OFF: return 0x0100;
    case DEVICE_TYPE_DIMMABLE: return 0x0101;
    case DEVICE_TYPE_TEMPERATURE_SENSOR: return 0x0302;
    case DEVICE_TYPE_HUMIDITY_SENSOR: return 0x0307;
    case DEVICE_TYPE_CONTACT_SENSOR: return 0x0015;
    case DEVICE_TYPE_OCCUPANCY_SENSOR: return 0x0107;
    case DEVICE_TYPE_LIGHT_SENSOR: return 0x0106;
    case DEVICE_TYPE_TANQUE: return 0;  // sem Matter endpoint, apenas dados
    default: return 0;
    }
}

esp_err_t device_registry_init(void)
{
    memset(s_devices, 0, sizeof(s_devices));
    s_device_count = 0;
    s_registry_mutex = xSemaphoreCreateMutex();
    if (!s_registry_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Device registry initialized, max %d devices", MAX_BRIDGED_DEVICES);
    return ESP_OK;
}

int device_registry_register(const char *id, device_type_t type, const char *name)
{
    if (!s_registry_mutex) return -1;
    xSemaphoreTake(s_registry_mutex, portMAX_DELAY);

    if (find_device_by_id(id) >= 0) {
        ESP_LOGW(TAG, "Device %s already registered, updating", id);
        xSemaphoreGive(s_registry_mutex);
        return find_device_by_id(id);
    }

    int slot = find_device_slot();
    if (slot < 0) {
        ESP_LOGE(TAG, "No slots available for device %s", id);
        xSemaphoreGive(s_registry_mutex);
        return -1;
    }

    bridged_device_t *dev = &s_devices[slot];
    strncpy(dev->id, id, MAX_DEVICE_ID_LEN - 1);
    dev->id[MAX_DEVICE_ID_LEN - 1] = '\0';
    strncpy(dev->name, name ? name : id, MAX_DEVICE_NAME_LEN - 1);
    dev->name[MAX_DEVICE_NAME_LEN - 1] = '\0';
    dev->type = type;
    dev->registered = true;
    dev->matter_endpoint_id = 0;
    dev->pending_command_count = 0;
    dev->online = true;
    s_device_count++;

    ESP_LOGI(TAG, "Registered device: %s (type: %s, slot: %d)", id, device_type_to_string(type), slot);
    xSemaphoreGive(s_registry_mutex);
    return slot;
}

bridged_device_t *device_registry_get_by_id(const char *id)
{
    if (!s_registry_mutex) return NULL;
    xSemaphoreTake(s_registry_mutex, portMAX_DELAY);
    int idx = find_device_by_id(id);
    if (idx < 0) {
        xSemaphoreGive(s_registry_mutex);
        return NULL;
    }
    xSemaphoreGive(s_registry_mutex);
    return &s_devices[idx];
}

bridged_device_t *device_registry_get_by_endpoint(uint16_t endpoint_id)
{
    if (!s_registry_mutex) return NULL;
    xSemaphoreTake(s_registry_mutex, portMAX_DELAY);
    int idx = find_device_by_endpoint(endpoint_id);
    if (idx < 0) {
        xSemaphoreGive(s_registry_mutex);
        return NULL;
    }
    xSemaphoreGive(s_registry_mutex);
    return &s_devices[idx];
}

bridged_device_t *device_registry_get_all(int *count)
{
    if (count) *count = s_device_count;
    return s_devices;
}

esp_err_t device_registry_update_state(const char *id, const char *key, const char *value)
{
    if (!s_registry_mutex) return ESP_FAIL;
    xSemaphoreTake(s_registry_mutex, portMAX_DELAY);
    int idx = find_device_by_id(id);
    if (idx < 0) {
        xSemaphoreGive(s_registry_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "Device %s state update: %s = %s", id, key, value);
    xSemaphoreGive(s_registry_mutex);
    return ESP_OK;
}

esp_err_t device_registry_add_command(uint16_t endpoint_id, const char *cluster, const char *command, const char *data)
{
    if (!s_registry_mutex) return ESP_FAIL;
    xSemaphoreTake(s_registry_mutex, portMAX_DELAY);
    int idx = find_device_by_endpoint(endpoint_id);
    if (idx < 0) {
        xSemaphoreGive(s_registry_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    bridged_device_t *dev = &s_devices[idx];
    if (dev->pending_command_count >= MAX_PENDING_COMMANDS) {
        ESP_LOGW(TAG, "Command queue full for device %s", dev->id);
        xSemaphoreGive(s_registry_mutex);
        return ESP_ERR_NO_MEM;
    }

    pending_command_t *pc = &dev->pending_commands[dev->pending_command_count++];
    strncpy(pc->cluster, cluster, sizeof(pc->cluster) - 1);
    strncpy(pc->command, command, sizeof(pc->command) - 1);
    if (data) strncpy(pc->data, data, MAX_COMMAND_DATA_LEN - 1);
    else pc->data[0] = '\0';

    ESP_LOGI(TAG, "Command queued for %s: %s/%s", dev->id, cluster, command);
    xSemaphoreGive(s_registry_mutex);
    return ESP_OK;
}

int device_registry_get_commands(uint16_t endpoint_id, pending_command_t *commands, int max_commands)
{
    if (!s_registry_mutex) return 0;
    xSemaphoreTake(s_registry_mutex, portMAX_DELAY);
    int idx = find_device_by_endpoint(endpoint_id);
    if (idx < 0) {
        xSemaphoreGive(s_registry_mutex);
        return 0;
    }

    bridged_device_t *dev = &s_devices[idx];
    int count = (dev->pending_command_count < max_commands) ? dev->pending_command_count : max_commands;
    for (int i = 0; i < count; i++) {
        memcpy(&commands[i], &dev->pending_commands[i], sizeof(pending_command_t));
    }
    dev->pending_command_count = 0;

    xSemaphoreGive(s_registry_mutex);
    return count;
}

esp_err_t device_registry_remove_device(const char *id)
{
    if (!s_registry_mutex) return ESP_FAIL;
    xSemaphoreTake(s_registry_mutex, portMAX_DELAY);
    int idx = find_device_by_id(id);
    if (idx < 0) {
        xSemaphoreGive(s_registry_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    memset(&s_devices[idx], 0, sizeof(bridged_device_t));
    s_device_count--;
    ESP_LOGI(TAG, "Removed device: %s", id);
    xSemaphoreGive(s_registry_mutex);
    return ESP_OK;
}

void device_registry_set_endpoint_id(const char *id, uint16_t endpoint_id)
{
    if (!s_registry_mutex) return;
    xSemaphoreTake(s_registry_mutex, portMAX_DELAY);
    int idx = find_device_by_id(id);
    if (idx >= 0) {
        s_devices[idx].matter_endpoint_id = endpoint_id;
        ESP_LOGI(TAG, "Device %s mapped to Matter endpoint %d", id, endpoint_id);
    }
    xSemaphoreGive(s_registry_mutex);
}

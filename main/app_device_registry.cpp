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
    dev->state_json[0] = '\0';
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

    bridged_device_t *dev = &s_devices[idx];
    char *state = dev->state_json;
    int state_len = strlen(state);

    char entry[MAX_DEVICE_STATE_LEN];
    snprintf(entry, sizeof(entry), "%s=%s", key, value);

    char *found_key = strstr(state, key);
    if (found_key) {
        char *sep = found_key + strlen(key);
        if (sep < state + state_len && *sep == '=') {
            char *old_val_end = sep + 1;
            char *next = strchr(old_val_end, '|');
            int old_len = next ? (next - sep) : (state + state_len - sep);
            int new_len = strlen(entry);
            if (old_len != new_len) {
                memmove(sep + new_len, sep + old_len, state + state_len - (sep + old_len) + 1);
            }
            memcpy(sep, entry + (key - found_key), new_len);
            xSemaphoreGive(s_registry_mutex);
            return ESP_OK;
        }
    }

    if (state_len > 0) {
        strncat(state, "|", MAX_DEVICE_STATE_LEN - strlen(state) - 1);
    }
    strncat(state, entry, MAX_DEVICE_STATE_LEN - strlen(state) - 1);

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

const char *device_registry_get_state_json(const char *id)
{
    if (!s_registry_mutex) return "{}";
    xSemaphoreTake(s_registry_mutex, portMAX_DELAY);
    int idx = find_device_by_id(id);
    if (idx < 0) {
        xSemaphoreGive(s_registry_mutex);
        return "{}";
    }

    static char json_buf[MAX_DEVICE_STATE_LEN + 16];
    const char *state = s_devices[idx].state_json;
    if (strlen(state) == 0) {
        xSemaphoreGive(s_registry_mutex);
        return "{}";
    }

    char tmp[MAX_DEVICE_STATE_LEN];
    strncpy(tmp, state, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    xSemaphoreGive(s_registry_mutex);

    json_buf[0] = '{';
    int pos = 1;
    char *token = strtok(tmp, "|");
    while (token && pos < (int)sizeof(json_buf) - 4) {
        char *eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';
            const char *k = token;
            const char *v = eq + 1;
            bool is_bool = (strcmp(v, "true") == 0 || strcmp(v, "false") == 0);
            bool is_num = true;
            for (const char *p = v; *p; p++) {
                if (*p != '.' && *p != '-' && (*p < '0' || *p > '9')) { is_num = false; break; }
            }
            if (pos > 1) json_buf[pos++] = ',';
            if (is_bool || is_num) {
                pos += snprintf(json_buf + pos, sizeof(json_buf) - pos, "\"%s\":%s", k, v);
            } else {
                pos += snprintf(json_buf + pos, sizeof(json_buf) - pos, "\"%s\":\"%s\"", k, v);
            }
        }
        token = strtok(NULL, "|");
    }

    if (pos < (int)sizeof(json_buf)) {
        json_buf[pos] = '}';
        json_buf[pos + 1] = '\0';
    } else {
        json_buf[sizeof(json_buf) - 2] = '}';
        json_buf[sizeof(json_buf) - 1] = '\0';
    }

    return json_buf;
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

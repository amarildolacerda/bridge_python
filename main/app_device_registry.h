#pragma once

#include <esp_err.h>
#include <stdint.h>
#include <string.h>

#define MAX_DEVICE_ID_LEN 48
#define MAX_DEVICE_NAME_LEN 48
#define MAX_DEVICE_STATE_LEN 48
#define MAX_BRIDGED_DEVICES CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT
#define MAX_PENDING_COMMANDS 4
#define MAX_COMMAND_DATA_LEN 32

typedef enum {
    DEVICE_TYPE_ON_OFF = 0,
    DEVICE_TYPE_DIMMABLE,
    DEVICE_TYPE_TEMPERATURE_SENSOR,
    DEVICE_TYPE_HUMIDITY_SENSOR,
    DEVICE_TYPE_CONTACT_SENSOR,
    DEVICE_TYPE_OCCUPANCY_SENSOR,
    DEVICE_TYPE_LIGHT_SENSOR,
    DEVICE_TYPE_TANQUE,
    DEVICE_TYPE_UNKNOWN,
} device_type_t;

typedef struct {
    char command[32];
    char cluster[32];
    char data[MAX_COMMAND_DATA_LEN];
} pending_command_t;

typedef struct {
    char id[MAX_DEVICE_ID_LEN];
    char name[MAX_DEVICE_NAME_LEN];
    char ip[16];
    device_type_t type;
    bool registered;
    uint16_t matter_endpoint_id;
    pending_command_t pending_commands[MAX_PENDING_COMMANDS];
    int pending_command_count;
    bool online;
    char state_json[MAX_DEVICE_STATE_LEN];
} bridged_device_t;

device_type_t device_type_from_string(const char *type_str);
const char *device_type_to_string(device_type_t type);
uint32_t device_type_to_matter_id(device_type_t type);

esp_err_t device_registry_init(void);
int device_registry_register(const char *id, device_type_t type, const char *name, const char *ip);
bridged_device_t *device_registry_get_by_id(const char *id);
bridged_device_t *device_registry_get_by_endpoint(uint16_t endpoint_id);
bridged_device_t *device_registry_get_all(int *count);
esp_err_t device_registry_update_state(const char *id, const char *key, const char *value);
const char *device_registry_get_state_json(const char *id);
esp_err_t device_registry_add_command(uint16_t endpoint_id, const char *cluster, const char *command, const char *data);
int device_registry_get_commands(uint16_t endpoint_id, pending_command_t *commands, int max_commands);
esp_err_t device_registry_remove_device(const char *id);
void device_registry_set_endpoint_id(const char *id, uint16_t endpoint_id);

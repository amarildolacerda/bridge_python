#include "app_wifi_server.h"
#include "app_bridge.h"
#include "app_device_registry.h"
#include <esp_log.h>
#include <esp_http_server.h>
#include <cJSON.h>
#include <string.h>

static const char *TAG = "wifi_server";
static httpd_handle_t s_server = NULL;

#define SCRATCH_BUFSIZE 8192

static esp_err_t register_device_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }

    char *buf = (char *)malloc(SCRATCH_BUFSIZE);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
        return ESP_FAIL;
    }

    int received = 0, cur_len = 0;
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "read error");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
        return ESP_FAIL;
    }

    cJSON *id_item = cJSON_GetObjectItem(root, "id");
    cJSON *type_item = cJSON_GetObjectItem(root, "type");
    cJSON *name_item = cJSON_GetObjectItem(root, "name");

    if (!id_item || !id_item->valuestring || !type_item || !type_item->valuestring) {
        cJSON_Delete(root);
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing id or type");
        return ESP_FAIL;
    }

    const char *id = id_item->valuestring;
    const char *type_str = type_item->valuestring;
    const char *name = name_item ? name_item->valuestring : id;

    device_type_t type = device_type_from_string(type_str);
    if (type == DEVICE_TYPE_UNKNOWN) {
        cJSON_Delete(root);
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unsupported device type");
        return ESP_FAIL;
    }

    int slot = device_registry_register(id, type, name);
    if (slot < 0) {
        cJSON_Delete(root);
        free(buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "registry full");
        return ESP_FAIL;
    }

    esp_err_t err = bridge_add_device(id, type, name);
    if (err != ESP_OK) {
        device_registry_remove_device(id);
        cJSON_Delete(root);
        free(buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "bridge add failed");
        return ESP_FAIL;
    }

    bridged_device_t *dev = device_registry_get_by_id(id);
    uint16_t ep_id = dev ? dev->matter_endpoint_id : 0;

    httpd_resp_set_type(req, "application/json");
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddNumberToObject(resp, "endpoint_id", ep_id);
    const char *resp_str = cJSON_Print(resp);
    httpd_resp_sendstr(req, resp_str);
    free((void *)resp_str);
    cJSON_Delete(resp);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Device registered: %s (type: %s, ep: %d)", id, type_str, ep_id);
    free(buf);
    return ESP_OK;
}

static esp_err_t device_state_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }

    char *buf = (char *)malloc(SCRATCH_BUFSIZE);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
        return ESP_FAIL;
    }

    int received = 0, cur_len = 0;
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "read error");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
        return ESP_FAIL;
    }

    cJSON *id_item = cJSON_GetObjectItem(root, "id");
    if (!id_item || !id_item->valuestring) {
        cJSON_Delete(root);
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing id");
        return ESP_FAIL;
    }

    const char *id = id_item->valuestring;
    bridged_device_t *dev = device_registry_get_by_id(id);
    if (!dev) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "device not found");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *child = NULL;
    cJSON_ArrayForEach(child, root) {
        if (strcmp(child->string, "id") == 0) continue;

        const char *key = child->string;
        char value[64];
        if (cJSON_IsBool(child)) {
            snprintf(value, sizeof(value), "%s", child->valueint ? "true" : "false");
        } else if (cJSON_IsNumber(child)) {
            snprintf(value, sizeof(value), "%g", child->valuedouble);
        } else if (cJSON_IsString(child)) {
            snprintf(value, sizeof(value), "%s", child->valuestring);
        } else {
            continue;
        }

        device_registry_update_state(id, key, value);
        bridge_update_matter_state(id, key, value);
    }

    httpd_resp_set_type(req, "application/json");
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    const char *resp_str = cJSON_Print(resp);
    httpd_resp_sendstr(req, resp_str);
    free((void *)resp_str);
    cJSON_Delete(resp);
    cJSON_Delete(root);
    free(buf);

    return ESP_OK;
}

static esp_err_t device_commands_handler(httpd_req_t *req)
{
    char id_str[MAX_DEVICE_ID_LEN] = {0};
    char query[256] = {0};
    bool found = false;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char *param = strtok(query, "&");
        while (param) {
            if (strncmp(param, "id=", 3) == 0) {
                strncpy(id_str, param + 3, sizeof(id_str) - 1);
                found = true;
                break;
            }
            param = strtok(NULL, "&");
        }
    }

    if (!found) {
        int total_len = req->content_len;
        if (total_len > 0 && total_len < SCRATCH_BUFSIZE) {
            char *buf = (char *)malloc(SCRATCH_BUFSIZE);
            if (buf) {
                int received = 0, cur_len = 0;
                while (cur_len < total_len) {
                    received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
                    if (received <= 0) break;
                    cur_len += received;
                }
                buf[total_len] = '\0';

                cJSON *root = cJSON_Parse(buf);
                if (root) {
                    cJSON *id_item = cJSON_GetObjectItem(root, "id");
                    if (id_item && id_item->valuestring) {
                        strncpy(id_str, id_item->valuestring, sizeof(id_str) - 1);
                        found = true;
                    }
                    cJSON_Delete(root);
                }
                free(buf);
            }
        }
    }

    if (!found) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing device id");
        return ESP_FAIL;
    }

    bridged_device_t *dev = device_registry_get_by_id(id_str);
    if (!dev || dev->matter_endpoint_id == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "device not found");
        return ESP_FAIL;
    }

    pending_command_t commands[MAX_PENDING_COMMANDS];
    int count = device_registry_get_commands(dev->matter_endpoint_id, commands, MAX_PENDING_COMMANDS);

    httpd_resp_set_type(req, "application/json");
    cJSON *resp = cJSON_CreateObject();
    cJSON *cmd_array = cJSON_AddArrayToObject(resp, "commands");
    for (int i = 0; i < count; i++) {
        cJSON *cmd = cJSON_CreateObject();
        cJSON_AddStringToObject(cmd, "cluster", commands[i].cluster);
        cJSON_AddStringToObject(cmd, "command", commands[i].command);
        cJSON_AddStringToObject(cmd, "data", commands[i].data);
        cJSON_AddItemToArray(cmd_array, cmd);
    }
    const char *resp_str = cJSON_Print(resp);
    httpd_resp_sendstr(req, resp_str);
    free((void *)resp_str);
    cJSON_Delete(resp);

    return ESP_OK;
}

static esp_err_t device_info_handler(httpd_req_t *req)
{
    char id_str[MAX_DEVICE_ID_LEN] = {0};
    char query[256] = {0};
    bool found = false;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char *param = strtok(query, "&");
        while (param) {
            if (strncmp(param, "id=", 3) == 0) {
                strncpy(id_str, param + 3, sizeof(id_str) - 1);
                found = true;
                break;
            }
            param = strtok(NULL, "&");
        }
    }

    if (!found) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing device id");
        return ESP_FAIL;
    }

    bridged_device_t *dev = device_registry_get_by_id(id_str);
    if (!dev) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "device not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "id", dev->id);
    cJSON_AddStringToObject(resp, "name", dev->name);
    cJSON_AddStringToObject(resp, "type", device_type_to_string(dev->type));
    cJSON_AddNumberToObject(resp, "endpoint_id", dev->matter_endpoint_id);
    cJSON_AddBoolToObject(resp, "online", dev->online);
    const char *resp_str = cJSON_Print(resp);
    httpd_resp_sendstr(req, resp_str);
    free((void *)resp_str);
    cJSON_Delete(resp);

    return ESP_OK;
}

static esp_err_t devices_list_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *resp = cJSON_CreateObject();
    cJSON *dev_array = cJSON_AddArrayToObject(resp, "devices");

    int count = 0;
    bridged_device_t *devices = device_registry_get_all(&count);

    for (int i = 0; i < count; i++) {
        if (devices[i].registered) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "id", devices[i].id);
            cJSON_AddStringToObject(item, "name", devices[i].name);
            cJSON_AddStringToObject(item, "type", device_type_to_string(devices[i].type));
            cJSON_AddNumberToObject(item, "endpoint_id", devices[i].matter_endpoint_id);
            cJSON_AddBoolToObject(item, "online", devices[i].online);
            cJSON_AddItemToArray(dev_array, item);
        }
    }

    const char *resp_str = cJSON_Print(resp);
    httpd_resp_sendstr(req, resp_str);
    free((void *)resp_str);
    cJSON_Delete(resp);

    return ESP_OK;
}

esp_err_t wifi_server_start(void)
{
    if (s_server) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 8;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t register_uri = {
        .uri = "/api/device/register",
        .method = HTTP_POST,
        .handler = register_device_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &register_uri);

    httpd_uri_t state_uri = {
        .uri = "/api/device/state",
        .method = HTTP_POST,
        .handler = device_state_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &state_uri);

    httpd_uri_t commands_uri = {
        .uri = "/api/device/commands",
        .method = HTTP_GET,
        .handler = device_commands_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &commands_uri);

    httpd_uri_t commands_post_uri = {
        .uri = "/api/device/commands",
        .method = HTTP_POST,
        .handler = device_commands_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &commands_post_uri);

    httpd_uri_t info_uri = {
        .uri = "/api/device/info",
        .method = HTTP_GET,
        .handler = device_info_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &info_uri);

    httpd_uri_t list_uri = {
        .uri = "/api/devices",
        .method = HTTP_GET,
        .handler = devices_list_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &list_uri);

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = [](httpd_req_t *r) -> esp_err_t {
            httpd_resp_set_type(r, "application/json");
            httpd_resp_sendstr(r, "{\"service\":\"esp-matter-bridge\",\"version\":\"1.0\"}");
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &root_uri);

    ESP_LOGI(TAG, "HTTP REST server started on port %d", config.server_port);
    return ESP_OK;
}

esp_err_t wifi_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
    return ESP_OK;
}

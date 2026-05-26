#include "app_wifi_server.h"
#include "app_bridge.h"
#include "app_device_registry.h"
#include "app_onboarding.h"
#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <string.h>
#include <esp_heap_caps.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "wifi_server";
static httpd_handle_t s_server = NULL;

#define SCRATCH_BUFSIZE 8192

// UDP Discovery
#define DISCOVERY_PORT 5000
#define BROADCAST_INTERVAL_US (10 * 1000000) // 10 seconds

static TaskHandle_t s_udp_task = NULL;
static int s_udp_socket = -1;

static char s_bridge_ip[16] = "0.0.0.0";

void wifi_server_update_ip(const char *ip)
{
    strncpy(s_bridge_ip, ip, sizeof(s_bridge_ip) - 1);
    s_bridge_ip[sizeof(s_bridge_ip) - 1] = '\0';
    ESP_LOGI(TAG, "Cached bridge IP updated: %s", s_bridge_ip);
}

static void handle_udp_discovery(void)
{
    struct sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);
    char *buf = (char *)malloc(512);
    if (!buf) return;

    int len = recvfrom(s_udp_socket, buf, 511, MSG_DONTWAIT,
                       (struct sockaddr *)&src_addr, &addr_len);
    if (len > 0) {
        buf[len] = '\0';

        bool is_discovery = false;
        cJSON *root = cJSON_Parse(buf);
        if (root) {
            cJSON *svc = cJSON_GetObjectItem(root, "service");
            cJSON *disc = cJSON_GetObjectItem(root, "discover");
            if (svc && svc->valuestring &&
                (strcmp(svc->valuestring, "mqtt-bridge") == 0 ||
                 strcmp(svc->valuestring, "esp-matter-bridge") == 0) &&
                disc && cJSON_IsTrue(disc)) {
                is_discovery = true;
            }
            cJSON_Delete(root);
        }

        if (is_discovery && strcmp(s_bridge_ip, "0.0.0.0") != 0) {
            char resp[256];
            snprintf(resp, sizeof(resp),
                "{\"service\":\"esp-matter-bridge\",\"ip_sta\":\"%s\",\"http_port\":80}",
                s_bridge_ip);
            sendto(s_udp_socket, resp, strlen(resp), 0,
                   (struct sockaddr *)&src_addr, addr_len);
            ESP_LOGI(TAG, "UDP discovery response sent to %s", inet_ntoa(src_addr.sin_addr));
        } else if (is_discovery) {
            ESP_LOGD(TAG, "UDP discovery request from %s (no IP yet, ignored)",
                     inet_ntoa(src_addr.sin_addr));
        }
    }

    free(buf);
}

static void send_udp_broadcast(void)
{
    if (strcmp(s_bridge_ip, "0.0.0.0") == 0) return;

    char resp[256];
    snprintf(resp, sizeof(resp),
        "{\"service\":\"esp-matter-bridge\",\"ip_sta\":\"%s\",\"http_port\":80}",
        s_bridge_ip);

    struct sockaddr_in bcast_addr;
    bcast_addr.sin_family = AF_INET;
    bcast_addr.sin_port = htons(DISCOVERY_PORT);
    bcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(s_udp_socket, resp, strlen(resp), 0,
           (struct sockaddr *)&bcast_addr, sizeof(bcast_addr));
    ESP_LOGD(TAG, "UDP broadcast sent: IP %s", s_bridge_ip);
}

static void udp_discovery_task(void *pv)
{
    s_udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_udp_socket < 0) {
        ESP_LOGE(TAG, "Failed to create UDP discovery socket");
        vTaskDelete(NULL);
        return;
    }

    int broadcast_enable = 1;
    setsockopt(s_udp_socket, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DISCOVERY_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s_udp_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind UDP socket to port %d", DISCOVERY_PORT);
        close(s_udp_socket);
        s_udp_socket = -1;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UDP discovery listening on port %d", DISCOVERY_PORT);

    {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("STA_DEF");
        if (netif) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
                snprintf(s_bridge_ip, sizeof(s_bridge_ip), IPSTR, IP2STR(&ip_info.ip));
                ESP_LOGI(TAG, "UDP discovery using cached IP: %s", s_bridge_ip);
            }
        }
    }

    int64_t last_broadcast = 0;
    while (1) {
        handle_udp_discovery();

        int64_t now = esp_timer_get_time();
        if (now - last_broadcast >= BROADCAST_INTERVAL_US) {
            last_broadcast = now;
            send_udp_broadcast();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void stop_udp_discovery(void)
{
    if (s_udp_task) {
        vTaskDelete(s_udp_task);
        s_udp_task = NULL;
    }
    if (s_udp_socket >= 0) {
        close(s_udp_socket);
        s_udp_socket = -1;
    }
}

static esp_err_t start_udp_discovery(void)
{
    if (s_udp_task) return ESP_OK;
    BaseType_t ret = xTaskCreate(udp_discovery_task, "udp_disc", 3072, NULL, 5, &s_udp_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UDP discovery task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

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

static esp_err_t commissioning_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "pin", 20202021);
    cJSON_AddNumberToObject(resp, "discriminator", 3840);
    cJSON_AddStringToObject(resp, "manual_code", onboarding_get_manual_code());
    cJSON_AddStringToObject(resp, "qr_code_payload", onboarding_get_qr_payload());
    const char *resp_str = cJSON_Print(resp);
    httpd_resp_sendstr(req, resp_str);
    free((void *)resp_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

static esp_err_t ping_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t bridge_info_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "ip", s_bridge_ip);
    cJSON_AddNumberToObject(resp, "uptime_s", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(resp, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(resp, "min_free_heap", esp_get_minimum_free_heap_size());
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

            const char *state_json = device_registry_get_state_json(devices[i].id);
            if (strcmp(state_json, "{}") != 0) {
                cJSON *state = cJSON_Parse(state_json);
                if (state) {
                    cJSON_AddItemToObject(item, "state", state);
                }
            }

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
    config.max_uri_handlers = 10;

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

    httpd_uri_t bridge_info_uri = {
        .uri = "/api/bridge/info",
        .method = HTTP_GET,
        .handler = bridge_info_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &bridge_info_uri);

    httpd_uri_t commissioning_uri = {
        .uri = "/api/bridge/commissioning",
        .method = HTTP_GET,
        .handler = commissioning_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &commissioning_uri);

    httpd_uri_t ping_uri = {
        .uri = "/api/ping",
        .method = HTTP_GET,
        .handler = ping_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &ping_uri);

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = [](httpd_req_t *r) -> esp_err_t {
            const char *html = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP-Matter Bridge</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,-apple-system,sans-serif;background:#F4F7FC;color:#2C3E50;padding:16px}
h1{font-size:1.2rem;color:#B2CEfE;margin-bottom:4px}
.sub{color:#7A8BA3;font-size:.8rem;margin-bottom:16px}
.card{background:#FFFFFF;border-radius:12px;padding:16px;margin-bottom:12px}
.card h2{font-size:.9rem;color:#B2CEfE;margin-bottom:8px}
.row{display:flex;justify-content:space-between;padding:6px 0;font-size:.85rem;border-bottom:1px solid #E8EEF8}
.row:last-child{border:none}
.row .label{color:#7A8BA3}
.row .value{color:#2C3E50}
.badge{display:inline-block;padding:2px 10px;border-radius:99px;font-size:.75rem;font-weight:600}
.badge.on{background:#E8F5E9;color:#2E7D32}
.badge.off{background:#FFEBEE;color:#C62828}
.device{margin-bottom:8px;padding:10px;background:#FFFFFF;border-radius:8px;border:1px solid #E8EEF8}
.device:last-child{margin-bottom:0}
.dev-name{font-weight:600;font-size:.9rem;color:#2C3E50}
.dev-meta{font-size:.75rem;color:#7A8BA3;margin:2px 0 4px}
.dev-state{font-size:.82rem;color:#7A8BA3}
.state-on{color:#2E7D32;font-weight:600}
.state-off{color:#C62828;font-weight:600}
.led{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px;vertical-align:middle}
.led.on{background:#2E7D32;box-shadow:0 0 6px #2E7D32}
.led.off{background:#8FA0B8}
.empty{text-align:center;padding:40px 16px;color:#7A8BA3}
</style>
</head>
<body>
<h1>ESP-Matter Bridge</h1>
<div class="sub" id="sub">carregando...</div>
<div class="card">
<h2>Bridge</h2>
<div id="bridge-info"><div class="row"><span class="label">IP</span><span class="value">—</span></div></div>
</div>
<div class="card">
<h2>Dispositivos <span id="dev-count" style="color:#7A8BA3;font-weight:400"></span></h2>
<div id="devices"><div class="empty">carregando...</div></div>
</div>
<script>
async function load(){try{
let r=await fetch('/api/bridge/info');
let b=await r.json();
let u=b.uptime_s|0;
let uptime=''+(u>86400?Math.floor(u/86400)+'d ':'')+Math.floor((u%86400)/3600)+'h'+Math.floor((u%3600)/60)+'m'+u%60+'s';
document.getElementById('bridge-info').innerHTML=
'<div class="row"><span class="label">IP</span><span class="value">'+b.ip+'</span></div>'+
'<div class="row"><span class="label">Uptime</span><span class="value">'+uptime+'</span></div>'+
'<div class="row"><span class="label">Heap livre</span><span class="value">'+(b.free_heap/1024).toFixed(0)+' KB</span></div>';
document.getElementById('sub').textContent='IP: '+b.ip;
}catch(e){document.getElementById('bridge-info').innerHTML='<div class="row"><span class="label" style="color:#C62828">Erro de conex\u00E3o</span></div>'}

try{
let r=await fetch('/api/devices');
let d=await r.json();
let devs=d.devices||[];
let h='';
let on=0;
for(let dev of devs){
if(dev.online)on++;
let st=dev.state||{};
let stHtml='';
if('on' in st)stHtml='<span class="'+(st.on?'state-on':'state-off')+'">'+(st.on?'LIGADO':'DESLIGADO')+'</span>';
else if('temperature' in st)stHtml=st.temperature.toFixed(1)+'\u00B0C '+(st.humidity!=null?st.humidity.toFixed(0)+'%':'');
else if('contact' in st)stHtml=st.contact?'ABERTO':'FECHADO';
else if('occupancy' in st)stHtml=st.occupancy?'MOVIMENTO':'LIVRE';
else stHtml='<span style="color:#7A8BA3">—</span>';
h+='<div class="device">'+
'<div><span class="led '+(dev.online?'on':'off')+'"></span><span class="dev-name">'+dev.name+'</span> <span class="badge '+(dev.online?'on':'off')+'">'+(dev.online?'online':'offline')+'</span></div>'+
'<div class="dev-meta">'+dev.type+' &middot; ep '+dev.endpoint_id+'</div>'+
'<div class="dev-state">'+stHtml+'</div></div>';
}
document.getElementById('devices').innerHTML=h||'<div class="empty">Nenhum dispositivo registrado</div>';
document.getElementById('dev-count').textContent='('+on+'/'+devs.length+' online)';
}catch(e){document.getElementById('devices').innerHTML='<div class="empty" style="color:#C62828">Erro ao carregar dispositivos</div>'}
}
load();setInterval(load,5000);
</script>
</body>
</html>
)rawliteral";
            httpd_resp_set_type(r, "text/html");
            httpd_resp_sendstr(r, html);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &root_uri);

    err = start_udp_discovery();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start UDP discovery (non-fatal)");
    }

    ESP_LOGI(TAG, "HTTP REST server started on port %d", config.server_port);
    return ESP_OK;
}

esp_err_t wifi_server_stop(void)
{
    stop_udp_discovery();
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
    return ESP_OK;
}

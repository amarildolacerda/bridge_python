#include "app_wifi_config.h"
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <cJSON.h>
#include <string.h>

static const char *TAG = "wifi_config";
static httpd_handle_t s_config_server = NULL;

#define CONFIG_AP_SSID "Bridge_Config"
#define CONFIG_AP_MAX_CONN 4

static const char *CONFIG_HTML =
    "<!DOCTYPE html>"
    "<html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Bridge WiFi Config</title>"
    "<style>"
    "body{font-family:sans-serif;margin:20px;background:#f5f5f5}"
    ".card{background:#fff;border-radius:8px;padding:20px;max-width:400px;margin:0 auto;box-shadow:0 2px 8px rgba(0,0,0,0.1)}"
    "h2{color:#333;margin-top:0}"
    "label{display:block;margin:12px 0 4px;color:#555;font-size:14px}"
    "input{width:100%;padding:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box;font-size:16px}"
    "button{width:100%;padding:12px;margin-top:16px;background:#007bff;color:#fff;border:none;border-radius:4px;font-size:16px;cursor:pointer}"
    "button:hover{background:#0056b3}"
    ".msg{padding:10px;margin-top:12px;border-radius:4px;display:none}"
    ".ok{background:#d4edda;color:#155724;display:block}"
    ".err{background:#f8d7da;color:#721c24;display:block}"
    "</style>"
    "</head><body>"
    "<div class='card'>"
    "<h2>Configurar WiFi</h2>"
    "<form id='wifiForm'>"
    "<label>SSID</label>"
    "<input type='text' id='ssid' placeholder='Nome da rede WiFi' required>"
    "<label>Senha</label>"
    "<input type='password' id='password' placeholder='Senha da rede WiFi'>"
    "<button type='submit'>Salvar e Conectar</button>"
    "</form>"
    "<div id='msg' class='msg'></div>"
    "</div>"
    "<script>"
    "document.getElementById('wifiForm').onsubmit=async function(e){"
    "e.preventDefault();"
    "const msg=document.getElementById('msg');"
    "msg.className='msg';msg.style.display='none';"
    "const ssid=document.getElementById('ssid').value;"
    "const password=document.getElementById('password').value;"
    "try{"
    "const r=await fetch('/api/wifi/configure',{"
    "method:'POST',"
    "headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({ssid,password})"
    "});"
    "const d=await r.json();"
    "if(d.status=='ok'){"
    "msg.className='msg ok';msg.textContent='Configurado! Reiniciando...';msg.style.display='block';"
    "setTimeout(()=>{window.location.href='/'},5000);"
    "}else{"
    "msg.className='msg err';msg.textContent='Erro: '+d.error;msg.style.display='block';"
    "}"
    "}catch(e){"
    "msg.className='msg err';msg.textContent='Erro de conexao';msg.style.display='block';"
    "}"
    "};"
    "</script>"
    "</body></html>";

static esp_err_t handle_config_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, CONFIG_HTML, strlen(CONFIG_HTML));
    return ESP_OK;
}

static esp_err_t handle_config_post(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty request");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *ssid_json = cJSON_GetObjectItem(root, "ssid");
    cJSON *password_json = cJSON_GetObjectItem(root, "password");

    if (!ssid_json || !ssid_json->valuestring || strlen(ssid_json->valuestring) == 0) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }

    const char *ssid = ssid_json->valuestring;
    const char *password = password_json && password_json->valuestring
                           ? password_json->valuestring : "";

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    if (strlen(password) > 0) {
        strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
    }

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        const char *resp = "{\"status\":\"error\",\"error\":\"Failed to save config\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_OK;
    }

    const char *resp = "{\"status\":\"ok\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));

    ESP_LOGI(TAG, "WiFi config saved. SSID: %s", ssid);

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

esp_err_t wifi_config_portal_start(void)
{
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (!ap_netif) {
        ESP_LOGE(TAG, "Failed to create AP netif");
        return ESP_FAIL;
    }

    esp_wifi_set_mode(WIFI_MODE_APSTA);

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = CONFIG_AP_SSID,
            .ssid_len = strlen(CONFIG_AP_SSID),
            .authmode = WIFI_AUTH_OPEN,
            .max_connection = CONFIG_AP_MAX_CONN,
        },
    };
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);

    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "AP started: %s", CONFIG_AP_SSID);

    httpd_config_t httpd_cfg = HTTPD_DEFAULT_CONFIG();
    httpd_cfg.server_port = 80;
    httpd_cfg.max_uri_handlers = 8;

    err = httpd_start(&s_config_server, &httpd_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t uri_get = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handle_config_get,
    };
    httpd_register_uri_handler(s_config_server, &uri_get);

    httpd_uri_t uri_post = {
        .uri = "/api/wifi/configure",
        .method = HTTP_POST,
        .handler = handle_config_post,
    };
    httpd_register_uri_handler(s_config_server, &uri_post);

    ESP_LOGI(TAG, "Config portal: http://192.168.4.1");
    return ESP_OK;
}

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_wifi.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include <common_macros.h>
#include <app_bridge.h>
#include <app_wifi_server.h>
#include <app_device_registry.h>
#include <app_priv.h>
#include <app_reset.h>
#include <app_onboarding.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

#include <setup_payload/ManualSetupPayloadGenerator.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>

#include <string>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;

#if CONFIG_ENABLE_ESP_INSIGHTS_TRACE
extern const char insights_auth_key_start[] asm("_binary_insights_auth_key_txt_start");
extern const char insights_auth_key_end[] asm("_binary_insights_auth_key_txt_end");
#endif

using chip::RendezvousInformationFlag;

static char s_manual_code[32] = {0};
static char s_qr_payload[256] = {0};

// Mutex for protecting Matter operations
static SemaphoreHandle_t xMatterMutex = NULL;

const char *onboarding_get_manual_code(void) { return s_manual_code; }
const char *onboarding_get_qr_payload(void) { return s_qr_payload; }
void onboarding_set_manual_code(const char *code) { strncpy(s_manual_code, code, sizeof(s_manual_code) - 1); }
void onboarding_set_qr_payload(const char *payload) { strncpy(s_qr_payload, payload, sizeof(s_qr_payload) - 1); }

static void print_onboarding_codes(void)
{
    chip::SetupPayload payload;
    payload.version = 0;
    payload.setUpPINCode = 20202021;
    payload.discriminator.SetLongValue(3840);
    payload.vendorID = 0xFFF1;
    payload.productID = 0x8000;
    payload.commissioningFlow = chip::CommissioningFlow::kStandard;
    payload.rendezvousInformation.SetValue(chip::RendezvousInformationFlags(RendezvousInformationFlag::kBLE));

    std::string code;
    CHIP_ERROR err = chip::ManualSetupPayloadGenerator(payload).payloadDecimalStringRepresentation(code);
    if (err == CHIP_NO_ERROR) {
        onboarding_set_manual_code(code.c_str());
        ESP_LOGI(TAG, "═══════════════════════════════════════════════");
        ESP_LOGI(TAG, "  Código manual (digite no Google Home): %s", s_manual_code);
        ESP_LOGI(TAG, "═══════════════════════════════════════════════");
    }

    err = chip::QRCodeSetupPayloadGenerator(payload).payloadBase38Representation(code);
    if (err == CHIP_NO_ERROR) {
        onboarding_set_qr_payload(code.c_str());
        ESP_LOGI(TAG, "Payload QR Code: %s", s_qr_payload);
        ESP_LOGI(TAG, "Gere a imagem em: https://www.qr-code-generator.com/");
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA iniciado — aguardando conexão...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *data = (wifi_event_sta_connected_t *)event_data;
        char ssid[33] = {0};
        memcpy(ssid, data->ssid, data->ssid_len);
        ESP_LOGI(TAG, "Conectado ao WiFi: %s (canal %d)", ssid, data->channel);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi desconectado — tentando reconectar...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *data = (ip_event_got_ip_t *)event_data;
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&data->ip_info.ip));
        wifi_server_update_ip(ip_str);
        ESP_LOGI(TAG, "═══════════════════════════════════════════════");
        ESP_LOGI(TAG, "  IP obtido: %s", ip_str);
        ESP_LOGI(TAG, "  Acesse: http://%s", ip_str);
        ESP_LOGI(TAG, "═══════════════════════════════════════════════");

        // HTTP server + onboarding codes now that WiFi is connected
        esp_err_t err = wifi_server_start();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "HTTP server started");
        } else {
            ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        }
        
        print_onboarding_codes();
    }
}

extern "C" void app_main()
{
    // Add delay to let system stabilize before starting services
    vTaskDelay(pdMS_TO_TICKS(2000));

    esp_err_t err = ESP_OK;

    // Initialize NVS
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        return;
    }

    // Create mutex for protecting Matter operations
    xMatterMutex = xSemaphoreCreateMutex();
    if (xMatterMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create Matter mutex");
        return;
    }

    esp_netif_init();
    esp_event_loop_create_default();

    device_registry_init();

    // Initialize Matter stack BEFORE WiFi — enables BLE commissioning
    // The Matter stack manages WiFi provisioning via commissioning flow
    ESP_LOGI(TAG, "Initializing Matter bridge...");
    err = bridge_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Matter bridge initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to initialize bridge: %s", esp_err_to_name(err));
    }

    // Register reset button
    app_reset_button_register();

    // Register event handlers BEFORE starting WiFi
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    // Matter's InitWiFiStack() already created netif and called esp_wifi_init()
    // We just need to set mode and start WiFi
    ESP_LOGI(TAG, "Starting WiFi...");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "ESP-Matter Bridge ready — aguardando WiFi via commissioning...");
    ESP_LOGI(TAG, "Setup PIN: 20202021, Discriminator: 3840");
    print_onboarding_codes();
}
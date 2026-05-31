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

static SemaphoreHandle_t xMatterMutex = NULL;

static SemaphoreHandle_t s_wifi_got_ip = NULL;

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

static void open_commissioning_window_work(intptr_t)
{
    ESP_LOGI(TAG, "Reabrindo janela de comissionamento por 60s");
    chip::Server::GetInstance().GetCommissioningWindowManager().OpenBasicCommissioningWindow(
        chip::System::Clock::Seconds32(60));
    print_onboarding_codes();
}

esp_err_t bridge_start_commissioning(void)
{
    ESP_LOGI(TAG, "bridge_start_commissioning called");
    chip::DeviceLayer::PlatformMgr().ScheduleWork(open_commissioning_window_work, 0);
    return ESP_OK;
}

static void disable_ble_commissioning(intptr_t)
{
    ESP_LOGI(TAG, "Desligando advertising BLE — aguardando WiFi");
    chip::DeviceLayer::ConnectivityMgr().SetBLEAdvertisingEnabled(false);
}

static void enable_ble_commissioning(intptr_t)
{
    if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
        ESP_LOGI(TAG, "WiFi conectado — habilitando BLE para commissioning");
        chip::Server::GetInstance().GetCommissioningWindowManager().OpenBasicCommissioningWindow(
            chip::System::Clock::Seconds32(300));
        print_onboarding_codes();
    } else {
        ESP_LOGI(TAG, "Bridge já comissionado — BLE desnecessário");
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

        esp_err_t err = wifi_server_start();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "HTTP server started");
        } else {
            ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        }

        if (s_wifi_got_ip) {
            xSemaphoreGive(s_wifi_got_ip);
        }

        chip::DeviceLayer::PlatformMgr().ScheduleWork(enable_ble_commissioning, 0);
    }
}

extern "C" void app_main()
{
    vTaskDelay(pdMS_TO_TICKS(2000));

    esp_err_t err = ESP_OK;

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        return;
    }

    xMatterMutex = xSemaphoreCreateMutex();
    if (xMatterMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create Matter mutex");
        return;
    }

    esp_netif_init();
    esp_event_loop_create_default();

    device_registry_init();

    ESP_LOGI(TAG, "Initializing Matter bridge...");
    err = bridge_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Matter bridge initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to initialize bridge: %s", esp_err_to_name(err));
    }

    chip::DeviceLayer::PlatformMgr().ScheduleWork(disable_ble_commissioning, 0);

    app_reset_button_register();

    s_wifi_got_ip = xSemaphoreCreateBinary();

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    ESP_LOGI(TAG, "Starting WiFi...");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Aguardando conexão WiFi (15s timeout)...");
    if (xSemaphoreTake(s_wifi_got_ip, pdMS_TO_TICKS(15000)) == pdTRUE) {
        ESP_LOGI(TAG, "WiFi conectado antes de habilitar BLE commissioning");
    } else {
        ESP_LOGW(TAG, "WiFi não conectou em 15s — habilitando BLE commissioning diretamente");
        chip::DeviceLayer::PlatformMgr().ScheduleWork(enable_ble_commissioning, 0);
    }

    ESP_LOGI(TAG, "ESP-Matter Bridge ready — Setup PIN: 20202021, Discriminator: 3840");
}

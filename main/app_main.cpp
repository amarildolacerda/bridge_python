#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_event.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include <common_macros.h>
#include <app_bridge.h>
#include <app_wifi_server.h>
#include <app_device_registry.h>
#include <app_priv.h>
#include <app_reset.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;

#if CONFIG_ENABLE_ESP_INSIGHTS_TRACE
extern const char insights_auth_key_start[] asm("_binary_insights_auth_key_txt_start");
extern const char insights_auth_key_end[] asm("_binary_insights_auth_key_txt_end");
#endif

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    nvs_flash_init();

    esp_netif_init();
    esp_event_loop_create_default();

    device_registry_init();

    err = bridge_init();
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to initialize bridge, err:%d", err));

#if CONFIG_ENABLE_ESP_INSIGHTS_TRACE
    enable_insights(insights_auth_key_start);
#endif

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
#endif

    err = wifi_server_start();
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start HTTP server, err:%d", err));

    ESP_LOGI(TAG, "ESP-Matter Bridge started");
    ESP_LOGI(TAG, "Setup PIN: 20202021, Discriminator: 3840");
}

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

// CORRETO: incluir apenas o componente principal
#include "esp_matter.h"
#include "esp_matter_core.h"

static const char *TAG = "matter_bridge";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Iniciando Matter Bridge...");

    // Inicializar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Inicializar rede
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Configurar o Matter node
    esp_matter::node::config_t node_config;
    esp_matter::node::create(&node_config, nullptr, nullptr);

    ESP_LOGI(TAG, "Matter Bridge iniciado com sucesso!");
}
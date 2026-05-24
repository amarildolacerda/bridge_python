#include <esp_log.h>
#include <esp_matter.h>
#include <app_priv.h>

static const char *TAG = "app_driver";

using namespace chip::app::Clusters;

app_driver_handle_t app_driver_button_init(void)
{
    return NULL;
}

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    return ESP_OK;
}

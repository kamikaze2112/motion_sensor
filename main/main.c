#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"     // ESP_LOGI etc.
#include "esp_event.h"   // esp_event_loop_create_default()
#include "nvs_flash.h"   // nvs_flash_init(), nvs_flash_erase()
#include "wifi.h"        // wifi_setup()
#include "radar.h"
#include "mqtt.h"

void app_main(void) {

esp_err_t ret = nvs_flash_init();  // Init NVS for wifi
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
ESP_ERROR_CHECK(ret);

ESP_ERROR_CHECK(esp_event_loop_create_default());


mqtt_setup();
wifi_setup();
radar_setup();

}

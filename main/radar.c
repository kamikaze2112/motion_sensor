#include "radar.h"
#include "mqtt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "freertos/queue.h"
#include "esp_log.h"

#define RADAR_GPIO GPIO_NUM_1

static const char *TAG = "motion/radar";

static QueueHandle_t radar_queue;

static void IRAM_ATTR radar_isr_handler(void *args) {

    int level = gpio_get_level(RADAR_GPIO);
    BaseType_t woken = pdFALSE;

    xQueueSendFromISR(radar_queue, &level, &woken);
    portYIELD_FROM_ISR(woken);
    
}

static void radar_task(void *arg) {

    int level;

    while(1) {
        if(xQueueReceive(radar_queue, &level, portMAX_DELAY) == pdTRUE) {
            mqtt_state(level);
        }
    }
}

bool radar_presence() {
    return gpio_get_level(RADAR_GPIO);
}

void radar_setup() {

    ESP_LOGI(TAG, "radar_setup()");

    gpio_config_t radar_cfg = {
        .pin_bit_mask   = 1ULL << RADAR_GPIO,
        .mode           = GPIO_MODE_INPUT,
        .pull_down_en   = GPIO_PULLDOWN_ENABLE,
        .pull_up_en     = GPIO_PULLUP_DISABLE,
        .intr_type      = GPIO_INTR_ANYEDGE,
    };

    ESP_ERROR_CHECK(gpio_config(&radar_cfg));

    radar_queue = xQueueCreate(4, sizeof(int));
    xTaskCreate(radar_task, "radar", 4096, NULL, 5, NULL);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(RADAR_GPIO, radar_isr_handler, NULL);

    ESP_LOGI(TAG, "Radar setup complete.");

}
#include "mqtt.h"
#include <stdbool.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "secrets.h"
#include "esp_mac.h"
#include <stdio.h>
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include "esp_ota_ops.h"    // esp_ota_get_running_partition(), esp_partition_t

#define HA_STATUS_TOPIC "homeassistant/status"

static const char *TAG = "motion/mqtt";
static char device_uid[16];
static char status_topic[32];
static char presence_topic[48];
static char ota_set[48];

static esp_mqtt_client_handle_t mqtt_handle;
static bool client_started = false;

static void mqtt_publish(const char *topic, const char *payload, int qos, int retain) {

    int msg_id = esp_mqtt_client_publish(mqtt_handle, topic, payload, 0, qos, retain);

    if (msg_id == -1) {
        ESP_LOGE(TAG, "Publish failed");
    } else if (msg_id == -2) {
        ESP_LOGE(TAG, "Publish failed, outbox full.");
    } else {
        ESP_LOGI(TAG, "Publish Success: %d", msg_id);
    }

}

static void mqtt_subscribe(const char *topic) {

    int msg_id = esp_mqtt_client_subscribe_single(mqtt_handle, topic, 1);

    if (msg_id == -1) {
        ESP_LOGE(TAG, "Subscribe to %s failed", topic);
    }

}

static void mqtt_discovery() {

    char config_topic[64];
    char ota_id[40];
    char url[32];
    char presence_id[40];

    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();

    char fw[64];
    snprintf(fw, sizeof(fw), "%s (%s)", app->version, running->label);

    esp_netif_t *radio = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t addr_info = {0};
    if (radio != NULL) {
        esp_netif_get_ip_info(radio, &addr_info);
    }

    snprintf(url, sizeof(url), "http://" IPSTR, IP2STR(&addr_info.ip));

    snprintf(presence_id, sizeof(presence_id), "%s_presence", device_uid);
    snprintf(config_topic, sizeof(config_topic), "homeassistant/device/%s/config", device_uid);
    snprintf(ota_id, sizeof(ota_id), "%s_ota_id", device_uid);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "availability_topic", status_topic);
    
    cJSON *dev = cJSON_AddObjectToObject(root, "device");
    cJSON_AddStringToObject(dev, "identifiers", device_uid);
    cJSON_AddStringToObject(dev, "name", device_uid);
    cJSON_AddStringToObject(dev, "configuration_url", url);
    cJSON_AddStringToObject(dev, "sw_version", fw);

    cJSON *origin = cJSON_AddObjectToObject(root, "origin");
    cJSON_AddStringToObject(origin, "name", "motion-fw");

    cJSON *comps = cJSON_AddObjectToObject(root, "components");
    cJSON *presence = cJSON_AddObjectToObject(comps, "presence");
    cJSON_AddStringToObject(presence, "platform", "binary_sensor");
    cJSON_AddStringToObject(presence, "name", "Presence");
    cJSON_AddStringToObject(presence, "unique_id", presence_id);
    cJSON_AddStringToObject(presence, "state_topic", presence_topic);
    cJSON_AddStringToObject(presence, "device_class", "occupancy");

    cJSON *ota = cJSON_AddObjectToObject(comps, "ota");
    cJSON_AddStringToObject(ota, "platform", "button");
    cJSON_AddStringToObject(ota, "name", "OTA (Click visit to upload)");
    cJSON_AddStringToObject(ota, "unique_id", ota_id);
    cJSON_AddStringToObject(ota, "command_topic", ota_set);

    char *payload = cJSON_Print(root);
    ESP_LOGI(TAG, "%s", payload);

    mqtt_publish(config_topic, payload, 1, 1);

    cJSON_free(payload);
    cJSON_Delete(root);

}

static void mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {

    switch (event_id)
    {
    case MQTT_EVENT_CONNECTED: 
        {
            ESP_LOGI(TAG, "MQTT Connected");
            mqtt_publish(status_topic, "online", 1, 1);
            mqtt_discovery();

        }
        break;
    
    case MQTT_EVENT_DISCONNECTED:
        /* code */
        break;

    case MQTT_EVENT_DATA:
    /* code */
        break;

    default:
        break;
    }
}

void mqtt_state(bool present) {

    const char *state = present ? "ON" : "OFF";
    mqtt_publish(presence_topic, state, 1, 1);

}

void mqtt_setup() {

    ESP_LOGI(TAG, "mqtt_setup()");

    uint8_t device_mac[6];

    ESP_ERROR_CHECK(esp_read_mac(device_mac, ESP_MAC_WIFI_STA));
    snprintf(device_uid, sizeof(device_uid), "motion_%02x%02x%02x", device_mac[3], device_mac[4], device_mac[5]);

    snprintf(status_topic, sizeof(status_topic), "%s/status", device_uid);
    snprintf(presence_topic, sizeof(presence_topic), "%s/presence/state", device_uid);
    snprintf(ota_set, sizeof(ota_set), "%s/ota/set", device_uid);

    esp_mqtt_client_config_t client_config = {
        .broker = {
            .address = {
                .uri = MQTT_URI,
            },
        },
        .session = {
            .keepalive = 30,
            .last_will = {
                .topic = status_topic,
                .msg = "offline",
                .qos = 1,
                .retain = 1,
            },
        },
        .credentials = {
            .username = MQTT_USER,
            .authentication = {
                .password = MQTT_PASS,
            },
        },
    };

     mqtt_handle = esp_mqtt_client_init(&client_config);

     if (mqtt_handle == NULL) {
        ESP_LOGE(TAG, "MQTT Handle returned NULL");
        return;
     }
}
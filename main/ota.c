#include "ota.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define OTA_TIMEOUT_MS (3 * 60 * 1000)

static esp_err_t ota_page(httpd_req_t *req);
static esp_err_t ota_upload(httpd_req_t *req);

static esp_timer_handle_t ota_timer;

static const char *TAG = "motion/ota";

static httpd_handle_t httpd_handle;

static const httpd_uri_t ota_page_rule = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = ota_page,
    .user_ctx = NULL, 
};

static const httpd_uri_t ota_upload_rule = {
    .uri = "/update",
    .method = HTTP_POST,
    .handler = ota_upload,
    .user_ctx = NULL,
};

static const char ota_html[] = 
    "<!DOCTYPE html><html><body>"
    "<h1>OTA UPDATE</h1>"
       "<input type='file' id='fw' accept='.bin'>"
    "<button id='go'>Upload</button>"
    "<p id='msg'></p>"
    "<script>"
    "document.getElementById('go').onclick = function() {"
    "  var f = document.getElementById('fw').files[0];"
    "  var m = document.getElementById('msg');"
    "  if (!f) { m.textContent = 'Pick a file first'; return; }"
    "  m.textContent = 'Uploading...';"
    "  fetch('/update', {method: 'POST', body: f})"
    "    .then(function(r) { return r.text(); })"
    "    .then(function(t) { m.textContent = t; })"
    "    .catch(function() { m.textContent = 'Upload failed'; });"
    "};"
    "</script>"
    "</body></html>";

static esp_err_t ota_page(httpd_req_t *req) {
    httpd_resp_send(req, ota_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t ota_upload(httpd_req_t *req) {
    
    const esp_partition_t *slot = esp_ota_get_next_update_partition(NULL);

    esp_ota_handle_t ota_handle;

    if(slot == NULL) {
        ESP_LOGE(TAG, "NULL Slot, aborting.");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NULL Slot, aborting.");
        return ESP_FAIL;
    }

    esp_err_t err = esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);



    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open OTA slot for writing with error: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open OTA Slot.");
        return ESP_FAIL;
    }

    char chunk[1024];
    int left = req->content_len;
    int total = 0;



    while (left > 0) {
        int got = httpd_req_recv(req, chunk, sizeof(chunk));
        if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (got <= 0) {
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Read failed.");
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, chunk, got);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Write failed with error: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed.");
            return ESP_FAIL;
        }

        left -= got;
        total += got;
    }

    err = esp_ota_end(ota_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA FAILED: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA failed.");
        return ESP_FAIL;
    } else {
        err = esp_ota_set_boot_partition(slot);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set boot partition");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set boot partition.");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Received %d bytes", total);
        httpd_resp_sendstr(req, "done");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    
    return ESP_OK;

}

static void ota_timeout(void *arg) {
    ESP_LOGW(TAG, "OTA TIMEOUT");
    httpd_stop(httpd_handle);
}

void ota_start() {

    httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();

    esp_err_t err = httpd_start(&httpd_handle, &httpd_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTPD failed to start with error: %d", err);
        return;
    }

    const esp_timer_create_args_t args = {
        .callback = ota_timeout,
        .name = "ota_timeout",
    };
    
    if(ota_timer == NULL) {
        esp_timer_create(&args, &ota_timer);
    }
    
    esp_timer_start_once(ota_timer, OTA_TIMEOUT_MS * 1000ULL);

    httpd_register_uri_handler(httpd_handle, &ota_page_rule);
    httpd_register_uri_handler(httpd_handle, &ota_upload_rule);


}
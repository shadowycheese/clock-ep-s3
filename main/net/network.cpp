#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "../passwords.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_DISCONNECTED_BIT BIT1
#define WIFI_FAIL_BIT BIT2

static const char *TAG = "WIFI";
static EventGroupHandle_t _wifi_event_group;

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Got event: %d", event_id);

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "Starting connection process");

        xEventGroupSetBits(_wifi_event_group, WIFI_DISCONNECTED_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_WRONG_PASSWORD)
    {
        ESP_LOGE(TAG, "Wrong password");

        // xEventGroupSetBits(_wifi_event_group, WIFI_FAIL_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Disconnected, connecting to AP");

        xEventGroupSetBits(_wifi_event_group, WIFI_DISCONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG, "Connected: %d.%d.%d.%d", IP2STR(&event->ip_info.ip));

        xEventGroupSetBits(_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
}

void wifi_task()
{
    const int sntp_reties = 10;

    ESP_LOGI(TAG, "Initializing network interface");
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_LOGI(TAG, "Creating network event loop");
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "Creating WIFI STA");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_LOGI(TAG, "Initializing WIFI");
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_LOGI(TAG, "Adding WIFI event handlers");
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    ESP_LOGI(TAG, "Configuring WIFI authentication");
    wifi_sta_config_t sta_config = {};
    strncpy((char *)sta_config.ssid, WIFI_SSID, sizeof(sta_config.ssid));
    strncpy((char *)sta_config.password, WIFI_PASSPHRASE, sizeof(sta_config.password));
    wifi_config_t wifi_config = {};
    wifi_config.sta = sta_config;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_LOGI(TAG, "Starting WIFI");
    ESP_ERROR_CHECK(esp_wifi_start());

    for (;;)
    {
        ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");
        EventBits_t bits = xEventGroupWaitBits(_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_DISCONNECTED_BIT | WIFI_FAIL_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

        if ((bits & WIFI_DISCONNECTED_BIT) > 0)
        {
            ESP_LOGW(TAG, "Conecting...");

            esp_wifi_connect();

            continue;
        }

        if ((bits & WIFI_FAIL_BIT) > 0)
        {
            ESP_LOGE(TAG, "Wi-Fi Authentication failed");

            vTaskDelay(pdMS_TO_TICKS(5000));

            continue;
        }

        ESP_LOGI(TAG, "Wi-Fi Connected successfully.");
        initialize_sntp();

        int retry = 0;

        vTaskDelay(pdMS_TO_TICKS(2000));
        while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry <= sntp_reties)
        {
            ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, sntp_reties);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }

        char strftime_buf[64];
        time_t now;
        struct tm timeinfo;

        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);

        break;
    }
}

extern "C"
{
    void wifi_sync()
    {
        ESP_LOGI(TAG, "Starting WIFI...");

        _wifi_event_group = xEventGroupCreate();

        wifi_task();

        // xTaskCreatePinnedToCoreWithCaps(wifi_task, "wifi_task", 8192, NULL, 2, NULL, 1, MALLOC_CAP_INTERNAL);
    }
}
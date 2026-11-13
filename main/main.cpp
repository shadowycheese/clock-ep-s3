#include "driver/gpio.h"
#include "io/espio.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include "nvs_flash.h"
#include "nvs.h"
#include <stdio.h>
#include <esp_timer.h>
#include <esp_sleep.h>
#include "ep/ep.h"
#include "net/network.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "rtc/ds3231.h"

const char *APP_TAG = "APP";

EPaper display;

#define RTC_MAGIC 0xDA7EDA7E

#define ADC_ATTEN_SETTING ADC_ATTEN_DB_12 // 0 to 3.3V range
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_6
#define ERROR_DEEP_SLEEP_MICROS (5LL * 1000LL * 1000LL)

typedef struct
{
    int32_t sync_day;
    int32_t sync_hour;
    int32_t render_minute;
    uint32_t magic;
} rtc_state_t;

RTC_DATA_ATTR rtc_state_t rtc_state;

esp_err_t ensure_success(esp_err_t err)
{
    if (err != ESP_OK)
    {
        ESP_ERROR_CHECK_WITHOUT_ABORT(err);
    }

    return err;
}

void validate_state()
{
    if (rtc_state.magic != RTC_MAGIC)
    {
        memset(&rtc_state, 0, sizeof(rtc_state));

        rtc_state.magic = RTC_MAGIC;
        rtc_state.sync_day = -1;
        rtc_state.sync_hour = -1;
        rtc_state.render_minute = -1;
    }
}

extern "C" void app_main(void)
{
    validate_state();

    rtc_handle_t *handle = ds3231_init_full(GPIO_NUM_11, GPIO_NUM_12);

    if (handle == nullptr)
    {
        ESP_LOGE(APP_TAG, "No DS3231 detected");
    }
    else
    {
        if (ds3231_set_esp_with_rtc(handle) != 0)
        {
            ESP_LOGE(APP_TAG, "Failed to read DS3231");
        }
    }

    time_t now = time(NULL);

    int32_t day = (int32_t)(now / 86400LL);

    if (day != rtc_state.sync_day)
    {
        ensure_success(nvs_flash_init());

        wifi_sync();

        // Get time again after sync + update D3231
        now = time(NULL);

        if (handle != nullptr)
        {
            esp_err_t err = ds3231_time_time_t_set(handle, now);

            if (err == ESP_OK)
            {
                ESP_LOGI(APP_TAG, "Updated DS3231");
            }
            else
            {
                ESP_LOGE(APP_TAG, "Failed to update DS3231: %d", err);
            }
        }

        rtc_state.sync_day = (int32_t)(now / 86400LL);
        rtc_state.sync_hour = (int32_t)(now / 3600LL);
    }

    uint32_t min = now / 60;

    if (min != rtc_state.render_minute)
    {
        display.init();
        display.update(handle != nullptr, rtc_state.sync_hour);
        display.commit();
        display.close();

        rtc_state.render_minute = min;
    }

    now = time(NULL);

    uint64_t wake_up_time;

    if (now / 60 != min)
    {
        wake_up_time = 1000000LL;
    }
    else
    {
        time_t sec = now % 60;

        wake_up_time = 100000LL + ((60LL - sec) * 1000000LL);
    }

    uint32_t ms = (uint32_t)(wake_up_time / 1000L);

    ESP_LOGI(APP_TAG, "Wake up in: %ums", ms);

    ensure_success(esp_sleep_pd_config(ESP_PD_DOMAIN_RC_FAST, ESP_PD_OPTION_OFF));

    ensure_success(esp_sleep_enable_timer_wakeup(wake_up_time));

    esp_deep_sleep_start();
}

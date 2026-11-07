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

const char *APP_TAG = "APP";

EPaper display;

#define RTC_MAGIC 0xDA7EDA7E

typedef struct
{
    int32_t sync_day;
    int32_t sync_hour;
    int32_t render_minute;
    uint32_t magic;
} rtc_state_t;

RTC_DATA_ATTR rtc_state_t rtc_state;

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

float battery_level()
{
    return 3.2f + ((random() % 1000) / 1000.0f);
}

extern "C" void app_main(void)
{
    validate_state();

    time_t now = time(NULL);

    int32_t day = (int32_t)(now / 86400LL);

    if (day != rtc_state.sync_day)
    {
        nvs_flash_init();

        wifi_sync();

        now = time(NULL);

        rtc_state.sync_day = (int32_t)(now / 86400LL);
        rtc_state.sync_hour = (int32_t)(now / 3600LL);
    }

    uint32_t min = now / 60;

    if (min != rtc_state.render_minute)
    {
        display.init();
        display.update(battery_level(), rtc_state.sync_hour);
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

    esp_sleep_pd_config(ESP_PD_DOMAIN_RC_FAST, ESP_PD_OPTION_OFF);

    esp_sleep_enable_timer_wakeup(wake_up_time);

    esp_deep_sleep_start();
}

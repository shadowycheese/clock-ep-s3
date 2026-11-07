#ifndef EP_H
#define EP_H

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "ep/adafruit/Adafruit_GFX.h"
#include "epd_gdey042t81.h"

extern const GFXfont Montserrat_Bold12pt7b;
extern const GFXfont Montserrat_Bold30pt7b;
extern const GFXfont Montserrat_Bold70pt7b;
extern const GFXfont Montserrat_Regular12pt7b;
extern const GFXfont Montserrat_Regular30pt7b;
extern const GFXfont Montserrat_Regular48pt7b;

#define TIME_ZONE_OFFSET (-(7L * 3600L))

#define local_time() ((time_t)(time(NULL) + TIME_ZONE_OFFSET))

class EPaper : public GFXcanvas1
{
public:
    EPaper() : GFXcanvas1(EPD_WIDTH, EPD_HEIGHT)
    {
        _buffer_size = EPD_WIDTH * EPD_HEIGHT / 8;
    }

    void init()
    {
        buffer = (uint8_t *)heap_caps_malloc(_buffer_size, MALLOC_CAP_DEFAULT);
        partial_buffer = (uint8_t *)heap_caps_malloc(_buffer_size, MALLOC_CAP_DEFAULT);

        memset(buffer, 0xFF, _buffer_size);

        ESP_LOGI(TAG, "Initializing SPI for EPD...");
        esp_err_t ret = epd_spi_init();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to initialize SPI bus");
            return;
        }

        epd_init();
    }

    void close()
    {
        epd_sleep();
    }

    void update(float battery_level, int32_t sync_hour)
    {
        memset(buffer, 0xFF, _buffer_size);
        setTextColor(0);
        setTextWrap(false);

        char hh[4], mm[4], day[12], date[32], bat[20];

        time_t t = local_time();

        struct tm timeinfo;

        localtime_r(&t, &timeinfo);

        strftime(hh, 3, "%H", &timeinfo);
        strftime(mm, 3, "%M", &timeinfo);
        strftime(day, 12, "%A", &timeinfo);
        strftime(date, 32, "%d %b %Y", &timeinfo);

        int sw = string_width(day, &Montserrat_Bold30pt7b);
        setCursor((400 - sw) / 2, 44);
        setFont(&Montserrat_Bold30pt7b);
        printf("%s", day);

        sw = string_width(date, &Montserrat_Regular30pt7b);
        setCursor((400 - sw) / 2, 110);
        setFont(&Montserrat_Regular30pt7b);
        printf("%s", date);

        setCursor(0, 240);
        setFont(&Montserrat_Bold70pt7b);
        printf("%s", hh);
        int y = cursor_y;
        cursor_y -= 20;
        setFont(&Montserrat_Regular48pt7b);
        print(":");
        cursor_y = y;
        setFont(&Montserrat_Bold70pt7b);
        printf("%s", mm);

        snprintf(bat, 20, "Batt: %0.2fV", battery_level);
        sw = string_width(bat, &Montserrat_Regular12pt7b);
        setCursor(400 - (sw + 4), 290);
        setFont(&Montserrat_Regular12pt7b);
        printf("%s", bat);

        t = time(NULL);

        time_t sync_sec = ((time_t)((uint64_t)sync_hour) * 3600LL);
        time_t diff = t - sync_sec;
        uint32_t diff2 = (uint32_t)diff;
        uint32_t hrs = diff2 / 3600;

        ESP_LOGI(TAG, "t = %d, sync hour = %d, sync sec = %d, diff = %d, hours = %d", (uint32_t)t, sync_hour, (uint32_t)sync_sec,
                 diff2, hrs);

        if (hrs > 24)
        {
            snprintf(date, 32, "Sync %dh ago [!]", (int)hrs);
        }
        else if (hrs < 1)
        {
            snprintf(date, 32, "Sync < 1h ago");
        }
        else if (hrs == 1)
        {
            snprintf(date, 32, "Sync ~1h ago");
        }
        else
        {
            snprintf(date, 32, "Sync ~%dh ago", (int)hrs);
        }

        setCursor(4, 290);
        printf("%s", date);
    }

    int string_width(const char *str, const GFXfont *font)
    {
        int w = 0;

        while (*str)
        {
            w += font->glyph[(*str++ - font->first)].xAdvance;
        }

        return w;
    }

    void sleep()
    {
        epd_sleep();

        ESP_LOGI(TAG, "Display sleeping");
    }

    virtual void drawPixel(int16_t x, int16_t y, uint16_t color)
    {
        epd_set_pixel(buffer, x, y, color == 0);
    }

    void commit()
    {
        epd_init_fast();
        epd_display_fast(buffer);
    }

private:
    size_t _buffer_size;

    uint8_t *partial_buffer;

    const char *TAG = "EPAPER";
};

#endif
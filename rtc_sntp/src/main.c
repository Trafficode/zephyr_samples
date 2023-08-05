/*
 * Created by Trafficode in 2023.
 */
#include <inttypes.h>
#include <stdint.h>
#include <time.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_config.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/sntp.h>

#include "wifi_net.h"
#include "config_wifi.h"

LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_DBG);

static const struct gpio_dt_spec InfoLed =
    GPIO_DT_SPEC_GET(DT_NODELABEL(info_led), gpios);

static int64_t UptimeSyncMs = 0;
static int64_t SntpSyncSec = 0;

static int64_t rtc_time_get(void) {
    int64_t sec_elapsed = (k_uptime_get() - UptimeSyncMs) / 1000;
    return (SntpSyncSec + sec_elapsed);
}

static int32_t rtc_time_sync(void) {
    int32_t rc = 0;
    struct sntp_time sntp_time = {0};

    rc = sntp_simple("time.google.com", 2000, &sntp_time);
    if (0 == rc) {
        SntpSyncSec = (int64_t)sntp_time.seconds;
        UptimeSyncMs = k_uptime_get();
    } else {
        LOG_ERR("Failed to acquire SNTP, code %d", rc);
    }

    return (rc);
}

int main(void) {
    LOG_INF("Board: %s", CONFIG_BOARD);
    LOG_INF("sys_clock_hw_cycles_per_sec = %u", sys_clock_hw_cycles_per_sec());

    if (!device_is_ready(InfoLed.port)) {
        LOG_ERR("GPIO0 not ready");
        return (0);
    }

    int32_t ret = gpio_pin_configure_dt(&InfoLed, GPIO_OUTPUT_ACTIVE);
    if (0 != ret) {
        LOG_ERR("gpio configuration failed");
        return (0);
    }

    /* Wait till wifi connection established */
    wifi_net_init(WIFI_SSID, WIFI_PASS);

    int32_t rtc_first_sync_rc = rtc_time_sync();
    while (0 != rtc_first_sync_rc) {
        rtc_first_sync_rc = rtc_time_sync();
        k_sleep(K_SECONDS(4));
    }

    int64_t last_sync_uptime = k_uptime_get();
    while (1) {
        k_sleep(K_SECONDS(1));
        gpio_pin_toggle_dt(&InfoLed);

        int64_t uptime_now = k_uptime_get();
        if (60 * 1000 < uptime_now - last_sync_uptime) {
            time_t now = rtc_time_get();
            struct tm now_tm;
            gmtime_r(&now, &now_tm);
            LOG_INF("RTC %u/%u/%u %02u:%02u:%02u", now_tm.tm_mday,
                    1 + now_tm.tm_mon, 1900 + now_tm.tm_year, now_tm.tm_hour,
                    now_tm.tm_min, now_tm.tm_sec);

            struct sntp_time sntp_time = {0};
            int32_t rc = sntp_simple("time.google.com", 2000, &sntp_time);
            if (0 == rc) {
                int64_t sntp_ts = (int64_t)sntp_time.seconds;
                gmtime_r(&sntp_ts, &now_tm);
                LOG_INF("UTC %u/%u/%u %02u:%02u:%02u", now_tm.tm_mday,
                        1 + now_tm.tm_mon, 1900 + now_tm.tm_year,
                        now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec);
            } else {
                LOG_ERR("Failed to acquire SNTP, code %d", rc);
            }
            last_sync_uptime = uptime_now;
        }
    }
}

/* ---------------------------------------------------------------------------
 * end of file
 * --------------------------------------------------------------------------*/
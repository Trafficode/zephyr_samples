/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_DBG);

static const struct gpio_dt_spec InfoLed =
    GPIO_DT_SPEC_GET(DT_NODELABEL(info_led), gpios);

int main(void) {
    const struct device *const dht22 = DEVICE_DT_GET_ONE(aosong_dht);
    LOG_INF("Board: %s", CONFIG_BOARD);
    LOG_INF("sys_clock_hw_cycles_per_sec = %u", sys_clock_hw_cycles_per_sec());

    if (!device_is_ready(InfoLed.port)) {
        LOG_ERR("GPIO0 not ready");
        return (0);
    }

    if (!device_is_ready(dht22)) {
        LOG_ERR("DHT is not ready");
        return 0;
    }

    int ret = gpio_pin_configure_dt(&InfoLed, GPIO_OUTPUT_ACTIVE);
    if (0 != ret) {
        LOG_ERR("gpio configuration failed");
        return (0);
    }

    while (1) {
        k_sleep(K_SECONDS(5));

        int rc = sensor_sample_fetch(dht22);
        if (rc != 0) {
            LOG_ERR("Sensor fetch failed: %d", rc);
            continue;
        }

        struct sensor_value temperature;
        struct sensor_value humidity;

        rc = sensor_channel_get(dht22, SENSOR_CHAN_AMBIENT_TEMP, &temperature);
        if (rc == 0) {
            rc = sensor_channel_get(dht22, SENSOR_CHAN_HUMIDITY, &humidity);
        }
        if (rc != 0) {
            LOG_ERR("get failed: %d", rc);
        } else {
            LOG_INF("%.1f Cel ; %.1f RH", sensor_value_to_double(&temperature),
                    sensor_value_to_double(&humidity));
        }

        gpio_pin_toggle_dt(&InfoLed);
    }
}

/* ---------------------------------------------------------------------------
 * end of file
 * --------------------------------------------------------------------------*/
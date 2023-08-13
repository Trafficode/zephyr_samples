/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>

static const struct gpio_dt_spec InfoLed =
    GPIO_DT_SPEC_GET(DT_NODELABEL(info_led), gpios);

int main(void) {
    const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));

    printk("WDG, BOARD <%s>\n", CONFIG_BOARD);

    if (!device_is_ready(wdt)) {
        printk("%s: device not ready.\n", wdt->name);
        return 0;
    }

    struct wdt_timeout_cfg wdt_config = {
        /* Reset SoC when watchdog timer expires. */
        .flags = WDT_FLAG_RESET_SOC,

        /* Expire watchdog after max window */
        .window.min = 0,    /* for esp32 it has to be 0 */
        .window.max = 2000, /* millis */
        .callback = NULL,
    };

    int32_t wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
    if (wdt_channel_id < 0) {
        printk("Watchdog install error\n");
        return 0;
    }

    int32_t err = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
    if (err < 0) {
        printk("Watchdog setup error\n");
        return 0;
    }

    if (!device_is_ready(InfoLed.port)) {
        printk("gpio0 not ready\n");
        return (0);
    }

    int ret = gpio_pin_configure_dt(&InfoLed, GPIO_OUTPUT_ACTIVE);
    if (0 != ret) {
        printk("gpio configuration failed\n");
        return (0);
    }

    int32_t loop_cnt = 0;
    while (1) {
        gpio_pin_toggle_dt(&InfoLed);
        k_msleep(500);
        if (loop_cnt < 10) {
            printk("Wdg sample running...\n");
            wdt_feed(wdt, wdt_channel_id);
            loop_cnt++;
        } else {
            printk("Wdg waiting for reset...\n");
            k_sleep(K_SECONDS(10));
        }
    }
}

/* ---------------------------------------------------------------------------
 * end of file
 * --------------------------------------------------------------------------*/
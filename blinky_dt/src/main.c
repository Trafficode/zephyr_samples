/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

static const struct gpio_dt_spec InfoLed =
    GPIO_DT_SPEC_GET(DT_NODELABEL(info_led), gpios);

int main(void) {
    printk("Blinky, BOARD <%s>\n", CONFIG_BOARD);

    if (!device_is_ready(InfoLed.port)) {
        printk("gpio0 not ready\n");
        return (0);
    }

    int ret = gpio_pin_configure_dt(&InfoLed, GPIO_OUTPUT_ACTIVE);
    if (0 != ret) {
        printk("gpio configuration failed\n");
        return (0);
    }

    while (1) {
        gpio_pin_toggle_dt(&InfoLed);
        k_msleep(500);
    }
}

/* ---------------------------------------------------------------------------
 * end of file
 * --------------------------------------------------------------------------*/
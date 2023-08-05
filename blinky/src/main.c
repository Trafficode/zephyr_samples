/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define BUILTIN_LED_PIN (2)

static const struct device *gpio_ct_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));

int main(void) {
    printk("Blinky, BOARD <%s>\n", CONFIG_BOARD);

    if (!device_is_ready(gpio_ct_dev)) {
        printk("gpio0 not ready\n");
        return (0);
    }

    int ret =
        gpio_pin_configure(gpio_ct_dev, BUILTIN_LED_PIN, GPIO_OUTPUT_ACTIVE);
    if (0 != ret) {
        printk("gpio configuration failed\n");
        return (0);
    }

    int led_state = 0;
    while (1) {
        gpio_pin_set_raw(gpio_ct_dev, BUILTIN_LED_PIN, led_state);
        led_state ^= 0x1;
        k_msleep(500);
    }
}

/* ---------------------------------------------------------------------------
 * end of file
 * --------------------------------------------------------------------------*/
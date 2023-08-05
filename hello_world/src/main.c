/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

int main(void) {
    printk("ESP32 Hello World BOARD <%s>\n", CONFIG_BOARD);

    while (1) {
        printk("Tick/Tack\n");
        k_msleep(1000);
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * end of file
 * --------------------------------------------------------------------------*/
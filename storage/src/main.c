/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>

#define NVS_PARTITION        storage_partition
#define NVS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(NVS_PARTITION)
#define NVS_PARTITION_SIZE   FIXED_PARTITION_SIZE(NVS_PARTITION)

static struct nvs_fs Fs = {0};

static const struct gpio_dt_spec InfoLed =
    GPIO_DT_SPEC_GET(DT_NODELABEL(info_led), gpios);

#define BOOT_CNT_ID (1)

int main(void) {
    int32_t rc = 0;
    struct flash_pages_info info = {0};

    printk("STORAGE, BOARD <%s>\n", CONFIG_BOARD);

    if (!device_is_ready(InfoLed.port)) {
        printk("gpio0 not ready\n");
        return (0);
    }

    int ret = gpio_pin_configure_dt(&InfoLed, GPIO_OUTPUT_ACTIVE);
    if (0 != ret) {
        printk("gpio configuration failed\n");
        return (0);
    }

    Fs.flash_device = NVS_PARTITION_DEVICE;
    if (!device_is_ready(Fs.flash_device)) {
        printk("Flash device %s is not ready\n", Fs.flash_device->name);
        return 0;
    }
    Fs.offset = NVS_PARTITION_OFFSET;
    rc = flash_get_page_info_by_offs(Fs.flash_device, Fs.offset, &info);
    if (rc) {
        printk("Unable to get page info\n");
        return 0;
    }
    printk("NVS sector size %u part size %u\n", info.size, NVS_PARTITION_SIZE);

    Fs.sector_size = info.size;
    Fs.sector_count = 3U;

    rc = nvs_mount(&Fs);
    if (rc) {
        printk("Flash Init failed\n");
        return 0;
    }

    uint32_t boot_counter = UINT32_C(0);
    size_t area_len = sizeof(boot_counter);
    rc = nvs_read(&Fs, BOOT_CNT_ID, &boot_counter, area_len);
    if (rc > 0) { /* item was found, show it */
        printk("Id: %d, boot counter: %d\n", BOOT_CNT_ID, boot_counter);
    } else { /* item was not found, add it */
        printk("No boot counter found, adding it at id %d\n", BOOT_CNT_ID);
    }
    boot_counter++;

    if (area_len == nvs_write(&Fs, BOOT_CNT_ID, &boot_counter, area_len)) {
        printk("Save boot counter %d succ\n", boot_counter);
    } else {
        printk("Save boot counter %d err\n", boot_counter);
    }

    while (1) {
        gpio_pin_toggle_dt(&InfoLed);
        k_msleep(500);
    }
}

/* ---------------------------------------------------------------------------
 * end of file
 * --------------------------------------------------------------------------*/
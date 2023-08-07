/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>

#include "config_wifi.h"
#include "mqtt_worker.h"
#include "wifi_net.h"

LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_DBG);

static const struct gpio_dt_spec InfoLed =
    GPIO_DT_SPEC_GET(DT_NODELABEL(info_led), gpios);

#define SUBSCRIBE_TOPIC "/test/mosquitto/pubsub/topic"
#define PUBLISH_TOPIC   "/test/mosquitto/publish/esp32"

void subs_cb(char *topic, uint16_t topic_len, char *payload,
             uint16_t payload_len) {
    LOG_INF("Topic: %s", topic);
    LOG_INF("Payload: %s", payload);
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

    wifi_net_init(WIFI_SSID, WIFI_PASS);

    struct mqtt_topic subs_topic = {
        .topic = {.utf8 = (uint8_t *)SUBSCRIBE_TOPIC,
                  .size = strlen(SUBSCRIBE_TOPIC)},
        .qos = MQTT_QOS_0_AT_MOST_ONCE};
    struct mqtt_subscription_list subs_list = {
        .list = &subs_topic, .list_count = 1U, .message_id = 1U};
    mqtt_worker_init("test.mosquitto.org", NULL, 1883, &subs_list, subs_cb);

    int32_t lopp_cnt = 0;
    for (;;) {
        k_sleep(K_SECONDS(1));
        gpio_pin_toggle_dt(&InfoLed);

        if (0 == lopp_cnt % 8) {
            mqtt_worker_publish_qos1(PUBLISH_TOPIC, "ESP32_TEST");
        }
        lopp_cnt++;
    }
}

/* ---------------------------------------------------------------------------
 * end of file
 * --------------------------------------------------------------------------*/
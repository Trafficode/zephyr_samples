/* ---------------------------------------------------------------------------
 *  mqtt
 * ---------------------------------------------------------------------------
 *  Name: mqtt_worker.c
 * --------------------------------------------------------------------------*/
#include "mqtt_worker.h"

#include <stdint.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socketutils.h>

#define CLIENT_ID ("zephyrux")

static void mqtt_evt_handler(struct mqtt_client *const client,
                             const struct mqtt_evt *evt);

LOG_MODULE_REGISTER(MQTT, LOG_LEVEL_DBG);

/* The mqtt client struct */
static struct mqtt_client ClientCtx;

/* MQTT Broker details. */
static struct sockaddr_storage Broker;

/* Buffers for MQTT client. */
static uint8_t RxBuffer[1024];
static uint8_t TxBuffer[1024];

bool Connected = false;

static int32_t wait_for_input(int32_t timeout) {
#if defined(CONFIG_MQTT_LIB_TLS)
    struct zsock_pollfd fds[1] = {
        [0] = {.fd = ClientCtx.transport.tls.sock,
               .events = ZSOCK_POLLIN,
               .revents = 0},
    };
#else
    struct zsock_pollfd fds[1] = {
        [0] = {.fd = ClientCtx.transport.tcp.sock,
               .events = ZSOCK_POLLIN,
               .revents = 0},
    };
#endif

    int32_t res = zsock_poll(fds, 1, timeout);
    if (res < 0) {
        LOG_ERR("poll read event error");
        return -errno;
    }

    return res;
}

void mqtt_worker_init(const char *hostname, uint32_t port) {
    struct zsock_addrinfo *haddr;
    int32_t res = 0;
    static struct zsock_addrinfo hints;
    char port_str[16];
    struct mqtt_client *client = &ClientCtx;

    snprintf(port_str, sizeof(port_str), "%d", port);

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    res = net_getaddrinfo_addr_str(hostname, port_str, &hints, &haddr);
    if (res < 0) {
        LOG_ERR("Unable to get address for broker, retrying");
        goto failed_done;
    }

    LOG_INF("DNS resolved for %s:%d", hostname, port);

    mqtt_client_init(client);

    struct sockaddr_in *broker4 = (struct sockaddr_in *)&Broker;

    broker4->sin_family = AF_INET;
    broker4->sin_port = htons(port);
    net_ipaddr_copy(&broker4->sin_addr, &net_sin(haddr->ai_addr)->sin_addr);

    /* MQTT client configuration */
    client->broker = &Broker;
    client->evt_cb = mqtt_evt_handler;
    client->protocol_version = MQTT_VERSION_3_1_1;
    client->client_id.utf8 = (uint8_t *)CLIENT_ID;
    client->client_id.size = strlen(CLIENT_ID);
    client->password = NULL;
    client->user_name = NULL;

    /* MQTT buffers configuration */
    client->rx_buf = RxBuffer;
    client->rx_buf_size = sizeof(RxBuffer);
    client->tx_buf = TxBuffer;
    client->tx_buf_size = sizeof(TxBuffer);

    res = mqtt_connect(client);
    if (res != 0) {
        LOG_ERR("mqtt_connect, error %d", res);
        mqtt_disconnect(client);
        goto failed_done;
    }

    if (wait_for_input(2000)) {
        mqtt_input(client);
    }

    if (!Connected) {
        LOG_ERR("Connection timeout, abort...");
        mqtt_abort(client);
    }

failed_done:
    return;
}

static void mqtt_evt_handler(struct mqtt_client *const client,
                             const struct mqtt_evt *evt) {
    LOG_INF("mqtt_evt_handler");
    switch (evt->type) {
        case MQTT_EVT_SUBACK:
            LOG_INF("SUBACK packet id: %u", evt->param.suback.message_id);
            break;

        case MQTT_EVT_UNSUBACK:
            LOG_INF("UNSUBACK packet id: %u", evt->param.suback.message_id);
            break;

        case MQTT_EVT_CONNACK:
            if (evt->result != 0) {
                LOG_ERR("MQTT connect failed %d", evt->result);
                break;
            }

            Connected = true;
            LOG_INF("MQTT client connected!");
            // mqtt_subscribe_config(client);
            break;

        case MQTT_EVT_DISCONNECT:
            LOG_INF("MQTT client disconnected %d", evt->result);
            Connected = false;
            break;

        case MQTT_EVT_PUBLISH: {
            LOG_INF("MQTT_EVT_PUBLISH");
            break;
        }

        case MQTT_EVT_PUBACK:
            if (evt->result != 0) {
                LOG_ERR("MQTT PUBACK error %d", evt->result);
                break;
            }
            LOG_INF("PUBACK packet id: %u", evt->param.puback.message_id);
            break;

        default:
            LOG_INF("MQTT event received %d", evt->type);
            break;
    }
}

/* ---------------------------------------------------------------------------
 * end of file
 * --------------------------------------------------------------------------*/
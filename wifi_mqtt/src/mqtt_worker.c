/* ---------------------------------------------------------------------------
 *  mqtt
 * ---------------------------------------------------------------------------
 *  Name: mqtt_worker.c
 * --------------------------------------------------------------------------*/
#include "mqtt_worker.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socketutils.h>

LOG_MODULE_REGISTER(MQTT, LOG_LEVEL_DBG);

#define CLIENT_ID ("zephyrux")

typedef enum mqtt_worker_state {
    DNS_RESOLVE,
    CONNECT_TO_BROKER,
    CONNECTED
} mqtt_worker_state_t;

static void mqtt_evt_handler(struct mqtt_client *const client,
                             const struct mqtt_evt *evt);
static int32_t wait_for_input(int32_t timeout);
static int32_t dns_resolve(void);
static int32_t connect_to_broker(void);
static int32_t input_handle(void);
static int32_t subscribe_setup(void);

static void mqtt_proc(void *, void *, void *);

/* The mqtt client struct */
static struct mqtt_client ClientCtx;

/* MQTT Broker details. */
static struct sockaddr_storage Broker;

/* Buffers for MQTT client. */
static uint8_t RxBuffer[1024];
static uint8_t TxBuffer[1024];

static char BrokerHostnameStr[32];
static char BrokerAddrStr[16];
static char BrokerPortStr[32];
static int32_t BrokerPort = 0;

static mqtt_worker_state_t StateMachine = DNS_RESOLVE;

bool Connected = false;

#define MQTT_NET_STACK_SIZE (1024)
#define MQTT_NET_PRIORITY   (5)

K_THREAD_DEFINE(MqttNetTid, MQTT_NET_STACK_SIZE, mqtt_proc, NULL, NULL, NULL,
                MQTT_NET_PRIORITY, 0, 0);
K_SEM_DEFINE(MqttNetReady, 0, 1);

void mqtt_proc(void *arg1, void *arg2, void *arg3) {
    /* wait for start signal */
    k_sem_take(&MqttNetReady, K_FOREVER);
    StateMachine = DNS_RESOLVE;
    for (;;) {
        switch (StateMachine) {
            case DNS_RESOLVE: {
                LOG_INF("DNS_RESOLVE");
                int32_t res = dns_resolve();
                if (0 == res) {
                    StateMachine = CONNECT_TO_BROKER;
                }
                break;
            }
            case CONNECT_TO_BROKER: {
                LOG_INF("CONNECT_TO_BROKER");
                int32_t res = connect_to_broker();
                if (0 == res) {
                    LOG_INF("MQTT client connected!");
                    subscribe_setup();
                    StateMachine = CONNECTED;
                }
                break;
            }
            case CONNECTED: {
                LOG_INF("CONNECTED");
                int32_t res = input_handle();
                if (0 == res) {
                    StateMachine = CONNECTED;
                } else {
                    StateMachine = DNS_RESOLVE;
                }
                break;
            }
            default: {
                break;
            }
        }
    }
}

static int32_t wait_for_input(int32_t timeout) {
#if defined(CONFIG_MQTT_LIB_TLS)
    struct zsock_pollfd fds[1] = {
        [0] =
            {
                .fd = ClientCtx.transport.tls.sock,
                .events = ZSOCK_POLLIN,
                .revents = 0,
            },
    };
#else
    struct zsock_pollfd fds[1] = {
        [0] =
            {
                .fd = ClientCtx.transport.tcp.sock,
                .events = ZSOCK_POLLIN,
                .revents = 0,
            },
    };
#endif

    int32_t res = zsock_poll(fds, 1, timeout);
    if (0 > res) {
        LOG_ERR("zsock_poll event err %d", res);
    }

    return (res);
}

static int32_t connect_to_broker(void) {
    struct mqtt_client *client = &ClientCtx;
    int32_t res = 0;

    Connected = false;
    res = mqtt_connect(client);
    if (res != 0) {
        LOG_ERR("mqtt_connect, err %d", res);
        mqtt_disconnect(client);
        goto failed_done;
    }

    res = wait_for_input(2000);
    if (0 < res) {
        mqtt_input(client);
    }

    if (!Connected) {
        LOG_ERR("Connection timeout, abort...");
        mqtt_abort(client);
    } else {
        res = 0;
    }
failed_done:
    return (res);
}

#define SUBSCRIBE_TOPIC "/test/mosquitto/pubsub/topic"

static int32_t subscribe_setup(void) {
    struct mqtt_client *client = &ClientCtx;

    /* subscribe to config information */
    struct mqtt_topic subs_topic = {
        .topic = {.utf8 = (uint8_t *)SUBSCRIBE_TOPIC,
                  .size = strlen(SUBSCRIBE_TOPIC)},
        .qos = MQTT_QOS_0_AT_MOST_ONCE};
    const struct mqtt_subscription_list subs_list = {
        .list = &subs_topic, .list_count = 1U, .message_id = 1U};

    int32_t res = mqtt_subscribe(client, &subs_list);
    if (0 != res) {
        LOG_ERR("Failed to subscribe to %s item, error %d",
                subs_topic.topic.utf8, res);
    }
    return (res);
}

static int32_t input_handle(void) {
    int32_t res = 0;
    struct mqtt_client *client = &ClientCtx;
    static int64_t next_alive = INT64_MIN;

    /* idle and process messages */
    int64_t uptime_ms = k_uptime_get();
    if (uptime_ms < next_alive) {
        res = wait_for_input(5 * MSEC_PER_SEC);
        if (0 < res) {
            mqtt_input(client);
        }

        if (!Connected) {
            mqtt_disconnect(client);
            res = -1;
            goto failed_done;
        }
    } else {
        LOG_INF("Keepalive...");
        mqtt_live(client);
        next_alive = uptime_ms + (60 * MSEC_PER_SEC);
    }

    res = 0;

failed_done:
    return (res);
}

static int32_t dns_resolve(void) {
    static struct zsock_addrinfo hints;
    struct zsock_addrinfo *haddr;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;

    int32_t res = net_getaddrinfo_addr_str(BrokerHostnameStr, BrokerPortStr,
                                           &hints, &haddr);
    if (0 != res) {
        LOG_ERR("Unable to get address of broker err %d", res);
        goto failed_done;
    }

    struct sockaddr_in *ipv4_broker = (struct sockaddr_in *)&Broker;
    ipv4_broker->sin_family = AF_INET;
    ipv4_broker->sin_port = htons(BrokerPort);
    net_ipaddr_copy(&ipv4_broker->sin_addr, &net_sin(haddr->ai_addr)->sin_addr);
    uint8_t *in_addr = ipv4_broker->sin_addr.s4_addr;
    LOG_INF("Broker addr %d.%d.%d.%d", in_addr[3], in_addr[2], in_addr[1],
            in_addr[0]);
failed_done:
    return (res);
}

void mqtt_worker_init(const char *hostname, const char *addr, int32_t port) {
    struct mqtt_client *client = &ClientCtx;

    strncpy(BrokerHostnameStr, hostname, sizeof(BrokerHostnameStr));
    BrokerPort = port;
    snprintf(BrokerPortStr, sizeof(BrokerPortStr), "%d", port);

    mqtt_client_init(client);

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

    k_sem_give(&MqttNetReady);
}

// void mqtt_net_init(const char *hostname, uint32_t port) {
//     struct zsock_addrinfo *haddr;
//     int32_t res = 0;
//     static struct zsock_addrinfo hints;
//     char port_str[16];
//     struct mqtt_client *client = &ClientCtx;

//     snprintf(port_str, sizeof(port_str), "%d", port);

//     hints.ai_family = AF_INET;
//     hints.ai_socktype = SOCK_STREAM;
//     hints.ai_protocol = 0;
//     res = net_getaddrinfo_addr_str(hostname, port_str, &hints, &haddr);
//     if (res < 0) {
//         LOG_ERR("Unable to get address for broker, retrying");
//         goto failed_done;
//     }

//     LOG_INF("DNS resolved for %s:%d", hostname, port);

//     mqtt_client_init(client);

//     struct sockaddr_in *broker4 = (struct sockaddr_in *)&Broker;

//     broker4->sin_family = AF_INET;
//     broker4->sin_port = htons(port);
//     net_ipaddr_copy(&broker4->sin_addr, &net_sin(haddr->ai_addr)->sin_addr);

//     /* MQTT client configuration */
//     client->broker = &Broker;
//     client->evt_cb = mqtt_evt_handler;
//     client->protocol_version = MQTT_VERSION_3_1_1;
//     client->client_id.utf8 = (uint8_t *)CLIENT_ID;
//     client->client_id.size = strlen(CLIENT_ID);
//     client->password = NULL;
//     client->user_name = NULL;

//     /* MQTT buffers configuration */
//     client->rx_buf = RxBuffer;
//     client->rx_buf_size = sizeof(RxBuffer);
//     client->tx_buf = TxBuffer;
//     client->tx_buf_size = sizeof(TxBuffer);

//     res = mqtt_connect(client);
//     if (res != 0) {
//         LOG_ERR("mqtt_connect, error %d", res);
//         mqtt_disconnect(client);
//         goto failed_done;
//     }

//     if (wait_for_input(2000)) {
//         mqtt_input(client);
//     }

//     if (!Connected) {
//         LOG_ERR("Connection timeout, abort...");
//         mqtt_abort(client);
//     }

// failed_done:
//     return;
// }

static void mqtt_evt_handler(struct mqtt_client *const client,
                             const struct mqtt_evt *evt) {
    LOG_INF("mqtt_evt_handler");

    switch (evt->type) {
        case MQTT_EVT_SUBACK: {
            LOG_INF("MQTT_EVT_SUBACK");
            break;
        }
        case MQTT_EVT_UNSUBACK: {
            LOG_INF("MQTT_EVT_UNSUBACK");
            break;
        }
        case MQTT_EVT_CONNACK: {
            if (evt->result != 0) {
                LOG_ERR("MQTT connect failed %d", evt->result);
            } else {
                Connected = true;
            }
            break;
        }
        case MQTT_EVT_DISCONNECT: {
            LOG_INF("MQTT client disconnected %d", evt->result);
            Connected = false;
            break;
        }
        case MQTT_EVT_PUBLISH: {
            LOG_INF("MQTT_EVT_PUBLISH");
            const struct mqtt_publish_param *pub = &evt->param.publish;
            uint8_t payload[64];
            int32_t len = pub->message.payload.len;
            int32_t bytes_read = 0;

            LOG_INF("MQTT publish received %d, %d bytes", evt->result, len);
            LOG_INF("   id: %d, qos: %d", pub->message_id,
                    pub->message.topic.qos);
            LOG_INF("   item: %s", pub->message.topic.topic.utf8);

            /* assuming the config message is textual */
            while (len) {
                bytes_read = mqtt_read_publish_payload_blocking(
                    client, payload, len >= 32 ? 32 : len);
                if (bytes_read < 0) {
                    LOG_ERR("failure to read payload");
                    break;
                }

                payload[bytes_read] = '\0';
                len -= bytes_read;
            }
            LOG_INF("   payload: %s", payload);
            break;
        }
        case MQTT_EVT_PUBACK: {
            if (evt->result != 0) {
                LOG_ERR("MQTT PUBACK error %d", evt->result);
                break;
            }
            LOG_INF("PUBACK packet id: %u", evt->param.puback.message_id);
            break;
        }
        case MQTT_EVT_PUBREC: {
            LOG_INF("MQTT_EVT_PUBREC");
            break;
        }
        case MQTT_EVT_PUBREL: {
            LOG_INF("MQTT_EVT_PUBREL");
            break;
        }
        case MQTT_EVT_PUBCOMP: {
            LOG_INF("MQTT_EVT_PUBCOMP");
            break;
        }
        case MQTT_EVT_PINGRESP: {
            LOG_INF("MQTT_EVT_PINGRESP");
            break;
        }
        default: {
            LOG_INF("MQTT event received %d", evt->type);
            break;
        }
    }
}

/* ---------------------------------------------------------------------------
 * end of file
 * --------------------------------------------------------------------------*/
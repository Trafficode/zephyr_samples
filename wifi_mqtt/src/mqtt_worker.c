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

typedef struct subs_data {
    uint8_t topic[128];
    uint8_t payload[256];
    uint16_t payload_len;
} subs_data_t;

typedef enum worker_state {
    DNS_RESOLVE,
    CONNECT_TO_BROKER,
    SUBSCRIBE,
    CONNECTED
} worker_state_t;

static void mqtt_evt_handler(struct mqtt_client *const client,
                             const struct mqtt_evt *evt);
static int32_t wait_for_input(int32_t timeout);
static int32_t dns_resolve(void);
static int32_t connect_to_broker(void);
static int32_t input_handle(void);
static int32_t mqtt_worker_subscribe(void);

static void mqtt_proc(void *, void *, void *);
static void subscribe_proc(void *, void *, void *);

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
struct mqtt_subscription_list *SubsList = NULL;
static worker_state_t StateMachine = DNS_RESOLVE;

bool Connected = false;
bool Subscribed = false;

static subs_data_t SubsData = {0};

#define MQTT_NET_STACK_SIZE (1024)
#define MQTT_NET_PRIORITY   (5)
K_THREAD_DEFINE(MqttNetTid, MQTT_NET_STACK_SIZE, mqtt_proc, NULL, NULL, NULL,
                MQTT_NET_PRIORITY, 0, 0);

#define SUBSCRIBE_STACK_SIZE (1024)
#define SUBSCRIBE_PRIORITY   (5)
K_THREAD_DEFINE(SubsTid, SUBSCRIBE_STACK_SIZE, subscribe_proc, NULL, NULL, NULL,
                SUBSCRIBE_PRIORITY, 0, 0);

K_SEM_DEFINE(MqttNetReady, 0, 1);
K_MSGQ_DEFINE(SubsQueue, sizeof(subs_data_t), 2, 4);

static void subscribe_proc(void *, void *, void *) {
    for (;;) {
        k_sleep(K_SECONDS(1));
    }
}

static void mqtt_proc(void *arg1, void *arg2, void *arg3) {
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
                    StateMachine = SUBSCRIBE;
                } else {
                    /* nothing, make next attempt */
                }
                break;
            }
            case SUBSCRIBE: {
                LOG_INF("SUBSCRIBE");
                int32_t res = mqtt_worker_subscribe();
                if (0 == res) {
                    LOG_INF("Subscribe done");
                    StateMachine = CONNECTED;
                } else {
                    /* nothing, make next attempt */
                }
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

static int32_t mqtt_worker_subscribe(void) {
    struct mqtt_client *client = &ClientCtx;
    int32_t res = 0;

    if (NULL != SubsList) {
        Subscribed = false;
        res = mqtt_subscribe(client, SubsList);
        if (0 != res) {
            LOG_ERR("Failed to subscribe topics, err %d", res);
            goto failed_done;
        }

        res = wait_for_input(4000);
        if (0 < res) {
            mqtt_input(client);
        }

        if (!Subscribed) {
            LOG_ERR("Subscribe timeout");
        } else {
            res = 0;
        }
    }

failed_done:
    return (res);
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
        LOG_ERR("Unable to get address of broker, err %d", res);
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

void mqtt_worker_init(const char *hostname, const char *addr, int32_t port,
                      struct mqtt_subscription_list *subs) {
    struct mqtt_client *client = &ClientCtx;

    SubsList = subs;
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

static void mqtt_evt_handler(struct mqtt_client *const client,
                             const struct mqtt_evt *evt) {
    LOG_INF("mqtt_evt_handler");

    switch (evt->type) {
        case MQTT_EVT_SUBACK: {
            LOG_INF("MQTT_EVT_SUBACK");
            Subscribed = true;
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
            int32_t len = pub->message.payload.len;
            int32_t bytes_read = 0;

            LOG_INF("MQTT publish received %d, %d bytes", evt->result, len);
            LOG_INF("   id: %d, qos: %d", pub->message_id,
                    pub->message.topic.qos);
            LOG_INF("   item: %s", pub->message.topic.topic.utf8);

            if (MQTT_WORKER_MAX_PAYLOAD_LEN >= (pub->message.payload.len + 1)) {
                LOG_ERR("Payload to long %d", len);
                break;
            }

            if (MQTT_WORKER_MAX_TOPIC_LEN >= pub->message.topic.topic.size) {
                LOG_ERR("Topic to long %d", pub->message.topic.topic.size);
                break;
            }

            /* assuming the config message is textual */
            while (len) {
                bytes_read = mqtt_read_publish_payload_blocking(
                    client, SubsData.payload, len >= 32 ? 32 : len);
                if (bytes_read < 0) {
                    LOG_ERR("failure to read payload");
                    break;
                }

                SubsData.payload[bytes_read] = '\0';
                len -= bytes_read;
            }

            LOG_INF("   payload: %s", SubsData.payload);
            memcpy(SubsData.topic, pub->message.topic.topic.utf8,
                   pub->message.topic.topic.size);

            int32_t res = k_msgq_put(&SubsQueue, &SubsData, K_MSEC(100));
            if (0 != res) {
                LOG_ERR("Timeout to put subs msg into queue");
            }
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
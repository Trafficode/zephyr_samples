/* ---------------------------------------------------------------------------
 *  mqtt
 * ---------------------------------------------------------------------------
 *  Name: mqtt_worker.c
 * --------------------------------------------------------------------------*/
#include "mqtt_worker.h"

#include <stdarg.h>
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
    uint8_t topic[MQTT_WORKER_MAX_TOPIC_LEN];
    uint16_t topic_len;
    uint8_t payload[MQTT_WORKER_MAX_PAYLOAD_LEN];
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
static struct mqtt_publish_param PubData = {0};
static enum mqtt_evt_type LastEvt = 0;

static worker_state_t StateMachine = DNS_RESOLVE;
static char PublishBuffer[MQTT_WORKER_MAX_PUBLISH_LEN];

bool Connected = false;
bool Subscribed = false;

static subs_cb_t SubsCb = NULL;

#define MQTT_NET_STACK_SIZE (2 * 1024)
#define MQTT_NET_PRIORITY   (5)
K_THREAD_DEFINE(MqttNetTid, MQTT_NET_STACK_SIZE, mqtt_proc, NULL, NULL, NULL,
                MQTT_NET_PRIORITY, 0, 0);

#define SUBSCRIBE_STACK_SIZE (2 * 1024)
#define SUBSCRIBE_PRIORITY   (5)
K_THREAD_DEFINE(SubsTid, SUBSCRIBE_STACK_SIZE, subscribe_proc, NULL, NULL, NULL,
                SUBSCRIBE_PRIORITY, 0, 0);

K_SEM_DEFINE(MqttNetReady, 0, 1);
K_SEM_DEFINE(PublishAck, 0, 1);
K_MSGQ_DEFINE(SubsQueue, sizeof(subs_data_t *), 4, 4);
K_MEM_SLAB_DEFINE_STATIC(SubsQueueSlab, sizeof(subs_data_t), 4, 4);

static void subscribe_proc(void *arg1, void *arg2, void *arg3) {
    subs_data_t *subs_data = NULL;
    for (;;) {
        if (0 == k_msgq_get(&SubsQueue, &subs_data, K_SECONDS(1))) {
            /* handle incomming message here*/
            if (NULL != SubsCb) {
                SubsCb((char *)subs_data->topic, subs_data->topic_len,
                       (char *)subs_data->payload, subs_data->payload_len);
            }
            k_mem_slab_free(&SubsQueueSlab, (void **)&subs_data);
        }
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

    if (NULL == SubsList) {
        LOG_WRN("Subscription list empty");
        goto failed_done;
    }

    Subscribed = false;
    res = mqtt_subscribe(client, SubsList);
    if (0 != res) {
        LOG_ERR("Failed to subscribe topics, err %d", res);
        goto failed_done;
    }

    while (true) {
        LastEvt = 0xFF;
        res = wait_for_input(4000);
        if (0 < res) {
            mqtt_input(client);
            if (LastEvt != MQTT_EVT_SUBACK && LastEvt != 0xFF) {
                LOG_WRN("Unexpected event got, try again");
                continue;
            }
        }
        break;
    }

    if (!Subscribed) {
        LOG_ERR("Subscribe timeout");
    } else {
        res = 0;
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
    int32_t res = 0;
    struct sockaddr_in *ipv4_broker = (struct sockaddr_in *)&Broker;
    uint8_t *in_addr = NULL;

    ipv4_broker->sin_family = AF_INET;
    ipv4_broker->sin_port = htons(BrokerPort);
    res = zsock_inet_pton(AF_INET, BrokerHostnameStr, &ipv4_broker->sin_addr);
    if (0 != res) {
        res = 0; /* 0 - success, string ip address delivered, dns not needed */
        goto resolve_done;
    }

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;

    res = net_getaddrinfo_addr_str(BrokerHostnameStr, BrokerPortStr, &hints,
                                   &haddr);
    if (0 != res) {
        LOG_ERR("Unable to get address of broker, err %d", res);
        goto resolve_done;
    }

    ipv4_broker->sin_family = AF_INET;
    ipv4_broker->sin_port = htons(BrokerPort);
    net_ipaddr_copy(&ipv4_broker->sin_addr, &net_sin(haddr->ai_addr)->sin_addr);

resolve_done:
    in_addr = ipv4_broker->sin_addr.s4_addr;
    LOG_INF("Broker addr %d.%d.%d.%d", in_addr[0], in_addr[1], in_addr[2],
            in_addr[3]);
    return (res);
}

int32_t mqtt_worker_publish_qos1(const char *topic, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    uint32_t len =
        vsnprintf(PublishBuffer, MQTT_WORKER_MAX_PUBLISH_LEN, fmt, args);
    va_end(args);

    PubData.message.payload.len = len;
    PubData.message.topic.topic.utf8 = (char *)topic;
    PubData.message.topic.topic.size = strlen((const char *)topic);
    PubData.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
    PubData.message_id += 1U;

    struct mqtt_client *client = &ClientCtx;

    k_sem_take(&PublishAck, K_NO_WAIT);
    int32_t res = mqtt_publish(client, &PubData);
    if (0 != res) {
        LOG_ERR("could not publish, err %d", res);
        goto failed_done;
    }

    res = k_sem_take(&PublishAck, K_SECONDS(2));
    if (0 != res) {
        LOG_ERR("publish ack timeout");
    }
failed_done:
    return (res);
}

void mqtt_worker_init(const char *hostname, int32_t port,
                      struct mqtt_subscription_list *subs, subs_cb_t subs_cb) {
    struct mqtt_client *client = &ClientCtx;

    SubsCb = subs_cb;
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

    PubData.message.payload.data = (uint8_t *)PublishBuffer;
    PubData.message_id = 1U;
    PubData.dup_flag = 0U;
    PubData.retain_flag = 1U;

    k_sem_give(&MqttNetReady);
}

static void mqtt_evt_handler(struct mqtt_client *const client,
                             const struct mqtt_evt *evt) {
    LOG_INF("mqtt_evt_handler");
    LastEvt = evt->type;

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
            subs_data_t *subs_data = NULL;
            const struct mqtt_publish_param *pub = &evt->param.publish;
            int32_t len = pub->message.payload.len;
            int32_t bytes_read = INT32_MIN;
            bool cleanup_and_break = false;

            LOG_INF("MQTT publish received %d, %d bytes", evt->result, len);
            LOG_INF("   id: %d, qos: %d", pub->message_id,
                    pub->message.topic.qos);
            LOG_INF("   topic: %s", pub->message.topic.topic.utf8);

            if (0 != k_mem_slab_alloc(&SubsQueueSlab, (void **)&subs_data,
                                      K_MSEC(1000))) {
                LOG_ERR("Get free subs slab failed");
                cleanup_and_break = true;
            }

            if (MQTT_WORKER_MAX_PAYLOAD_LEN < (pub->message.payload.len + 1)) {
                LOG_ERR("Payload to long %d", len);
                cleanup_and_break = true;
            }

            if (MQTT_WORKER_MAX_TOPIC_LEN < pub->message.topic.topic.size) {
                LOG_ERR("Topic to long %d", pub->message.topic.topic.size);
                cleanup_and_break = true;
            }

            if (!Connected) {
                LOG_WRN("Not connected yet");
                cleanup_and_break = true;
            }

            if (cleanup_and_break) {
                uint8_t tmp[32];
                if (NULL != subs_data) {
                    k_mem_slab_free(&SubsQueueSlab, (void **)&subs_data);
                }

                while (len) { /* cleanup mqtt socket buffer */
                    bytes_read = mqtt_read_publish_payload_blocking(
                        client, tmp, len >= 32 ? 32 : len);
                    if (0 > bytes_read) {
                        break;
                    }
                    len -= bytes_read;
                }
                break;
            }

            /* assuming the config message is textual */
            int32_t read_idx = 0;
            while (len) {
                bytes_read = mqtt_read_publish_payload_blocking(
                    client, subs_data->payload + read_idx,
                    len >= 32 ? 32 : len);

                if (0 > bytes_read) {
                    LOG_ERR("Failure to read payload");
                    break;
                }

                len -= bytes_read;
                read_idx += bytes_read;
            }

            subs_data->payload[pub->message.payload.len] = '\0';
            subs_data->payload_len = pub->message.payload.len;
            subs_data->topic_len = pub->message.topic.topic.size;
            if (0 <= bytes_read) {
                LOG_INF("   payload: %s", subs_data->payload);
                memcpy(subs_data->topic, pub->message.topic.topic.utf8,
                       subs_data->topic_len);
                subs_data->topic[subs_data->topic_len] = '\0';
                int32_t res = k_msgq_put(&SubsQueue, &subs_data, K_MSEC(1000));
                if (0 != res) {
                    LOG_ERR("Timeout to put subs msg into queue");
                }
            }

            break;
        }
        case MQTT_EVT_PUBACK: {
            if (evt->result != 0) {
                LOG_ERR("PUBACK error %d", evt->result);
            } else {
                LOG_INF("PUBACK packet id: %u", evt->param.puback.message_id);
                k_sem_give(&PublishAck);
            }
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
            LOG_WRN("MQTT event unknown %d", evt->type);
            break;
        }
    }
}

/* ---------------------------------------------------------------------------
 * end of file
 * --------------------------------------------------------------------------*/
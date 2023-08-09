/* ---------------------------------------------------------------------------
 *  mqtt
 * ---------------------------------------------------------------------------
 *  Name: mqtt_worker.h
 * --------------------------------------------------------------------------*/
#ifndef MQTT_WORKER_H_
#define MQTT_WORKER_H_

#include <stdint.h>
#include <zephyr/net/mqtt.h>

#define MQTT_WORKER_CLIENT_ID           ("zephyrux")
#define MQTT_WORKER_MAX_TOPIC_LEN       (128)
#define MQTT_WORKER_MAX_PAYLOAD_LEN     (256)
#define MQTT_WORKER_MAX_PUBLISH_LEN     (256)
#define MQTT_WORKER_PUBLISH_ACK_TIMEOUT (4) /* seconds */

typedef void (*subs_cb_t)(char *topic, uint16_t topic_len, char *payload,
                          uint16_t payload_len);

/**
 * @brief Initialize worker. All next action will be executed in separated
 * thread. Function is not blocked, to verify if driver is connected with broker
 * use api below.
 * @param hostname It can be name of host or string representation of ip addr.
 * @param port Broker port
 * @param subs Pointer to list of topics to be subscribed, NULL if no topics
 * should be subcribed
 * @param subs_cb Function to handle data incomming to subscribed topics, NULL
 * if not needed.
 */
void mqtt_worker_init(const char *hostname, int32_t port,
                      struct mqtt_subscription_list *subs, subs_cb_t subs_cb);

/**
 * @brief Publish data to given topic. Use in the same way as typical printf().
 * @param topic Topic where msg will be published
 */
int32_t mqtt_worker_publish_qos1(const char *topic, const char *fmt, ...);

/**
 * @brief Typically put to network disconnect callback to notify mqtt stack
 * about network absence. This will speed up reconnection process.
 */
void mqtt_worker_disconnect(void);

/**
 * @brief Make broker connection attempt.
 */
void mqtt_worker_connection_attempt(void);

#endif /* MQTT_WORKER_H_ */
/* ---------------------------------------------------------------------------
 * end of file
 * --------------------------------------------------------------------------*/
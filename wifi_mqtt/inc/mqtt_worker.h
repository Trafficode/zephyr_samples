/* ---------------------------------------------------------------------------
 *  mqtt
 * ---------------------------------------------------------------------------
 *  Name: mqtt_worker.h
 * --------------------------------------------------------------------------*/
#ifndef MQTT_WORKER_H_
#define MQTT_WORKER_H_

#include <stdint.h>
#include <zephyr/net/mqtt.h>

#define MQTT_WORKER_MAX_TOPIC_LEN   (128)
#define MQTT_WORKER_MAX_PAYLOAD_LEN (256)
#define MQTT_WORKER_MAX_PUBLISH_LEN (256)

typedef void (*subs_cb_t)(char *topic, uint16_t topic_len, char *payload,
                          uint16_t payload_len);

void mqtt_worker_init(const char *hostname, const char *addr, int32_t port,
                      struct mqtt_subscription_list *subs, subs_cb_t subs_cb);

int32_t mqtt_worker_publish_qos1(const char *topic, const char *fmt, ...);

#endif /* MQTT_WORKER_H_ */
/* ---------------------------------------------------------------------------
 * end of file
 * --------------------------------------------------------------------------*/
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

void mqtt_worker_init(const char *hostname, const char *addr, int32_t port,
                      struct mqtt_subscription_list *subs);

#endif /* MQTT_WORKER_H_ */
/* ---------------------------------------------------------------------------
 * end of file
 * --------------------------------------------------------------------------*/
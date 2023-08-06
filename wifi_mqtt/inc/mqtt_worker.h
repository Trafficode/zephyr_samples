/* ---------------------------------------------------------------------------
 *  mqtt
 * ---------------------------------------------------------------------------
 *  Name: mqtt_worker.h
 * --------------------------------------------------------------------------*/
#ifndef MQTT_WORKER_H_
#define MQTT_WORKER_H_

#include <stdint.h>

void mqtt_worker_init(const char *hostname, const char *addr, int32_t port);

#endif /* MQTT_WORKER_H_ */
/* ---------------------------------------------------------------------------
 * end of file
 * --------------------------------------------------------------------------*/
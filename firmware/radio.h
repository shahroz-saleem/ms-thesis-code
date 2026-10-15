#ifndef RADIO_H
#define RADIO_H

#include <stddef.h>
#include <stdint.h>

#include "event.h"

typedef void (*radio_receive_t)(uint16_t sender, const void *data, size_t len);

int radio_init(event_queue_t *queue, radio_receive_t receive);
int radio_set_id(uint16_t id);
uint16_t radio_id(void);
int radio_send(uint16_t to, const void *data, size_t len);

void radio_probe(void);
void radio_print_heard(void);

#endif

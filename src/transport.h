#pragma once
#include <stdint.h>
void transport_poll(void);
void transport_tick(void);
void transport_receive(const uint8_t packet[64]);
void transport_reset(void);

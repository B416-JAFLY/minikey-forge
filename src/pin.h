#pragma once
#include "cbor.h"
void pin_init(void);
int pin_is_set(void);
size_t pin_request(cv map,uint8_t *out,size_t cap);
/* returns CTAP error, sets verified only for an authenticated PIN token */
int pin_authorize(cv map,int param_key,int protocol_key,const uint8_t hash[32],int *verified);

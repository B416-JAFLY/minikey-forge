#pragma once
#include <stddef.h>
#include <stdint.h>
#define CTAP_MAX 1200
size_t ctap_request(const uint8_t *request,size_t n,uint8_t *response,size_t cap);
void ctap_reset_state(void);

#pragma once
#include <stdint.h>
#define STORE_BASE 0xF0000u
#define STORE_SIZE 0x10000u
#define STORE_CAPACITY 48u
typedef struct {uint8_t id[16],key[32],rp[32];uint32_t counter;uint8_t resident,user_len,user[64];char name[65],display[65];} credential;
int store_init(void);
int store_find(const uint8_t id[16],const uint8_t rp[32],credential *out);
int store_create(const uint8_t rp[32],credential *out,uint8_t public_key[64]);
int store_save(const credential *c);
int store_reset(void);
unsigned store_used(void);
unsigned store_count(void);
int store_at(unsigned index,credential *out);
int store_pin_get(credential *out);
int store_pin_save(const credential *c);
int store_resident(const uint8_t rp[32],const uint8_t *user,unsigned length,credential *out);

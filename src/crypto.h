#pragma once
#include <stdint.h>
#include <stddef.h>
void sha(const void *p,size_t n,uint8_t out[32]);
void mac(const uint8_t key[32],const void *p,size_t n,uint8_t out[32]);
void derive(const char *label,uint32_t index,uint8_t out[32]);
int crypto_equal(const void *a,const void *b,size_t n);
int crypto_public(const uint8_t private_key[32],uint8_t public_key[64]);
int crypto_sign(const uint8_t private_key[32],const uint8_t hash[32],uint8_t der[72]);
void crypto_cipher(uint8_t *p,size_t n,const uint8_t iv[16]);

void crypto_seed(const uint8_t seed[32]);
void crypto_random(uint8_t out[32]);
int crypto_ecdh(const uint8_t key[32],const uint8_t pub[64],uint8_t secret[32]);
void crypto_cbc(uint8_t *p,size_t n,const uint8_t key[32],int encrypt);

#include "crypto.h"
#include "platform.h"
#include "sha256.h"
#include "uECC.h"
#include "aes.h"
#include <string.h>
void sha(const void *p,size_t n,uint8_t out[32]){SHA256_CTX c;sha256_init(&c);sha256_update(&c,p,n);sha256_final(&c,out);}
void mac(const uint8_t key[32],const void *p,size_t n,uint8_t out[32]){uint8_t pad[64],inner[32];SHA256_CTX c;
 memset(pad,0x36,64);for(int i=0;i<32;i++)pad[i]^=key[i];sha256_init(&c);sha256_update(&c,pad,64);sha256_update(&c,p,n);sha256_final(&c,inner);
 memset(pad,0x5c,64);for(int i=0;i<32;i++)pad[i]^=key[i];sha256_init(&c);sha256_update(&c,pad,64);sha256_update(&c,inner,32);sha256_final(&c,out);memset(inner,0,32);}
void derive(const char *label,uint32_t index,uint8_t out[32]){uint8_t b[64]={0};size_t n=strlen(label);if(n>55)n=55;memcpy(b,label,n);b[56]=index>>24;b[57]=index>>16;b[58]=index>>8;b[59]=index;mac(mini_master_key,b,60,out);}
int crypto_equal(const void *aa,const void *bb,size_t n){const uint8_t *a=aa,*b=bb;unsigned diff=0;for(size_t i=0;i<n;i++)diff|=a[i]^b[i];return diff==0;}
int crypto_public(const uint8_t k[32],uint8_t p[64]){return uECC_compute_public_key(k,p,uECC_secp256r1());}
typedef struct {uECC_HashContext base;SHA256_CTX c;} hc;
static void hi(const uECC_HashContext *b){sha256_init(&((hc*)b)->c);}
static void hu(const uECC_HashContext *b,const uint8_t *p,unsigned n){sha256_update(&((hc*)b)->c,p,n);}
static void hf(const uECC_HashContext *b,uint8_t *p){sha256_final(&((hc*)b)->c,p);}
int crypto_sign(const uint8_t k[32],const uint8_t hash[32],uint8_t der[72]){uint8_t temp[128],raw[64];hc c;memset(&c,0,sizeof(c));c.base=(uECC_HashContext){hi,hu,hf,64,32,temp};
 if(!uECC_sign_deterministic(k,hash,32,&c.base,raw,uECC_secp256r1()))return 0;
 unsigned pos=2;der[0]=0x30;for(int half=0;half<2;half++){unsigned begin=half*32,end=begin+32;while(begin<end-1&&raw[begin]==0)begin++;unsigned pad=(raw[begin]&128)!=0;der[pos++]=2;der[pos++]=end-begin+pad;if(pad)der[pos++]=0;memcpy(der+pos,raw+begin,end-begin);pos+=end-begin;}der[1]=pos-2;memset(raw,0,64);return pos;}
void crypto_cipher(uint8_t *p,size_t n,const uint8_t iv[16]){uint8_t k[32];derive("storage-encryption",0,k);struct AES_ctx c;AES_init_ctx_iv(&c,k,iv);AES_CTR_xcrypt_buffer(&c,p,n);memset(k,0,32);memset(&c,0,sizeof(c));}

static uint8_t random_seed[32];static uint32_t random_count;
void crypto_seed(const uint8_t seed[32]){mac(mini_master_key,seed,32,random_seed);random_count=0;}
void crypto_random(uint8_t out[32]){uint8_t b[8];uint32_t t=board_millis(),c=++random_count;memcpy(b,&t,4);memcpy(b+4,&c,4);mac(random_seed,b,8,out);}
int crypto_ecdh(const uint8_t k[32],const uint8_t pub[64],uint8_t secret[32]){uint8_t x[32];if(!uECC_valid_public_key(pub,uECC_secp256r1())||!uECC_shared_secret(pub,k,x,uECC_secp256r1()))return 0;sha(x,32,secret);memset(x,0,32);return 1;}
void crypto_cbc(uint8_t *p,size_t n,const uint8_t key[32],int encrypt){uint8_t iv[16]={0};struct AES_ctx c;AES_init_ctx_iv(&c,key,iv);if(encrypt)AES_CBC_encrypt_buffer(&c,p,n);else AES_CBC_decrypt_buffer(&c,p,n);memset(&c,0,sizeof(c));}

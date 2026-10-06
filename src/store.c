#include "store.h"
#include "platform.h"
#include "crypto.h"
#include <string.h>
/* Dual 32KiB banks; commit header last, preserve old bank until next GC. */
#define BANK 32768u
#define SLOTS 56u
typedef struct {uint8_t id[16],rp[32];uint32_t address;} entry;
static entry index_[STORE_CAPACITY];
static unsigned count_,used;static uint32_t base,generation,serial,pin_address;
static uint8_t epoch[16];static int ready,legacy;
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void put32(uint8_t *p,uint32_t n){p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static int blank(const uint8_t *p,unsigned n){while(n--)if(*p++!=255)return 0;return 1;}
static void unique(const char *label,uint32_t number,uint8_t out[32]){uint8_t b[80]={0};memcpy(b,label,strlen(label));memcpy(b+56,epoch,16);put32(b+72,number);mac(mini_master_key,b,76,out);}
static int header(uint32_t a,uint8_t *h){uint8_t tag[32],fingerprint[32];if(!flash_read(a,h,96)||memcmp(h,"MINIF2v2",8))return 0;derive("storage-header",0,fingerprint);mac(mini_master_key,h,64,tag);return crypto_equal(h+8,fingerprint,32)&&crypto_equal(h+64,tag,32);}
static int commit_header(uint32_t a,uint32_t gen){uint8_t h[96],check[96];memcpy(h,"MINIF2v2",8);derive("storage-header",0,h+8);memcpy(h+40,epoch,16);put32(h+56,gen);put32(h+60,serial);mac(mini_master_key,h,64,h+64);return flash_write(a+4,h+4,92)&&flash_read(a+4,check+4,92)&&crypto_equal(h+4,check+4,92)&&flash_write(a,h,4)&&header(a,check);}
static int read_record(uint32_t a,credential *c){uint8_t b[512],k[32],tag[32];memset(c,0,sizeof(*c));if(legacy){if(!flash_read(a,b,256)||memcmp(b,"MREC",4))return 0;derive("storage-auth",0,k);mac(k,b+4,116,tag);if(!crypto_equal(tag,b+120,32))return 0;crypto_cipher(b+36,84,b+20);memcpy(c->id,b+4,16);memcpy(c->key,b+36,32);memcpy(c->rp,b+68,32);c->counter=get32(b+100);}
 else {if(!flash_read(a,b,512)||memcmp(b,"MRV2",4))return 0;derive("storage-auth-v2",0,k);mac(k,b+4,380,tag);if(!crypto_equal(tag,b+384,32))return 0;crypto_cipher(b+40,344,b+24);memcpy(c->id,b+8,16);memcpy(c->key,b+40,32);memcpy(c->rp,b+72,32);c->counter=get32(b+104);c->resident=b[108];c->user_len=b[109];if(c->user_len>64)return 0;memcpy(c->user,b+110,64);memcpy(c->name,b+174,64);memcpy(c->display,b+238,64);}
 memset(b,0,sizeof(b));return 1;}
static int add_index(const credential *c,uint32_t a){if(c->resident==128){pin_address=a;return 1;}unsigned i;for(i=0;i<count_;i++){if(crypto_equal(index_[i].id,c->id,16))break;if(c->resident==1&&crypto_equal(index_[i].rp,c->rp,32)){credential old;if(read_record(index_[i].address,&old)&&old.resident==1&&old.user_len==c->user_len&&crypto_equal(old.user,c->user,c->user_len))break;}}if(i==count_){if(count_==STORE_CAPACITY)return 0;count_++;}memcpy(index_[i].id,c->id,16);memcpy(index_[i].rp,c->rp,32);index_[i].address=a;return 1;}
static int scan(void){uint8_t b[512];credential c;count_=used=pin_address=0;for(unsigned i=0;i<(legacy?240:SLOTS);i++){uint32_t a=base+4096+i*(legacy?256:512);if(!flash_read(a,b,legacy?256:512))return 0;if(!blank(b,legacy?256:512)){used=i+1;if(!legacy&&get32(b+4)!=UINT32_MAX&&get32(b+4)>serial)serial=get32(b+4);}if(read_record(a,&c)&&!add_index(&c,a))return 0;crypto_yield();}memset(&c,0,sizeof(c));return 1;}
int store_init(void){uint8_t a[96],b[96],tag[32],fp[32];ready=legacy=0;int ha=header(STORE_BASE,a),hb=header(STORE_BASE+BANK,b);if(ha||hb){uint8_t *h=(!ha||(hb&&get32(b+56)>get32(a+56)))?b:a;base=h==b?STORE_BASE+BANK:STORE_BASE;generation=get32(h+56);serial=get32(h+60);memcpy(epoch,h+40,16);}
 else {if(!flash_read(STORE_BASE,a,64)||memcmp(a,"MINIF2v1",8))return 0;derive("storage-header",0,fp);mac(mini_master_key,a,56,tag);if(!crypto_equal(fp,a+8,32)||!crypto_equal(tag,a+56,8))return 0;base=STORE_BASE;generation=0;serial=240;memcpy(epoch,a+40,16);legacy=1;}
 if(!scan())return 0;ready=1;return 1;}
static int write_record(uint32_t a,const credential *c,uint32_t seq){uint8_t b[512],check[512],iv[32],k[32];memset(b,255,512);put32(b+4,seq);memcpy(b+8,c->id,16);unique("record-iv-v2",seq,iv);memcpy(b+24,iv,16);memset(b+40,0,344);memcpy(b+40,c->key,32);memcpy(b+72,c->rp,32);put32(b+104,c->counter);b[108]=c->resident;b[109]=c->user_len;memcpy(b+110,c->user,64);memcpy(b+174,c->name,64);memcpy(b+238,c->display,64);crypto_cipher(b+40,344,b+24);derive("storage-auth-v2",0,k);mac(k,b+4,380,b+384);int ok=flash_write(a,b,512)&&flash_read(a,check,512)&&crypto_equal(b,check,512)&&flash_write(a,"MRV2",4);memset(b,0,512);return ok;}
static int compact(void){if(generation==UINT32_MAX||serial>UINT32_MAX-128)return 0;
 /* Do not erase a target overlapping a live legacy source. */
 if(legacy&&used>112)return 0;uint32_t target=base==STORE_BASE?STORE_BASE+BANK:STORE_BASE;for(unsigned a=target;a<target+BANK;a+=4096)if(!flash_erase(a))return 0;
 credential c;unsigned j=0;for(unsigned i=0;i<count_;i++){if(!read_record(index_[i].address,&c)||!write_record(target+4096+j++*512,&c,++serial))return 0;crypto_yield();}if(pin_address){if(!read_record(pin_address,&c)||!write_record(target+4096+j++*512,&c,++serial))return 0;}
 if(!commit_header(target,generation+1))return 0;memset(&c,0,sizeof(c));return store_init();}
unsigned store_used(void){return used;}unsigned store_count(void){return count_;}
int store_at(unsigned i,credential *c){return ready&&i<count_&&read_record(index_[i].address,c);}
int store_find(const uint8_t id[16],const uint8_t rp[32],credential *c){if(ready)for(unsigned i=0;i<count_;i++)if(crypto_equal(id,index_[i].id,16)&&crypto_equal(rp,index_[i].rp,32))return read_record(index_[i].address,c);memset(c,0,sizeof(*c));return 0;}
int store_resident(const uint8_t rp[32],const uint8_t *user,unsigned n,credential *c){for(unsigned i=0;i<count_;i++)if(crypto_equal(rp,index_[i].rp,32)&&store_at(i,c)&&c->resident==1&&c->user_len==n&&crypto_equal(c->user,user,n))return 1;return 0;}
int store_save(const credential *c){if(!ready)return 0;int known=c->resident==128;for(unsigned i=0;i<count_;i++)if(crypto_equal(c->id,index_[i].id,16))known=1;if(!known&&count_>=STORE_CAPACITY){credential old;if(c->resident!=1||!store_resident(c->rp,c->user,c->user_len,&old))return 0;}if((legacy||used>=SLOTS)&&!compact())return 0;if(serial==UINT32_MAX)return 0;uint32_t a=base+4096+used++*512;if(!write_record(a,c,++serial))return 0;return add_index(c,a);}
int store_create(const uint8_t rp[32],credential *c,uint8_t pub[64]){if(!ready)return 0;credential old;int replacing=c->resident==1&&store_resident(rp,c->user,c->user_len,&old);if(count_>=STORE_CAPACITY&&!replacing)return 0;if((legacy||used>=SLOTS)&&!compact())return 0;uint8_t id[32];unique("credential-id-v2",serial+1,id);memcpy(c->id,id,16);unique("credential-private-v2",serial+1,c->key);memcpy(c->rp,rp,32);c->counter=0;if(!crypto_public(c->key,pub))return 0;return store_save(c);}
int store_pin_get(credential *c){return ready&&pin_address&&read_record(pin_address,c);}
int store_pin_save(const credential *c){return c->resident==128&&store_save(c);}
int store_reset(void){uint8_t input[80],tag[32];ready=0;if(!flash_read(STORE_BASE,input,64))return 0;memcpy(input+64,"reset-v2",8);uint32_t t=board_millis();memcpy(input+72,&t,4);memset(input+76,0,4);mac(mini_master_key,input,80,tag);memcpy(epoch,tag,16);serial=0;for(unsigned a=STORE_BASE;a<STORE_BASE+STORE_SIZE;a+=4096)if(!flash_erase(a))return 0;if(!commit_header(STORE_BASE,1))return 0;return store_init();}

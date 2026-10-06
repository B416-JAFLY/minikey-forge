#include "pin.h"
#include "crypto.h"
#include "platform.h"
#include "store.h"
#include <string.h>
static uint8_t agreement_private[32],agreement_public[64],token[32];
static int initialized,token_valid;static unsigned consecutive,auth_failures;static uint32_t token_since;
static int integer(cv m,int k,int *value){cv v;return cb_get(m,k,0,&v)==1&&cb_int(v,value);}
static int blob(cv m,int k,const uint8_t **p,size_t *n){cv v;return cb_get(m,k,0,&v)==1&&cb_bytes(v,2,p,n);}
static void rotate(void){do{crypto_random(agreement_private);}while(!crypto_public(agreement_private,agreement_public));}
void pin_init(void){memset(agreement_private,0,32);memset(token,0,32);initialized=token_valid=consecutive=auth_failures=0;}
int pin_is_set(void){credential c;int result=store_pin_get(&c);memset(&c,0,sizeof(c));return result;}
static unsigned error(uint8_t *out,unsigned code){out[0]=code;return 1;}
static int shared(cv m,uint8_t secret[32]){cv key;int x;const uint8_t *p;size_t n;uint8_t pub[64];if(cb_get(m,3,0,&key)!=1||!integer(key,1,&x)||x!=2||!integer(key,3,&x)||x!=-25||!integer(key,-1,&x)||x!=1||!blob(key,-2,&p,&n)||n!=32)return 0;memcpy(pub,p,32);if(!blob(key,-3,&p,&n)||n!=32)return 0;memcpy(pub+32,p,32);return crypto_ecdh(agreement_private,pub,secret);}
static int utf8_count(const uint8_t *p,unsigned n){unsigned count=0;for(unsigned i=0;i<n;){uint32_t v;unsigned len;uint8_t a=p[i];if(a<0x80){if(!a)return -1;len=1;v=a;}else if(a>=0xc2&&a<=0xdf){len=2;v=a&31;}else if(a>=0xe0&&a<=0xef){len=3;v=a&15;}else if(a>=0xf0&&a<=0xf4){len=4;v=a&7;}else return -1;if(i+len>n)return -1;for(unsigned j=1;j<len;j++){if((p[i+j]&0xc0)!=0x80)return -1;v=(v<<6)|(p[i+j]&63);}if((len==3&&v<0x800)||(len==4&&v<0x10000)||(v>=0xd800&&v<=0xdfff)||v>0x10ffff)return -1;i+=len;count++;}return count;}
static int check_old(credential *c,const uint8_t secret[32],const uint8_t encrypted[16]){if(!c->counter)return 0x32;if(consecutive>=3)return 0x34;c->counter--;if(!store_pin_save(c))return 0x7f;uint8_t hash[16];memcpy(hash,encrypted,16);crypto_cbc(hash,16,secret,0);int match=crypto_equal(hash,c->key,16);memset(hash,0,16);if(!match){token_valid=0;consecutive++;rotate();return !c->counter?0x32:(consecutive>=3?0x34:0x31);}c->counter=8;if(!store_pin_save(c))return 0x7f;consecutive=0;return 0;}
size_t pin_request(cv m,uint8_t *out,size_t cap){int proto,sub;credential config={0};int exists=store_pin_get(&config);cw w={out,1,cap,0};out[0]=0;if(!integer(m,1,&proto)||proto!=1||!integer(m,2,&sub))return error(out,2);
 if(sub==1){cb_put(&w,5,1);cb_put(&w,0,3);cb_put(&w,0,exists?config.counter:8);return w.n;}
 if(!initialized){rotate();crypto_random(token);initialized=1;}
 if(sub==2){cb_put(&w,5,1);cb_put(&w,0,1);cb_put(&w,5,5);cb_put(&w,0,1);cb_put(&w,0,2);cb_put(&w,0,3);cb_put(&w,1,24);cb_put(&w,1,0);cb_put(&w,0,1);cb_put(&w,1,1);cb_blob(&w,agreement_public,32);cb_put(&w,1,2);cb_blob(&w,agreement_public+32,32);return w.n;}
 if(sub!=3&&sub!=4&&sub!=5)return error(out,2);if(sub==3&&exists)return error(out,0x30);if(sub!=3&&!exists)return error(out,0x35);if(exists&&!config.counter)return error(out,0x32);if(exists&&consecutive>=3)return error(out,0x34);
 uint8_t secret[32],buffer[80],tag[32];const uint8_t *newpin=0,*oldhash=0,*auth=0;size_t nn=0,on=0,an=0;if(!shared(m,secret))return error(out,2);
 if(sub==3||sub==4){if(!blob(m,5,&newpin,&nn)||nn!=64||!blob(m,4,&auth,&an)||an!=16)return error(out,2);memcpy(buffer,newpin,nn);if(sub==4){if(!blob(m,6,&oldhash,&on)||on!=16)return error(out,2);memcpy(buffer+64,oldhash,16);}mac(secret,buffer,sub==4?80:64,tag);if(!crypto_equal(tag,auth,16))return error(out,0x33);}
 else if(!blob(m,6,&oldhash,&on)||on!=16)return error(out,2);
 if(sub!=3){int status=check_old(&config,secret,oldhash);if(status)return error(out,status);}
 if(sub==5){crypto_random(token);token_valid=1;token_since=board_millis();auth_failures=0;memcpy(buffer,token,32);crypto_cbc(buffer,32,secret,1);cb_put(&w,5,1);cb_put(&w,0,2);cb_blob(&w,buffer,32);}
 else {memcpy(buffer,newpin,64);crypto_cbc(buffer,64,secret,0);unsigned length=64;while(length&&buffer[length-1]==0)length--;if(length>63||utf8_count(buffer,length)<4)return error(out,0x37);memset(&config,0,sizeof(config));config.resident=128;config.counter=8;config.user_len=length;sha(buffer,length,tag);memcpy(config.key,tag,16);if(!store_pin_save(&config))return error(out,0x7f);crypto_random(token);token_valid=0;auth_failures=0;}
 memset(buffer,0,sizeof(buffer));memset(secret,0,32);memset(&config,0,sizeof(config));return w.error?error(out,0x7f):w.n;}
int pin_authorize(cv m,int param_key,int proto_key,const uint8_t hash[32],int *verified){*verified=0;cv v;int found=cb_get(m,param_key,0,&v);if(found<0)return 0x12;if(!found)return 0;const uint8_t *p;size_t n;if(!cb_bytes(v,2,&p,&n))return 0x12;if(!n){if(!board_presence())return operation_cancelled?0x2d:0x2f;return pin_is_set()?0x36:0x35;}if(!pin_is_set())return 0x35;int proto;if(n!=16||!integer(m,proto_key,&proto)||proto!=1)return 0x33;if(auth_failures>=3)return 0x34;if(!token_valid||(uint32_t)(board_millis()-token_since)>600000)return 0x33;uint8_t tag[32];mac(token,hash,32,tag);if(!crypto_equal(tag,p,16)){auth_failures++;return auth_failures>=3?0x34:0x33;}auth_failures=0;*verified=1;return 0;}

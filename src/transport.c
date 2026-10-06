#include "transport.h"
#include "ctap.h"
#include "platform.h"
#include <string.h>
#define CHANNEL 0x4d494e49u
static uint8_t request[CTAP_MAX],response[CTAP_MAX];
static uint16_t total,received;
static uint8_t command,sequence;
static uint32_t started,last_keepalive;
static int active,working,allocated;
volatile int operation_cancelled;
static uint32_t be(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void put(uint8_t *p,uint32_t x){p[0]=x>>24;p[1]=x>>16;p[2]=x>>8;p[3]=x;}
static void send(uint32_t cid,uint8_t cmd,const uint8_t *p,unsigned n){uint8_t b[64]={0};put(b,cid);b[4]=cmd|128;b[5]=n>>8;b[6]=n;unsigned first=n<57?n:57;if(first)memcpy(b+7,p,first);if(!usb_write(b))return;unsigned pos=first;uint8_t seq=0;
 while(pos<n){memset(b+4,0,60);b[4]=seq++;unsigned take=n-pos<59?n-pos:59;memcpy(b+5,p+pos,take);if(!usb_write(b))return;pos+=take;}}
static void error(uint32_t cid,uint8_t code){send(cid,0x3f,&code,1);}
void transport_reset(void){active=working=allocated=operation_cancelled=0;total=received=sequence=0;}
void transport_receive(const uint8_t b[64]){uint32_t cid=be(b);uint8_t cmd=b[4]&127;
 if((b[4]&128)&&cmd==6){unsigned len=((unsigned)b[5]<<8)|b[6];if(len!=8){error(cid,3);return;}if(cid!=0xffffffffu&&(!allocated||cid!=CHANNEL)){error(cid,0x0b);return;}if(working){error(cid,6);return;}allocated=1;active=0;uint8_t info[17];memcpy(info,b+7,8);put(info+8,CHANNEL);info[12]=2;info[13]=0;info[14]=1;info[15]=0;info[16]=0x0d;send(cid,6,info,17);return;}
 if(!allocated||cid!=CHANNEL){error(cid,0x0b);return;}
 if((b[4]&128)&&cmd==0x11){if(b[5]||b[6]){error(cid,3);return;}if(working)operation_cancelled=1;else active=0;return;}
 if(working){error(cid,6);return;}
 if(b[4]&128){if(active){error(cid,6);return;}total=((unsigned)b[5]<<8)|b[6];if(total>CTAP_MAX){error(cid,3);return;}command=cmd;sequence=0;received=total<57?total:57;memcpy(request,b+7,received);active=1;started=board_millis();}
 else{if(!active){error(cid,4);return;}if(b[4]!=sequence++){active=0;error(cid,4);return;}unsigned take=total-received<59?total-received:59;memcpy(request+received,b+5,take);received+=take;}
 if(received!=total)return;active=0;working=1;operation_cancelled=0;last_keepalive=board_millis();
 if(command==1)send(cid,1,request,total);
 else if(command==8){if(total)error(cid,3);else{board_ui(3);send(cid,8,0,0);}}
 else if(command==0x10){size_t n=ctap_request(request,total,response,sizeof(response));send(cid,0x10,response,n);}
 else if(command==0x40){if(total!=11||memcmp(request,"MINI-ISP-v1",11))error(cid,2);else{board_ui(1);if(board_presence()){send(cid,0x40,(const uint8_t*)"OK",2);board_request_isp();}else error(cid,0x05);}}
 else error(cid,1);working=0;memset(request,0,sizeof(request));memset(response,0,sizeof(response));}
void transport_poll(void){uint8_t b[64];while(usb_read(b))transport_receive(b);if(active&&(uint32_t)(board_millis()-started)>3000){active=0;error(CHANNEL,5);}}
void transport_tick(void){if(working&&(uint32_t)(board_millis()-last_keepalive)>=80){uint8_t status=1;extern unsigned mini_ui_state;status=mini_ui_state==1?2:1;send(CHANNEL,0x3b,&status,1);last_keepalive=board_millis();}if(working)transport_poll();board_service();}
void crypto_yield(void){transport_tick();}

#include "transport.h"
#include "ctap.h"
#include "cbor.h"
#include "platform.h"
#include <string.h>
#define CHANNEL 0x4d494e49u
static uint8_t request[CTAP_MAX],response[CTAP_MAX];
static uint16_t total,received;
static uint8_t command,sequence;
static uint32_t started,last_keepalive;
static int active,working,allocated,bkp_ok;
volatile int operation_cancelled;
static uint32_t be(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void put(uint8_t *p,uint32_t x){p[0]=x>>24;p[1]=x>>16;p[2]=x>>8;p[3]=x;}
static void send(uint32_t cid,uint8_t cmd,const uint8_t *p,unsigned n){uint8_t b[64]={0};put(b,cid);b[4]=cmd|128;b[5]=n>>8;b[6]=n;unsigned first=n<57?n:57;if(first)memcpy(b+7,p,first);if(!usb_write(b))return;unsigned pos=first;uint8_t seq=0;
 while(pos<n){memset(b+4,0,60);b[4]=seq++;unsigned take=n-pos<59?n-pos:59;memcpy(b+5,p+pos,take);if(!usb_write(b))return;pos+=take;}}
static void error(uint32_t cid,uint8_t code);
/* Protocol debug log: 32-byte records appended to DEBUG_LOG_BASE; holds request shape/status only, never PINs, tokens or keys. */
static uint32_t log_pos=0xffffffffu;
static void log_put(uint8_t *r){if(log_pos==0xffffffffu){uint8_t b[32];log_pos=4096;for(uint32_t o=0;o<4096;o+=32){if(!flash_read(DEBUG_LOG_BASE+o,b,32))return;unsigned i=0;while(i<32&&b[i]==255)i++;if(i==32){log_pos=o;break;}}if(log_pos<4096){uint8_t boot[32];memset(boot,0,32);boot[0]=0xb0;put(boot+7,board_millis());flash_write(DEBUG_LOG_BASE+log_pos,boot,32);log_pos+=32;}}if(log_pos>=4096)return;put(r+7,board_millis());flash_write(DEBUG_LOG_BASE+log_pos,r,32);log_pos+=32;}
static void log_event(uint8_t type,uint8_t a,uint8_t b,unsigned n){uint8_t r[32];memset(r,0,32);r[0]=type;r[1]=a;r[2]=n>>8;r[3]=n;r[4]=b;log_put(r);}
static void log_ctap(const uint8_t *q,unsigned n,const uint8_t *s,unsigned sn){uint8_t r[32];memset(r,0,32);r[0]=0x10;r[1]=n?q[0]:0;r[2]=n>>8;r[3]=n;r[4]=sn?s[0]:0xff;r[5]=sn>>8;r[6]=sn;r[14]=0xff;cv m={q+1,n?n-1:0},v,o;unsigned ty;uint32_t val;size_t hd;
 if(n>1&&cb_valid(m)&&cb_head(m,&ty,&val,&hd)&&ty==5){uint8_t c=q[0];unsigned bits=0;for(int k=1;k<=15;k++)if(cb_get(m,k,0,&v)==1)bits|=1u<<k;r[11]=bits>>8;r[12]=bits;
  if((c==1||c==2)&&cb_get(m,c==1?7:5,0,&o)==1){static const char *names[3]={"up","uv","rk"};for(int i=0;i<3;i++)if(cb_get(o,0,names[i],&v)==1)r[13]|=(1u<<(i*2))|((v.n==1&&v.p[0]==0xf5)?2u<<(i*2):0);}
  const uint8_t *p;size_t pn;int x;
  if(c==1||c==2){if(cb_get(m,c==1?8:6,0,&v)==1&&cb_bytes(v,2,&p,&pn))r[14]=pn>255?255:pn;if(cb_get(m,c==1?9:7,0,&v)==1&&cb_int(v,&x))r[15]=x;if(cb_get(m,c==1?5:3,0,&v)==1&&cb_head(v,&ty,&val,&hd)&&ty==4){r[16]=val>255?255:val;cv it,id;if(cb_at(v,0,&it)&&cb_get(it,0,"id",&id)==1&&cb_bytes(id,2,&p,&pn))r[17]=pn>255?255:pn;}
   if(c==1){if(cb_get(m,2,0,&v)==1&&cb_get(v,0,"id",&o)==1&&cb_bytes(o,3,&p,&pn))memcpy(r+18,p,pn<14?pn:14);}else if(cb_get(m,1,0,&v)==1&&cb_bytes(v,3,&p,&pn))memcpy(r+18,p,pn<14?pn:14);
   if(c==1&&cb_get(m,6,0,&v)==1)r[13]|=64;if(c==2&&cb_get(m,4,0,&v)==1)r[13]|=64;}
  else if(c==6){
    if(cb_get(m,1,0,&v)==1&&cb_int(v,&x))r[15]=x;
    if(cb_get(m,2,0,&v)==1&&cb_int(v,&x))r[16]=x;
    if(cb_get(m,9,0,&v)==1&&cb_int(v,&x))r[17]=x;
    if(cb_get(m,10,0,&v)==1&&cb_bytes(v,3,&p,&pn))memcpy(r+18,p,pn<14?pn:14);
  }}
 log_put(r);}
static void error(uint32_t cid,uint8_t code){log_event(0x3f,command,code,total);send(cid,0x3f,&code,1);}
void transport_reset(void){active=working=allocated=bkp_ok=operation_cancelled=0;total=received=sequence=0;log_pos=0xffffffffu;}
void transport_receive(const uint8_t b[64]){uint32_t cid=be(b);uint8_t cmd=b[4]&127;
 if((b[4]&128)&&cmd==6){unsigned len=((unsigned)b[5]<<8)|b[6];if(len!=8){error(cid,3);return;}if(cid!=0xffffffffu&&(!allocated||cid!=CHANNEL)){error(cid,0x0b);return;}if(working){error(cid,6);return;}allocated=1;active=0;uint8_t info[17];memcpy(info,b+7,8);put(info+8,CHANNEL);info[12]=2;info[13]=0;info[14]=1;info[15]=0;info[16]=0x0d;send(cid,6,info,17);log_event(0x06,cid==0xffffffffu,0,len);return;}
 if(!allocated||cid!=CHANNEL){error(cid,0x0b);return;}
 if((b[4]&128)&&cmd==0x11){log_event(0x11,working,0,0);if(b[5]||b[6]){error(cid,3);return;}if(working)operation_cancelled=1;else active=0;return;}
 if(working){error(cid,6);return;}
 if(b[4]&128){if(active){error(cid,6);return;}total=((unsigned)b[5]<<8)|b[6];if(total>CTAP_MAX){error(cid,3);return;}command=cmd;sequence=0;received=total<57?total:57;memcpy(request,b+7,received);active=1;started=board_millis();}
 else{if(!active){error(cid,4);return;}if(b[4]!=sequence++){active=0;error(cid,4);return;}unsigned take=total-received<59?total-received:59;memcpy(request+received,b+5,take);received+=take;}
 if(received!=total)return;active=0;working=1;operation_cancelled=0;last_keepalive=board_millis();
 if(command!=0x10&&command!=0x40)log_event(0x20,command,0,total);
 if(command==1)send(cid,1,request,total);
 else if(command==8){if(total)error(cid,3);else{board_ui(3);send(cid,8,0,0);}}
 else if(command==0x10){size_t n=ctap_request(request,total,response,sizeof(response));send(cid,0x10,response,n);log_ctap(request,total,response,n);}
 else if(command==0x40){if(total==11&&!memcmp(request,"MINI-ISP-v1",11)){board_ui(1);if(board_presence()){board_ui(2);send(cid,0x40,(const uint8_t*)"OK",2);board_request_isp();}else{board_ui(0);error(cid,0x05);}}
  else if((total==13&&(!memcmp(request,"MINI-BKP-v1",11)||!memcmp(request,"MINI-LOG-v1",11)))||(total==11&&!memcmp(request,"MINI-LOGCLR",11))){int log=request[5]=='L';unsigned off=total==13?((unsigned)request[11]<<8)|request[12]:0;if((off&511)||(log&&off>=4096))error(cid,2);else if(!bkp_ok&&(board_ui(1),!board_presence())){board_ui(0);error(cid,0x05);}
   else{bkp_ok=1;if(total==11){if(flash_erase(DEBUG_LOG_BASE)){log_pos=0xffffffffu;board_ui(3);send(cid,0x40,(const uint8_t*)"OK",2);}else error(cid,0x7f);}else if(flash_read((log?DEBUG_LOG_BASE:0xF0000u)+off,response,512)){board_ui((!log&&off==0xFE00u)?3:0);send(cid,0x40,response,512);}else error(cid,0x7f);}}
  else error(cid,2);}
 else error(cid,1);working=0;memset(request,0,sizeof(request));memset(response,0,sizeof(response));}
void transport_poll(void){uint8_t b[64];while(usb_read(b))transport_receive(b);if(active&&(uint32_t)(board_millis()-started)>3000){active=0;error(CHANNEL,5);}}
void transport_tick(void){if(working&&(uint32_t)(board_millis()-last_keepalive)>=80){uint8_t status=1;extern unsigned mini_ui_state;status=mini_ui_state==1?2:1;send(CHANNEL,0x3b,&status,1);last_keepalive=board_millis();}if(working)transport_poll();board_service();}
void crypto_yield(void){transport_tick();}

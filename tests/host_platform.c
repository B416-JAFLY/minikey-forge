#include "platform.h"
#include "transport.h"
#include "store.h"
#include "ctap.h"
#include "pin.h"
#include "crypto.h"
#include <string.h>
#define EXPORT __declspec(dllexport)
/* Test key only; distinct from device key in private/provision.c. */
const uint8_t mini_master_key[32]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32};
static uint8_t flash[0x100000],packets[128][64];
static int packet_count,presence_ok=1,fail_after=-1,fail_erase=-1;
static uint32_t ms;
static int isp_requested;
static unsigned boot_nonce;
unsigned mini_ui_state;
EXPORT size_t host_ctap(const uint8_t *p,size_t n,uint8_t *out,size_t cap){return ctap_request(p,n,out,cap);}
EXPORT void host_poll(void){transport_poll();}
EXPORT int host_isp_requested(void){return isp_requested;}
void board_request_isp(void){isp_requested=1;}
EXPORT void host_init(void){memset(flash,255,sizeof(flash));packet_count=0;presence_ok=1;fail_after=fail_erase=-1;isp_requested=0;ms=0;transport_reset();store_reset();uint8_t seed[32]={0};seed[0]=++boot_nonce;crypto_seed(seed);pin_init();ctap_reset_state();}
EXPORT void host_reboot(void){transport_reset();ms=0;store_init();uint8_t seed[32]={0};seed[0]=++boot_nonce;crypto_seed(seed);pin_init();ctap_reset_state();}
EXPORT void host_presence(int ok){presence_ok=ok;}
EXPORT void host_time(unsigned now){ms=now;}
EXPORT void host_fail_write(int after){fail_after=after;}
EXPORT void host_fail_erase(int after){fail_erase=after;}
EXPORT int host_flash(uint32_t a,void *p,unsigned n){return flash_read(a,p,n);}
EXPORT void host_corrupt(uint32_t a,uint8_t mask){flash[a]^=mask;}
EXPORT void host_packet(const uint8_t *p){transport_receive(p);}
EXPORT int host_pop(uint8_t *p){if(!packet_count)return 0;memcpy(p,packets[0],64);memmove(packets,packets+1,(--packet_count)*64);return 1;}
void board_init(void){}
uint32_t board_millis(void){return ms;}
void board_service(void){}
int board_presence(void){return presence_ok;}
void board_ui(unsigned s){mini_ui_state=s;}
int flash_read(uint32_t a,void *p,size_t n){if(a>sizeof(flash)||n>sizeof(flash)-a)return 0;memcpy(p,flash+a,n);return 1;}
int flash_write(uint32_t a,const void *p,size_t n){if(a>sizeof(flash)||n>sizeof(flash)-a)return 0;const uint8_t *b=p;for(size_t i=0;i<n;i++){if(fail_after==0)return 0;if(fail_after>0)fail_after--;flash[a+i]&=b[i];}return 1;}
int flash_erase(uint32_t a){if(fail_erase==0)return 0;if(fail_erase>0)fail_erase--;if(a>=sizeof(flash)||(a&4095))return 0;memset(flash+a,255,4096);return 1;}
int usb_read(uint8_t p[64]){(void)p;return 0;}
int usb_write(const uint8_t p[64]){if(packet_count==128)return 0;memcpy(packets[packet_count++],p,64);return 1;}


EXPORT void host_load_flash(uint32_t a,const void *p,unsigned n){if(a+n<=sizeof(flash))memcpy(flash+a,p,n);}
EXPORT unsigned host_store_used(void){return store_used();}
EXPORT unsigned host_store_count(void){return store_count();}

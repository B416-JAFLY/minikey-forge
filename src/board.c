#include "platform.h"
#include "transport.h"
#include "ch32x035_usbfs_device.h"
#include "usbd_compatibility_hid.h"
#include "store.h"
#include <string.h>
#include "crypto.h"
#include "pin.h"
uint8_t HID_Report_Buffer[64] __attribute__((aligned(4)));
volatile uint8_t HID_Set_Report_Flag;
unsigned mini_ui_state;
static uint32_t touch_base;
static uint32_t ticks_last,ticks_remainder,time_ms;
static int flash_ok;
static int isp_pending;
static uint32_t isp_at;
static void output(GPIO_TypeDef *g,unsigned p,int high){if(high)g->BSHR=1u<<p;else g->BCR=1u<<p;}
static void mode(GPIO_TypeDef *g,unsigned p,unsigned m){GPIO_InitTypeDef c={0};c.GPIO_Pin=1u<<p;c.GPIO_Mode=m;c.GPIO_Speed=GPIO_Speed_50MHz;GPIO_Init(g,&c);}
uint32_t board_millis(void){uint32_t now=(uint32_t)SysTick->CNT;uint32_t elapsed=now-ticks_last;ticks_last=now;uint32_t per=SystemCoreClock/8000;uint64_t sum=(uint64_t)elapsed+ticks_remainder;time_ms+=sum/per;ticks_remainder=sum%per;return time_ms;}
static void delay(unsigned ms){uint32_t begin=board_millis();while((uint32_t)(board_millis()-begin)<ms);}
void Delay_Us(uint32_t n){uint32_t start=(uint32_t)SysTick->CNT;uint32_t ticks=n*(SystemCoreClock/8000000);while((uint32_t)((uint32_t)SysTick->CNT-start)<ticks);}
static uint8_t spi(uint8_t b){uint8_t value=0;for(unsigned bit=0;bit<8;bit++){output(GPIOA,7,(b&128)!=0);b<<=1;output(GPIOA,5,1);value=(value<<1)|((GPIOA->INDR>>6)&1);output(GPIOA,5,0);}return value;}
static void flash_begin(void){output(GPIOA,3,1);output(GPIOB,7,1);mode(GPIOA,6,GPIO_Mode_IN_FLOATING);output(GPIOB,7,0);}
static void flash_end(void){output(GPIOB,7,1);}
static void address(uint32_t a){spi(a>>16);spi(a>>8);spi(a);}
static int flash_wait(void){uint32_t start=board_millis();for(;;){flash_begin();spi(5);uint8_t status=spi(255);flash_end();if(!(status&1))return 1;if((uint32_t)(board_millis()-start)>3000)return 0;transport_tick();}}
static void write_enable(void){flash_begin();spi(6);flash_end();}
int flash_read(uint32_t a,void *data,size_t n){if(!flash_ok||a>0x100000||n>0x100000-a)return 0;uint8_t *p=data;flash_begin();spi(3);address(a);while(n--)*p++=spi(255);flash_end();return 1;}
int flash_write(uint32_t a,const void *data,size_t n){if(!flash_ok||a<STORE_BASE||a>0x100000||n>0x100000-a)return 0;const uint8_t *p=data;
 while(n){size_t take=256-(a&255);if(take>n)take=n;if(!flash_wait())return 0;write_enable();flash_begin();spi(2);address(a);for(size_t i=0;i<take;i++)spi(p[i]);flash_end();if(!flash_wait())return 0;a+=take;p+=take;n-=take;}return 1;}
int flash_erase(uint32_t a){if(!flash_ok||a<STORE_BASE||a>=0x100000||(a&4095))return 0;if(!flash_wait())return 0;write_enable();flash_begin();spi(0x20);address(a);flash_end();return flash_wait();}
void board_ui(unsigned state){mini_ui_state=state;}
void board_request_isp(void){isp_pending=1;isp_at=board_millis()+150;}
void board_service(void){uint32_t now=board_millis();if(isp_pending&&(int32_t)(now-isp_at)>=0){__disable_irq();USBFSD->BASE_CTRL=0;AFIO->CTLR&=~(USB_IOEN|UDP_PUE_MASK);FLASH->KEYR=0x45670123;FLASH->KEYR=0xcdef89ab;FLASH->BOOT_MODEKEYR=0x45670123;FLASH->BOOT_MODEKEYR=0xcdef89ab;FLASH->STATR|=1u<<14;FLASH->CTLR|=1u<<7;NVIC_SystemReset();for(;;);}
}
static unsigned touch(void){ADC_RegularChannelConfig(ADC1,ADC_Channel_9,1,ADC_SampleTime_11Cycles);TKey1->IDATAR1=0x80;TKey1->RDATAR=8;uint32_t start=board_millis();while(!ADC_GetFlagStatus(ADC1,ADC_FLAG_EOC)){if(board_millis()-start>10)return touch_base;}return TKey1->RDATAR;}
int board_presence(void){uint32_t start=board_millis(),stable=0;int released=0;while(board_millis()-start<30000){transport_tick();if(operation_cancelled)return 0;unsigned v=touch();int pressed=v+100<touch_base;
 if(!pressed){released=1;stable=0;}else if(released){if(!stable)stable=board_millis();if(board_millis()-stable>=35)return 1;}delay(2);}return 0;}
int usb_read(uint8_t b[64]){if(!USBFS_DevEnumStatus||!RingBuffer_Comm.RemainPack)return 0;
 NVIC_DisableIRQ(USBFS_IRQn);unsigned idx=RingBuffer_Comm.DealPtr;unsigned n=RingBuffer_Comm.PackLen[idx];memcpy(b,Data_Buffer+idx*64,64);RingBuffer_Comm.DealPtr=(idx+1)%DEF_Ring_Buffer_Max_Blks;RingBuffer_Comm.RemainPack--;if(RingBuffer_Comm.StopFlag&&RingBuffer_Comm.RemainPack<DEF_RING_BUFFER_RESTART){RingBuffer_Comm.StopFlag=0;USBFSD->UEP1_CTRL_H=(USBFSD->UEP1_CTRL_H&~USBFS_UEP_R_RES_MASK)|USBFS_UEP_R_RES_ACK;}NVIC_EnableIRQ(USBFS_IRQn);if(n!=64)return 0;return 1;}
int usb_write(const uint8_t b[64]){uint32_t start=board_millis();while(USBFS_Endp_Busy[2]){if(!USBFS_DevEnumStatus||board_millis()-start>500)return 0;board_service();}if(!USBFS_DevEnumStatus)return 0;memcpy(USBFS_EP2_Buf,b,64);USBFS_Endp_Busy[2]=1;USBFSD->UEP2_TX_LEN=64;USBFSD->UEP2_CTRL_H=(USBFSD->UEP2_CTRL_H&~USBFS_UEP_T_RES_MASK)|USBFS_UEP_T_RES_ACK;return 1;}
void board_init(void){SystemCoreClockUpdate();SysTick->CTLR=0;SysTick->CNT=0;SysTick->CTLR=1;
 transport_reset();USBFS_RCC_Init();USBFS_Device_Init(ENABLE,PWR_VDD_3V3);
 RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA|RCC_APB2Periph_GPIOB|RCC_APB2Periph_GPIOC|RCC_APB2Periph_ADC1,ENABLE);
 mode(GPIOA,0,GPIO_Mode_Out_PP);mode(GPIOA,3,GPIO_Mode_Out_PP);mode(GPIOA,5,GPIO_Mode_Out_PP);mode(GPIOA,7,GPIO_Mode_Out_PP);mode(GPIOB,7,GPIO_Mode_Out_PP);output(GPIOA,3,1);output(GPIOB,7,1);output(GPIOA,5,0);output(GPIOA,0,0); /* Hold LCD reset; no display traffic. */
 flash_begin();spi(0x9f);uint8_t manufacturer=spi(255),type=spi(255),density=spi(255);flash_end();(void)manufacturer;(void)type;flash_ok=density==0x14;
 mode(GPIOB,1,GPIO_Mode_AIN);ADC_CLKConfig(ADC1,ADC_CLK_Div6);ADC_InitTypeDef a={0};a.ADC_Mode=ADC_Mode_Independent;a.ADC_DataAlign=ADC_DataAlign_Right;a.ADC_NbrOfChannel=1;ADC_Init(ADC1,&a);ADC_Cmd(ADC1,ENABLE);TKey1->CTLR1|=1u<<24;touch_base=0;for(int i=0;i<16;i++){touch_base+=touch();delay(2);}touch_base/=16;
 uint8_t noise[32]={0};for(unsigned i=0;i<128;i++){unsigned v=touch()^(uint32_t)SysTick->CNT;noise[i%32]^=v;noise[(i+7)%32]^=v>>8;}crypto_seed(noise);memset(noise,0,sizeof(noise));
 board_ui(store_init()?0:5);pin_init();
}

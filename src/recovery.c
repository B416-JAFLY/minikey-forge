/* Independent first-stage recovery, at 0x0000. App starts at 0x2000.
 * No USB or LCD dependencies: fresh touch held for 700ms in the 3s boot window
 * requests the FACTORY ISP via the WCH-documented BOOT_MODE bit.
 * Never changes option bytes or writes the factory bootloader region. */
#include "ch32x035.h"
#include "system_ch32x035.h"
#define APP_ADDRESS 0x2000u
static unsigned millis(void){return (unsigned)SysTick->CNT/(SystemCoreClock/8000);}
static unsigned sample(void){unsigned start=millis();ADC_RegularChannelConfig(ADC1,ADC_Channel_9,1,ADC_SampleTime_11Cycles);TKey1->IDATAR1=0x80;TKey1->RDATAR=8;while(!ADC_GetFlagStatus(ADC1,ADC_FLAG_EOC))if(millis()-start>5)return 0;return TKey1->RDATAR;}
static void isp(void){__disable_irq();FLASH->KEYR=0x45670123;FLASH->KEYR=0xcdef89ab;FLASH->BOOT_MODEKEYR=0x45670123;FLASH->BOOT_MODEKEYR=0xcdef89ab;FLASH->STATR|=1u<<14;FLASH->CTLR|=1u<<7;NVIC_SystemReset();for(;;);}
int main(void){SystemCoreClockUpdate();SysTick->CTLR=0;SysTick->CNT=0;SysTick->CTLR=1;RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB|RCC_APB2Periph_ADC1,ENABLE);GPIO_InitTypeDef g={0};g.GPIO_Pin=GPIO_Pin_1;g.GPIO_Mode=GPIO_Mode_AIN;GPIO_Init(GPIOB,&g);ADC_CLKConfig(ADC1,ADC_CLK_Div6);ADC_InitTypeDef a={0};a.ADC_Mode=ADC_Mode_Independent;a.ADC_DataAlign=ADC_DataAlign_Right;a.ADC_NbrOfChannel=1;ADC_Init(ADC1,&a);ADC_Cmd(ADC1,ENABLE);TKey1->CTLR1|=1u<<24;
 unsigned baseline=0;for(int i=0;i<16;i++)baseline+=sample();baseline/=16;unsigned start=millis(),held=0;while(millis()-start<3000){unsigned v=sample();if(v&&v+100<baseline){if(!held)held=millis();if(millis()-held>=700)isp();}else held=0;}
 /* Blank/corrupt application entry: use factory ISP instead of jumping. */
 if(*(volatile uint32_t*)APP_ADDRESS==0xffffffffu||*(volatile uint32_t*)APP_ADDRESS==0)isp();__disable_irq();SysTick->CTLR=0;RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1,DISABLE);void (*app)(void)=(void(*)(void))APP_ADDRESS;app();isp();return 0;}

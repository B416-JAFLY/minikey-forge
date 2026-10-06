#pragma once
#include <stdint.h>
#define SET_REPORT_WAIT_DEAL 1
#define SET_REPORT_DEAL_OVER 0
extern uint8_t HID_Report_Buffer[64];
extern volatile uint8_t HID_Set_Report_Flag;

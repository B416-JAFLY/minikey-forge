#pragma once
#include <stdint.h>
#include <stddef.h>
void board_init(void);
uint32_t board_millis(void);
void board_service(void);
int board_presence(void);
void board_ui(unsigned state);
int flash_read(uint32_t address,void *p,size_t n);
int flash_write(uint32_t address,const void *p,size_t n);
int flash_erase(uint32_t address);
int usb_read(uint8_t packet[64]);
int usb_write(const uint8_t packet[64]);
extern volatile int operation_cancelled;
void crypto_yield(void);
void board_request_isp(void);
/* Internal-flash key, provisioned once per prototype; never exposed via USB. */
extern const uint8_t mini_master_key[32];

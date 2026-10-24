#include "platform.h"
#include "transport.h"
#include "ch32x035_usbfs_device.h"
#include "usbd_compatibility_hid.h"
#include "store.h"
#include <string.h>
#include "crypto.h"
#include "pin.h"
#include "ui.h"

uint8_t HID_Report_Buffer[64] __attribute__((aligned(4)));
volatile uint8_t HID_Set_Report_Flag;
unsigned mini_ui_state;

static uint32_t last_ui, ui_since, touch_base;
static unsigned rendered_state = 99, rendered_card_key = 999;
static int touch_active, usb_was_enum;
static int lcd_xoff = 1, lcd_yoff = 26;
static uint32_t ticks_last, ticks_remainder, time_ms;
static int flash_ok;
static int isp_pending;
static uint32_t isp_at;

static void output(GPIO_TypeDef *g, unsigned p, int high) {
    if (high) g->BSHR = 1u << p;
    else g->BCR = 1u << p;
}

static void mode(GPIO_TypeDef *g, unsigned p, unsigned m) {
    GPIO_InitTypeDef c = {0};
    c.GPIO_Pin = 1u << p;
    c.GPIO_Mode = m;
    c.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(g, &c);
}

uint32_t board_millis(void) {
    uint32_t now = (uint32_t)SysTick->CNT;
    uint32_t elapsed = now - ticks_last;
    ticks_last = now;
    uint32_t per = SystemCoreClock / 8000;
    uint64_t sum = (uint64_t)elapsed + ticks_remainder;
    time_ms += (uint32_t)(sum / per);
    ticks_remainder = (uint32_t)(sum % per);
    return time_ms;
}

static void delay(unsigned ms) {
    uint32_t begin = board_millis();
    while ((uint32_t)(board_millis() - begin) < ms) {}
}

void Delay_Us(uint32_t n) {
    uint32_t start = (uint32_t)SysTick->CNT;
    uint32_t ticks = n * (SystemCoreClock / 8000000);
    while ((uint32_t)((uint32_t)SysTick->CNT - start) < ticks) {}
}

static uint8_t spi(uint8_t b) {
    while (!(SPI1->STATR & 2u)) {}
    SPI1->DATAR = b;
    while (!(SPI1->STATR & 1u)) {}
    uint8_t rx = (uint8_t)SPI1->DATAR;
    while (SPI1->STATR & 0x80u) {}
    return rx;
}

static void lcd_cs(int high) {
    output(GPIOC, 3, high);
    output(GPIOA, 3, high);
}

static void flash_begin(void) {
    lcd_cs(1);
    output(GPIOB, 7, 1);
    GPIOA->CFGLR = (GPIOA->CFGLR & ~(0xFu << 24)) | (0x8u << 24);
    GPIOA->BSHR = 1u << 6;
    output(GPIOB, 7, 0);
}

static void flash_end(void) {
    output(GPIOB, 7, 1);
    GPIOA->CFGLR = (GPIOA->CFGLR & ~(0xFu << 24)) | (0x3u << 24);
    GPIOA->BSHR = 1u << 6;
}

static void address(uint32_t a) {
    spi((uint8_t)(a >> 16));
    spi((uint8_t)(a >> 8));
    spi((uint8_t)a);
}

static int flash_wait(void) {
    uint32_t start = board_millis();
    for (;;) {
        flash_begin();
        spi(5);
        uint8_t status = spi(255);
        flash_end();
        if (!(status & 1)) return 1;
        if ((uint32_t)(board_millis() - start) > 3000) return 0;
        transport_tick();
    }
}

static void write_enable(void) {
    flash_begin();
    spi(6);
    flash_end();
}

int flash_read(uint32_t a, void *data, size_t n) {
    if (!flash_ok || a > 0x100000 || n > 0x100000 - a) return 0;
    uint8_t *p = data;
    flash_begin();
    spi(3);
    address(a);
    while (n--) *p++ = spi(255);
    flash_end();
    return 1;
}

int flash_write(uint32_t a, const void *data, size_t n) {
    if (!flash_ok || a < STORE_BASE || a > 0x100000 || n > 0x100000 - a) return 0;
    const uint8_t *p = data;
    while (n) {
        size_t take = 256 - (a & 255);
        if (take > n) take = n;
        if (!flash_wait()) return 0;
        write_enable();
        flash_begin();
        spi(2);
        address(a);
        for (size_t i = 0; i < take; i++) spi(p[i]);
        flash_end();
        if (!flash_wait()) return 0;
        a += (uint32_t)take;
        p += take;
        n -= take;
    }
    return 1;
}

int flash_erase(uint32_t a) {
    if (!flash_ok || a < STORE_BASE || a >= 0x100000 || (a & 4095)) return 0;
    if (!flash_wait()) return 0;
    write_enable();
    flash_begin();
    spi(0x20);
    address(a);
    flash_end();
    return flash_wait();
}

static void lcd_begin(int data) {
    output(GPIOB, 7, 1);
    GPIOA->CFGLR = (GPIOA->CFGLR & ~(0xFu << 24)) | (0x3u << 24);
    output(GPIOA, 6, data);
    lcd_cs(0);
}

static void lcd_cmd(uint8_t c, const uint8_t *p, unsigned n) {
    lcd_begin(0);
    spi(c);
    if (n) {
        output(GPIOA, 6, 1);
        for (unsigned i = 0; i < n; i++) spi(p[i]);
    }
    lcd_cs(1);
}

static void window(unsigned x, unsigned y, unsigned w, unsigned h) {
    int x0 = (int)x + lcd_xoff, x1 = (int)(x + w - 1) + lcd_xoff;
    int y0 = (int)y + lcd_yoff, y1 = (int)(y + h - 1) + lcd_yoff;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    uint8_t xa[4] = {0, (uint8_t)x0, 0, (uint8_t)x1};
    uint8_t ya[4] = {0, (uint8_t)y0, 0, (uint8_t)y1};
    lcd_cmd(0x2a, xa, 4);
    lcd_cmd(0x2b, ya, 4);
    lcd_cmd(0x2c, 0, 0);
}

static void fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t color) {
    window(x, y, w, h);
    lcd_begin(1);
    uint8_t hi = (uint8_t)(color >> 8), lo = (uint8_t)color;
    for (unsigned i = 0; i < w * h; i++) {
        spi(hi);
        spi(lo);
    }
    lcd_cs(1);
}

static void draw_hero(unsigned state, uint32_t age, int td) {
    window(8, 8, 64, 64);
    lcd_begin(1);
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            uint16_t c = ui_hero_pixel(x, y, state, age, td);
            spi((uint8_t)(c >> 8));
            spi((uint8_t)c);
        }
    }
    lcd_cs(1);
}

static void draw_card(unsigned state, uint32_t age, int td) {
    window(78, 10, 76, 60);
    lcd_begin(1);
    for (int y = 0; y < 60; y++) {
        for (int x = 0; x < 76; x++) {
            uint16_t c = ui_card_pixel(x, y, state, age, td);
            spi((uint8_t)(c >> 8));
            spi((uint8_t)c);
        }
    }
    lcd_cs(1);
}

static void draw_bar(unsigned state, uint32_t age, int td) {
    window(84, 56, 64, 6);
    lcd_begin(1);
    for (int y = 0; y < 6; y++) {
        for (int x = 0; x < 64; x++) {
            uint16_t c = ui_bar_pixel(x, state, age, td);
            spi((uint8_t)(c >> 8));
            spi((uint8_t)c);
        }
    }
    lcd_cs(1);
}

void board_ui(unsigned state) {
    if (state != mini_ui_state) {
        unsigned prev = mini_ui_state;
        mini_ui_state = state;
        uint32_t now = board_millis();
        /* Returning to idle after an action enters ambient idle directly without re-running boot intro */
        ui_since = (state == 0 && prev != 0 && now > 900) ? (now - 900) : now;
        last_ui = 0;
        rendered_state = 99;
        board_service();
    }
}

void board_request_isp(void) {
    isp_pending = 1;
    isp_at = board_millis() + 150;
}

void board_service(void) {
    uint32_t now = board_millis();
    if (isp_pending && (int32_t)(now - isp_at) >= 0) {
        __disable_irq();
        USBFSD->BASE_CTRL = 0;
        AFIO->CTLR &= ~(USB_IOEN | UDP_PUE_MASK);
        FLASH->KEYR = 0x45670123;
        FLASH->KEYR = 0xcdef89ab;
        FLASH->BOOT_MODEKEYR = 0x45670123;
        FLASH->BOOT_MODEKEYR = 0xcdef89ab;
        FLASH->STATR |= 1u << 14;
        FLASH->CTLR |= 1u << 7;
        NVIC_SystemReset();
        for (;;) {}
    }
    if (USBFS_DevEnumStatus && !usb_was_enum) {
        usb_was_enum = 1;
        if (mini_ui_state == 0 && now > 900) {
            ui_since = now;
            rendered_state = 99;
            last_ui = 0;
        }
    }
    uint32_t age = now - ui_since;
    if (mini_ui_state == 3 && age > 1800) {
        board_ui(0);
        return;
    }
    if (mini_ui_state == 4 && age > 1600) {
        board_ui(0);
        return;
    }
    unsigned interval = (mini_ui_state == 2) ? 45 : 28;
    if (rendered_state == mini_ui_state && (uint32_t)(now - last_ui) < interval) return;

    unsigned state = mini_ui_state;
    int td = (state == 1) ? touch_active : 0;
    unsigned card_key = (state << 4) | ((state == 0 && age >= 900) ? 2u : 0u) | (unsigned)(td ? 1u : 0u);

    draw_hero(state, age, td);
    if (rendered_state != state || rendered_card_key != card_key) {
        draw_card(state, age, td);
        rendered_state = state;
        rendered_card_key = card_key;
    } else {
        draw_bar(state, age, td);
    }
    last_ui = board_millis();
}

static unsigned touch(void) {
    ADC_RegularChannelConfig(ADC1, ADC_Channel_9, 1, ADC_SampleTime_11Cycles);
    TKey1->IDATAR1 = 0x80;
    TKey1->RDATAR = 8;
    uint32_t start = board_millis();
    while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC)) {
        if (board_millis() - start > 10) return touch_base;
    }
    return TKey1->RDATAR;
}

int board_presence(void) {
    board_ui(1);
    uint32_t start = board_millis(), stable = 0;
    int released = 0;
    touch_active = 0;
    while (board_millis() - start < 30000) {
        transport_tick();
        if (operation_cancelled) {
            touch_active = 0;
            return 0;
        }
        unsigned v = touch();
        int pressed = v + 100 < touch_base;
        touch_active = pressed && released;
        if (!pressed) {
            released = 1;
            stable = 0;
        } else if (released) {
            if (!stable) stable = board_millis();
            if (board_millis() - stable >= 35) {
                touch_active = 0;
                return 1;
            }
        }
        delay(2);
    }
    touch_active = 0;
    return 0;
}

int usb_read(uint8_t b[64]) {
    if (!USBFS_DevEnumStatus) return 0;
    if (HID_Set_Report_Flag) {
        NVIC_DisableIRQ(USBFS_IRQn);
        memcpy(b, HID_Report_Buffer, 64);
        HID_Set_Report_Flag = 0;
        NVIC_EnableIRQ(USBFS_IRQn);
        return 1;
    }
    if (!RingBuffer_Comm.RemainPack) return 0;
    NVIC_DisableIRQ(USBFS_IRQn);
    unsigned idx = RingBuffer_Comm.DealPtr;
    unsigned n = RingBuffer_Comm.PackLen[idx];
    memcpy(b, Data_Buffer + idx * 64, 64);
    RingBuffer_Comm.DealPtr = (idx + 1) % DEF_Ring_Buffer_Max_Blks;
    RingBuffer_Comm.RemainPack--;
    if (RingBuffer_Comm.StopFlag && RingBuffer_Comm.RemainPack < DEF_RING_BUFFER_RESTART) {
        RingBuffer_Comm.StopFlag = 0;
        USBFSD->UEP1_CTRL_H = (USBFSD->UEP1_CTRL_H & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_ACK;
    }
    NVIC_EnableIRQ(USBFS_IRQn);
    if (n != 64) return 0;
    return 1;
}

int usb_write(const uint8_t b[64]) {
    uint32_t start = board_millis();
    while (USBFS_Endp_Busy[2]) {
        if (!USBFS_DevEnumStatus || board_millis() - start > 500) return 0;
        board_service();
    }
    if (!USBFS_DevEnumStatus) return 0;
    memcpy(USBFS_EP2_Buf, b, 64);
    USBFS_Endp_Busy[2] = 1;
    USBFSD->UEP2_TX_LEN = 64;
    USBFSD->UEP2_CTRL_H = (USBFSD->UEP2_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_ACK;
    return 1;
}

static void lcd_init(void) {
    int xoff = 1, yoff = 26, inv = 1, orient = 1;
    uint8_t cal[8];
    if (flash_ok && flash_read(0xfff8, cal, 8) && cal[4] >= 1 && cal[4] <= 5) {
        int8_t xo = (int8_t)cal[0];
        if (xo >= -3 && xo <= 3) xoff = xo;
        if (cal[1] <= 40) yoff = cal[1];
        inv = (cal[2] == 1);
        if (cal[3] <= 1) orient = cal[3];
    }
    lcd_xoff = xoff;
    lcd_yoff = yoff;

    lcd_cs(1);
    output(GPIOA, 6, 1);
    output(GPIOA, 0, 1);
    delay(10);
    output(GPIOA, 0, 0);
    delay(20);
    output(GPIOA, 0, 1);
    delay(120);

    lcd_cmd(0x01, 0, 0);
    delay(120);
    lcd_cmd(0x11, 0, 0);
    delay(120);

    static const uint8_t b1[] = {0x01, 0x2C, 0x2D};
    static const uint8_t b3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
    static const uint8_t b4[] = {0x07};
    static const uint8_t c0[] = {0xA2, 0x02, 0x84};
    static const uint8_t c1[] = {0xC5};
    static const uint8_t c2[] = {0x0A, 0x00};
    static const uint8_t c3[] = {0x8A, 0x2A};
    static const uint8_t c4[] = {0x8A, 0xEE};
    static const uint8_t c5[] = {0x0E};
    static const uint8_t e0[] = {0x0F, 0x1A, 0x0F, 0x18, 0x2F, 0x28, 0x20, 0x22, 0x1F, 0x1B, 0x23, 0x37, 0x00, 0x07, 0x02, 0x10};
    static const uint8_t e1[] = {0x0F, 0x1B, 0x0F, 0x17, 0x33, 0x2C, 0x29, 0x2E, 0x30, 0x30, 0x39, 0x3F, 0x00, 0x07, 0x03, 0x10};
    uint8_t madctl = orient ? 0xA8 : 0x68;
    uint8_t colmod = 0x05;

    lcd_cmd(0xB1, b1, sizeof(b1));
    lcd_cmd(0xB2, b1, sizeof(b1));
    lcd_cmd(0xB3, b3, sizeof(b3));
    lcd_cmd(0xB4, b4, sizeof(b4));
    lcd_cmd(0xC0, c0, sizeof(c0));
    lcd_cmd(0xC1, c1, sizeof(c1));
    lcd_cmd(0xC2, c2, sizeof(c2));
    lcd_cmd(0xC3, c3, sizeof(c3));
    lcd_cmd(0xC4, c4, sizeof(c4));
    lcd_cmd(0xC5, c5, sizeof(c5));
    lcd_cmd(inv ? 0x21 : 0x20, 0, 0);
    lcd_cmd(0x36, &madctl, 1);
    lcd_cmd(0x3A, &colmod, 1);
    lcd_cmd(0xE0, e0, sizeof(e0));
    lcd_cmd(0xE1, e1, sizeof(e1));
    lcd_cmd(0x29, 0, 0);
    delay(10);
    fill(0, 0, 160, 80, 0x0843);
}

void board_init(void) {
    SystemCoreClockUpdate();
    SysTick->CTLR = 0;
    SysTick->CNT = 0;
    SysTick->CTLR = 1;
    transport_reset();
    USBFS_RCC_Init();
    USBFS_Device_Init(ENABLE, PWR_VDD_3V3);

    RCC_APB2PeriphClockCmd(
        RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC |
        RCC_APB2Periph_ADC1 | RCC_APB2Periph_SPI1,
        ENABLE);

    mode(GPIOA, 0, GPIO_Mode_Out_PP);
    mode(GPIOA, 3, GPIO_Mode_Out_PP);
    mode(GPIOA, 5, GPIO_Mode_AF_PP);
    mode(GPIOA, 6, GPIO_Mode_Out_PP);
    mode(GPIOA, 7, GPIO_Mode_AF_PP);
    mode(GPIOB, 7, GPIO_Mode_Out_PP);
    mode(GPIOC, 3, GPIO_Mode_Out_PP);
    lcd_cs(1);
    output(GPIOB, 7, 1);
    output(GPIOA, 6, 1);
    output(GPIOA, 0, 1);

    SPI1->CTLR1 = 0x034C; /* Master, CPOL=0, CPHA=0, SSM=1, SSI=1, SPE=1, fPCLK/4 (12MHz) */
    SPI1->CRCR = 7;

    flash_begin();
    spi(0x9f);
    uint8_t manufacturer = spi(255), type = spi(255), density = spi(255);
    flash_end();
    (void)manufacturer;
    (void)type;
    flash_ok = (density == 0x14);

    lcd_init();

    mode(GPIOB, 1, GPIO_Mode_AIN);
    ADC_CLKConfig(ADC1, ADC_CLK_Div6);
    ADC_InitTypeDef a = {0};
    a.ADC_Mode = ADC_Mode_Independent;
    a.ADC_DataAlign = ADC_DataAlign_Right;
    a.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &a);
    ADC_Cmd(ADC1, ENABLE);
    TKey1->CTLR1 |= 1u << 24;
    touch_base = 0;
    for (int i = 0; i < 16; i++) {
        touch_base += touch();
        delay(2);
    }
    touch_base /= 16;

    uint8_t noise[32] = {0};
    for (unsigned i = 0; i < 128; i++) {
        unsigned v = touch() ^ (uint32_t)SysTick->CNT;
        noise[i % 32] ^= (uint8_t)v;
        noise[(i + 7) % 32] ^= (uint8_t)(v >> 8);
    }
    crypto_seed(noise);
    memset(noise, 0, sizeof(noise));

    int ok = store_init();
    pin_init();
    mini_ui_state = 99;
    ui_since = board_millis();
    board_ui(ok ? 0 : 5);
}

// A better X68000 keyboard adaptor for 2025
// by RetroSwim.

// Credits:
// TinyUSB HID Host implementation example:
// https://github.com/raspberrypi/pico-examples/tree/master/usb/host/host_cdc_msc_hid
//
// x68key by Zuofo and Guddler
// https://github.com/Guddler/x68Key
//
// Requirements:
// - Raspberry Pi Pico
// - Level shifter (only really necessary for the input, the X68K keyboard port receives 3v3 just fine)
// - USB OTG adaptor
// - VS Code with Raspberry Pi Pico extension
//
// Wiring:
// - UART0 TXD (pin 16, GP12) - Pin 2 (Mouse Data)
// - UART1 TXD (pin 6, GP4)   - Pin 3 (Keyboard Interface RX)
// - UART1 RXD (pin 7, GP5)   - Level Shifter - Pin 4 (Keyboard Interface TX)
// - VBUS (pin 40)            - Pin 1 (+5VDC)
// - GND (pin 3,8,13,18,etc)  - Pin 7 (GND)
//



#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "tusb.h"
#include "PicoX68Key.h"
#include "bsp/board_api.h"

#include "include/layout_us.h"

#define KB_UART_ID uart1
#define KB_BAUD_RATE 2400

#define KB_UART_TX_PIN 4
#define KB_UART_RX_PIN 5

#define MOUSE_UART_ID uart0
#define MOUSE_BAUD_RATE 4800

#define MOUSE_UART_TX_PIN 12
#define MOUSE_UART_RX_PIN 13

#define MOUSE_DIVIDER 0x03

void press(uint8_t c);
void keyDown(uint8_t c);
void keyUp(uint8_t c);
void doRepeat();
void stopRepeat();
bool timerCallback(struct repeating_timer *t);

// void typeCodeForDebug(uint8_t c);
// void testMessage()

extern void hid_app_task(void);

uint8_t newKeyCode = 0;
uint8_t downKeyCode = 0;
uint32_t keyDownTime = 0;
uint16_t repeatDelay = 500;   // ms before first repeat
struct repeating_timer repeatTimer;

// Key repeat timer code
bool timerCallback(struct repeating_timer *t) {
    doRepeat();
    return true;
}

// Press a key
void press(uint8_t c) {
    keyDown(c);
    sleep_ms(10);
    keyUp(c);
    sleep_ms(10);
}

// Send the bytes to the X68000's keyboard interface
void keyDown(uint8_t c) {
    uart_putc(KB_UART_ID, c);
}

void keyUp(uint8_t c) {
    uart_putc(KB_UART_ID, c | 0x80);
}

// True while Left GUI (Windows) key is being held. Unlocks additional keys.
bool isSpecial = false;

void setSpecial(bool enabled) {
    isSpecial = enabled;
}

// Translate keystrokes from USB Boot Protocol "Usages" to X68000 scan codes

bool keyIsDown = false;

void handleKey(uint8_t keycode, uint8_t state) {
    
    newKeyCode = keymapping[keycode];

    if(isSpecial) {
        for(uint8_t i = 0; i < sizeof(altKeysUSB); i++) {
            if(altKeysUSB[i] == keycode) {
                newKeyCode = altKeyCodes[i];
            }
        }
    }

    if(newKeyCode == 0) return;
    
    if(state == USBKEY_PRESSED) {
        keyDown(newKeyCode);
        downKeyCode = newKeyCode;
        keyDownTime = to_ms_since_boot(get_absolute_time());
        keyIsDown = true;
    }else if(state == USBKEY_RELEASED) {
        keyUp(newKeyCode);
        if(newKeyCode == downKeyCode) keyIsDown = false;
    }
}

// Accumulate deltas from USB HID Mouse reports.
uint8_t mouseButtons = 0;
int16_t mouseDx = 0, mouseDy = 0;

void handleMouse(uint8_t buttons, int8_t x, int8_t y) {
    mouseDx += x / MOUSE_DIVIDER;
    mouseDy += y / MOUSE_DIVIDER;
    mouseButtons = buttons;
}

void doRepeat() {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if(keyIsDown && (now - keyDownTime) >= repeatDelay) {
        keyDown(downKeyCode);
    }
}

int main()
{
    uint8_t lastByte = 0;
    
    tuh_hid_set_default_protocol(HID_PROTOCOL_BOOT);
    tuh_init(BOARD_TUH_RHPORT);
    board_init_after_tusb();

    uart_init(KB_UART_ID, KB_BAUD_RATE);
    uart_init(MOUSE_UART_ID, MOUSE_BAUD_RATE);
    uart_set_format(MOUSE_UART_ID, 8, 2, UART_PARITY_NONE);
    
    gpio_set_function(KB_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(KB_UART_RX_PIN, GPIO_FUNC_UART);

    gpio_set_function(MOUSE_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(MOUSE_UART_RX_PIN, GPIO_FUNC_UART);


    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    add_repeating_timer_ms(500, timerCallback, NULL, &repeatTimer);
    
    while (true) {
        tuh_task();
        hid_app_task();

        // Serial port messages should be rare, so not worth making this interrupt driven.
        while(uart_is_readable(KB_UART_ID)){

            const uint8_t thisByte = uart_getc(KB_UART_ID);

            // 0x4x replicates the MSCTRL pin on the mouse port. Bit 0 falling means poll now.
            // 0b01000001 then 0b01000000 = poll
            if(thisByte == 0x40 && lastByte == 0x41){
                uint8_t mousePacket[3];
                uint8_t xOvp = 0, xOvn = 0, yOvp = 0, yOvn = 0;

                // Mouse deltas have 10 bits of precision, packed awkwardly into 3 bytes.
                if (mouseDx > 127)  xOvp = 1;
                if (mouseDx < -128) xOvn = 1;
                if (mouseDy > 127)	yOvp = 1;
                if (mouseDy < -128) yOvn = 1;

                mousePacket[0] = (yOvn << 7) | (yOvp << 6) | (xOvn << 5) | (xOvp << 4) | mouseButtons;
                mousePacket[1] = mouseDx;
                mousePacket[2] = mouseDy;
                mouseDx = 0;
                mouseDy = 0;
                for (int i = 0; i < 3; i++) uart_putc(MOUSE_UART_ID, mousePacket[i]);

            }

            // 0x8x sets the keyboard LEDs.
            // 0b 1 xxx xxxx
            if(thisByte & 0x80) {
                // CAPS -> CAPS
                // INS -> NUMLOCK
                // FULLWIDTH -> SCROLL LOCK
                // I guess?

                set_leds((thisByte >> 4) & 1, (thisByte >> 3) & 1, (thisByte >> 6) & 1);

            }

            if((thisByte & 0xf0) == 0x60)
            {
                const uint16_t rateByte = thisByte & 0x0f; 
                const uint16_t rateMs = 30 + ((rateByte ^ 2) * 5);
                repeatTimer.delay_us = (uint64_t)rateMs * 1000;
            }

            if((thisByte & 0xf0) == 0x70)
            {
                const uint16_t delayByte = thisByte & 0x0f; 
                const uint16_t delayMs = 200 + (delayByte * 100);
                repeatDelay = delayMs;
            }

            lastByte = thisByte;
        }
    }

}

// void typeCodeForDebug(uint8_t c) {
//
//   static const uint8_t hexDigitToKeycodeLut[] = {0x0B, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x1E, 0x2E, 0x2C, 0x20, 0x13, 0x21};
//   const uint8_t highNibble = (c & 0xf0) >> 4;
//   const uint8_t lowNibble = c & 0x0f;

//   press(hexDigitToKeycodeLut[highNibble]);
//   press(hexDigitToKeycodeLut[lowNibble]);
    
//   press(0x35);

// }

// void testMessage() {

//     static const uint8_t message[] = { 0x23, 0x13, 0x26, 0x26, 0x19, 0x35, 0x26, 0x20 };

//     for(uint8_t i = 0; i < sizeof(message); i++) {
//         press(message[i]);
//     }

//     keyDown(0x70);
//     press(0x02);
//     keyUp(0x70);

// }
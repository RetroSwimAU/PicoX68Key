// Uncomment to print messages to stdio
// #define DEBUG
#define USBKEY_PRESSED  1
#define USBKEY_HELD     2
#define USBKEY_RELEASED 4

// Camel case in my new code
void handleKey(uint8_t keycode, uint8_t state);
void handleMouse(uint8_t buttons, int8_t x, int8_t y);
void setSpecial(bool enabled);

// Debugging tools
void ledOn(bool isOn);
void littleBlink();

// This case used in hid_app.c to keep style consistent
void set_leds(uint8_t led_mask);

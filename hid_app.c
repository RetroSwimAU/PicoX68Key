/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021, Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "bsp/board_api.h"
#include "tusb.h"
#include "PicoX68Key.h"

// Modified for brevity, for full explanation, see original source:
// https://github.com/raspberrypi/pico-examples/blob/master/usb/host/host_cdc_msc_hid/hid_app.c

//--------------------------------------------------------------------+
// MACRO TYPEDEF CONSTANT ENUM DECLARATION
//--------------------------------------------------------------------+

#define MAX_REPORT  8
// Each HID instance can has multiple reports
static uint8_t _report_count[CFG_TUH_HID];
static tuh_hid_report_info_t _report_info_arr[CFG_TUH_HID][MAX_REPORT];

#define MAX_KEYBOARDS 10
struct keyboard { uint8_t addr; uint8_t instance; };
static struct keyboard keyboards[MAX_KEYBOARDS] =
{ 
  { 0xff, 0xff},
  { 0xff, 0xff},
  { 0xff, 0xff},
  { 0xff, 0xff},
  { 0xff, 0xff},
  { 0xff, 0xff},
  { 0xff, 0xff},
  { 0xff, 0xff},
  { 0xff, 0xff},
  { 0xff, 0xff}
 };

static uint8_t num_keyboards;

static void process_kbd_report(hid_keyboard_report_t const *report);
static void process_mouse_report(hid_mouse_report_t const * report);
static void process_generic_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);

void hid_app_task(void)
{
  // nothing to do
}

static bool got_keyboard = false;
static uint8_t sharp_to_hid_leds = 0;
static uint8_t last_led_mask = 0;

void set_leds(uint8_t led_mask) {
  
  if(led_mask == last_led_mask) return;

  // I came up with the below mapping arbitrarily.
  sharp_to_hid_leds = 0; 
  if(led_mask & 0x40) sharp_to_hid_leds |= KEYBOARD_LED_NUMLOCK;  // WIDE (全角) -> Num Lock
//if(led_mask & 0x20) sharp_to_hid_leds |= 0;
  if(led_mask & 0x10) sharp_to_hid_leds |= KEYBOARD_LED_SCROLLLOCK; // INS -> Scroll Lock
  if(led_mask & 0x08) sharp_to_hid_leds |= KEYBOARD_LED_CAPSLOCK; // CAPS -> Caps Lock
  if(led_mask & 0x04) sharp_to_hid_leds |= KEYBOARD_LED_COMPOSE; // CHORD ENTRY (コード入力) -> Compose ??
//if(led_mask & 0x02) sharp_to_hid_leds |= 0;
  if(led_mask & 0x01) sharp_to_hid_leds |= KEYBOARD_LED_KANA; // KANA (かな) -> Kana ??
  // Don't have a keyboard with compose or kana LEDs to test, so YMMV.

  for(uint8_t i = 0; i < MAX_KEYBOARDS; i++){
    if(keyboards[i].addr != 0xff && keyboards[i].instance != 0xff ){
      tuh_hid_set_report(keyboards[i].addr, keyboards[i].instance, 0, HID_REPORT_TYPE_OUTPUT, &sharp_to_hid_leds, 1);
    }
  }

  last_led_mask = led_mask;
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use. tuh_hid_parse_report_descriptor()
// can be used to parse common/simple enough descriptor.
// Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE, it will be skipped
// therefore report_desc = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
  // Interface protocol (hid_interface_protocol_enum_t)
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  _report_count[instance] = tuh_hid_parse_report_descriptor(_report_info_arr[instance], MAX_REPORT, desc_report, desc_len);
  
  for (int i = 0; i < _report_count[instance]; i++) 
  {
    if ((_report_info_arr[instance][i].usage_page == HID_USAGE_PAGE_DESKTOP) && 
        (_report_info_arr[instance][i].usage == HID_USAGE_DESKTOP_KEYBOARD)) 
    {
      ledOn(true);
      keyboards[num_keyboards].addr = dev_addr;
      keyboards[num_keyboards].instance = instance;
      num_keyboards++;
      num_keyboards %= 10;
    }
  }

  tuh_hid_receive_report(dev_addr, instance);

}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  for(uint8_t i = 0; i < MAX_KEYBOARDS; i++) {
    if(keyboards[i].addr == dev_addr && keyboards[i].instance == instance) {
      keyboards[i].addr = 0xff;
      keyboards[i].instance = 0xff;
    }
  }
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  switch (itf_protocol)
  {
    case HID_ITF_PROTOCOL_KEYBOARD:
      process_kbd_report( (hid_keyboard_report_t const*) report );
    break;

    case HID_ITF_PROTOCOL_MOUSE:
      process_mouse_report( (hid_mouse_report_t const*) report );
    break;

    default:
      // Generic report requires matching ReportID and contents with previous parsed report info
      process_generic_report(dev_addr, instance, report, len);
    break;
  }

  // continue to request to receive report
  tuh_hid_receive_report(dev_addr, instance);

}

//--------------------------------------------------------------------+
// Keyboard
//--------------------------------------------------------------------+

// look up new key in previous keys
static inline bool find_key_in_report(hid_keyboard_report_t const *report, uint8_t keycode)
{
  for(uint8_t i=0; i<6; i++)
  {
    if (report->keycode[i] == keycode)  return true;
  }

  return false;
}

static void process_kbd_report(hid_keyboard_report_t const *report)
{
  
  static hid_keyboard_report_t prev_report = { 0, 0, {0} }; // previous report to check key released

  for(uint8_t i=0; i<6; i++)
  {

    if ( prev_report.keycode[i] ) {
      if( !find_key_in_report(report, prev_report.keycode[i]) ) {
        handleKey(prev_report.keycode[i], USBKEY_RELEASED);
      }
    }

    if ( report->keycode[i] )
    {
      if ( find_key_in_report(&prev_report, report->keycode[i]) )
      {
        handleKey(report->keycode[i], USBKEY_HELD);
      } else {
        handleKey(report->keycode[i], USBKEY_PRESSED);
      }
    }
    
  }

  // Turn the modifiers byte back into scan codes, it's easier this way.
  for(uint8_t i=0; i<8; i++) {
    const bool is_pressed = (report->modifier >> i) & 0x01;
    const bool was_pressed = (prev_report.modifier >> i) & 0x01;
    const uint8_t this_code = 0xE0 + i;

    if(!was_pressed && is_pressed) {
      handleKey(this_code, USBKEY_PRESSED);
      if(i == 3) setSpecial(true);
    }

    if(was_pressed && !is_pressed) {
      handleKey(this_code, USBKEY_RELEASED);
      if(i == 3) setSpecial(false);
    }

    if(was_pressed && is_pressed) handleKey(this_code, USBKEY_HELD);
  }

  prev_report = *report;

}

//--------------------------------------------------------------------+
// Mouse
//--------------------------------------------------------------------+

static void process_mouse_report(hid_mouse_report_t const * report)
{
  uint8_t button_state = 0;
  if(report->buttons & MOUSE_BUTTON_LEFT) button_state += 1;
  if(report->buttons & MOUSE_BUTTON_RIGHT) button_state += 2;
  handleMouse(button_state, report->x, report->y);
}

//--------------------------------------------------------------------+
// Generic Report
//--------------------------------------------------------------------+
static void process_generic_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  
  (void) dev_addr;

  uint8_t const rpt_count = _report_count[instance];
  tuh_hid_report_info_t* rpt_info_arr = _report_info_arr[instance];
  tuh_hid_report_info_t* rpt_info = NULL;

  if ( rpt_count == 1 && rpt_info_arr[0].report_id == 0)
  {
    // Simple report without report ID as 1st byte
    rpt_info = &rpt_info_arr[0];
  }else
  {
    // Composite report, 1st byte is report ID, data starts from 2nd byte
    uint8_t const rpt_id = report[0];

    // Find report id in the array
    for(uint8_t i=0; i<rpt_count; i++)
    {
      if (rpt_id == rpt_info_arr[i].report_id )
      {
        rpt_info = &rpt_info_arr[i];
        break;
      }
    }

    report++;
    len--;
  }

  if (!rpt_info)
  {
    return;
  }

  // For complete list of Usage Page & Usage checkout src/class/hid/hid.h. For examples:
  // - Keyboard                     : Desktop, Keyboard
  // - Mouse                        : Desktop, Mouse
  // - Gamepad                      : Desktop, Gamepad
  // - Consumer Control (Media Key) : Consumer, Consumer Control
  // - System Control (Power key)   : Desktop, System Control
  // - Generic (vendor)             : 0xFFxx, xx
  if ( rpt_info->usage_page == HID_USAGE_PAGE_DESKTOP )
  {
    switch (rpt_info->usage)
    {
      case HID_USAGE_DESKTOP_KEYBOARD:
        // Assume keyboard follow boot report layout
        process_kbd_report( (hid_keyboard_report_t const*) report );
      break;

      case HID_USAGE_DESKTOP_MOUSE:
        // Assume mouse follow boot report layout
        process_mouse_report( (hid_mouse_report_t const*) report );
      break;

      default: break;
    }
  }

}

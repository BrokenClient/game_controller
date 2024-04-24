
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/board.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/uart.h"
#include "host/hcd.h"
#include "host/usbh.h"
#include "host/usbh_classdriver.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pio_usb_ll.h"
#include "tusb.h"
#include "xbox.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

/*------------- MAIN -------------*/

// core1: handle host events
void core1_main() {
    sleep_ms(10);

    // Use tuh_configure() to pass pio configuration to the host stack
    // Note: tuh_configure() must be called before
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

    // To run USB SOF interrupt in core1, init host stack for pio_usb (roothub
    // port1) on core1
    tuh_init(1);
    while (true) {
        tuh_task();  // tinyusb host task
    }
}

// core0: handle device events

xbox_msg* global_msg;
uint8_t global_box[32];
const uint8_t global_box_len = sizeof(global_box) / sizeof(global_box[0]);

const int max_cycles = 65535;
const int max_value = 32000/4;
static uint32_t start_ms = 0;
const uint32_t interval_ms = 200;
volatile int sum_x = 0;
volatile int sum_y = 0;
// 1 -> spin clockwise
volatile int spin = 0;
const uint slice_num1 = 2;
const uint slice_num2 = 4;
const uint slice_num3 = 3;

/* wire connection
4-18   5-19
8-20   9-21
*/
void init() {
    // 1 uart
    uart_init(uart0, 38400);
    gpio_set_function(12, GPIO_FUNC_UART);
    gpio_set_function(13, GPIO_FUNC_UART);

    // 4 pwm out
    gpio_set_function(4, GPIO_FUNC_PWM);
    gpio_set_function(5, GPIO_FUNC_PWM);

    gpio_set_function(8, GPIO_FUNC_PWM);
    gpio_set_function(9, GPIO_FUNC_PWM);

    gpio_set_function(6, GPIO_FUNC_PWM);
    gpio_set_function(7, GPIO_FUNC_PWM);
    
    pwm_set_wrap(slice_num1, max_cycles);
    pwm_set_wrap(slice_num2, max_cycles);
    pwm_set_wrap(slice_num3, max_cycles);

    pwm_set_enabled(slice_num1, true);
    pwm_set_enabled(slice_num2, true);
    pwm_set_enabled(slice_num3, true);

    // 4 digital out
    gpio_init(18);
    gpio_init(19);
    gpio_init(20);
    gpio_init(21);

    gpio_set_dir(18, GPIO_OUT);
    gpio_set_dir(19, GPIO_OUT);
    gpio_set_dir(20, GPIO_OUT);
    gpio_set_dir(21, GPIO_OUT);
}

inline int limit(int min, int x, int max) {
    return x > max ? max : x < min ? min : x;
}

int normal(int minx, int x, int maxx, int min, int max) {
    x = limit(minx, x, maxx);
    return (x - minx) * (max - min) / (maxx - minx)  + min;
}

bool sig(int x) { return x >= 0; }

void set_pwm_dig(int x, int y) {
    // front right & font left for symbol
    // x>=0 -> 1  ||||  x<0 ->0
    int map[4][4] = {{0, 0, 0, 0}, {0, 1, 1, 0}, {1, 0, 0, 1}, {1, 1, 1, 1}};
    // note low speed /4
    x = normal(-max_value, x, max_value, -max_cycles, max_cycles) / 4;
    y = normal(-max_value, y, max_value, -max_cycles, max_cycles) / 4;

    int a = (x + y) >> 1;
    int b = (y - x) >> 1;

    int idx = (sig(a) << 1) + sig(b);
    for (int i = 0; i < 4; i++) {
        gpio_put(18 + i, map[idx][i]);
    }
    char s[128];
    sprintf(s,":%06d %06d\n", a, b);
    uart_puts(uart0, s);

    pwm_set_chan_level(slice_num1, PWM_CHAN_A, abs(a));  // 6
    pwm_set_chan_level(slice_num1, PWM_CHAN_B, abs(b));  // 7
    pwm_set_chan_level(slice_num3, PWM_CHAN_B, abs(a));  // 6
    pwm_set_chan_level(slice_num3, PWM_CHAN_A, abs(a));  // 6
    pwm_set_chan_level(slice_num2, PWM_CHAN_A, abs(b));  // 8
    pwm_set_chan_level(slice_num2, PWM_CHAN_B, abs(a));  // 9
}

int main(void) {
    // default 125MHz is not appropreate. Sysclock should be multiple of 12MHz.
    set_sys_clock_khz(120000, true);

    sleep_ms(10);

    multicore_reset_core1();
    // all USB task run in core1
    multicore_launch_core1(core1_main);

    init();
    while (true) {
        // printf("%02x\n",global_msg->left_tri);
        pwm_set_chan_level(slice_num1, PWM_CHAN_A, global_msg->left_tri);  // 2
        if (board_millis() - start_ms < 200) continue;
        char s[32];
        sprintf(s, ".", global_msg->id);
        uart_puts(uart0, s);
        start_ms += 200;
    }

    return 0;
}

//--------------------------------------------------------------------+
// Host HID
//--------------------------------------------------------------------+

// bc added

void pass(tuh_xfer_t* xfer) { (void)xfer; }

void parse_msg(uint8_t* msg) {
    global_msg = (xbox_msg*)msg;
    // char s[32];
    // sprintf(s,":%d %d\n",global_msg->left_joystick_x,global_msg->left_joystick_y);
    // uart_puts(uart0, s);

    // /4 in case multipy leding into overflow
    set_pwm_dig(global_msg->left_joystick_x/4, global_msg->left_joystick_y/4);
}

void vendor_transfer(uint8_t addr, uint8_t bmRequestType, uint8_t bRequest,
                     uint16_t wValue, uint16_t wIndex, uint16_t wLength) {
    tusb_control_request_t request_1 = {.bmRequestType = bmRequestType,
                                        .bRequest = bRequest,
                                        .wValue = wValue,
                                        .wIndex = wIndex,
                                        .wLength = wLength};
    tuh_xfer_t xfer_1 = {.daddr = addr,
                         .ep_addr = 0x00,
                         .setup = &request_1,
                         .buffer = NULL,
                         .complete_cb = NULL,
                         .user_data = 0};
    tuh_control_xfer(&xfer_1);
}

void tuh_mount_cb(uint8_t addr) {
    uart_puts(uart0, "mounted\n");
    {
        // string
        tuh_descriptor_get_string(addr, 0x03, 0x0409, NULL, 0xff, NULL, 0);
        // vendor
        vendor_transfer(addr, 0xc0, 0x90, 0, 0x04, 0x10);
        vendor_transfer(addr, 0xc0, 0x90, 0, 0x04, 0x28);
        // string
        tuh_descriptor_get_string(addr, 0x00, 0x0000, NULL, 0xff, NULL, 0);
        // string
        tuh_descriptor_get_string(addr, 0x02, 0x0409, NULL, 0xff, NULL, 0);
        // device_qualifier
        tuh_descriptor_get(addr, 0x06, 0, NULL, 0x0a, NULL, 0);
        // device
        tuh_descriptor_get(addr, 0x01, 0, NULL, 0x12, NULL, 0);
        // configuration
        tuh_descriptor_get(addr, 0x02, 0, NULL, 0x09, NULL, 0);
        tuh_descriptor_get(addr, 0x02, 0, NULL, 0x30, NULL, 0);

        // get_status
        {
            static tusb_control_request_t request = {.bmRequestType = 0x80,
                                                     .bRequest = 0x00,
                                                     .wValue = 0x00,
                                                     .wIndex = 0x00,
                                                     .wLength = 0x02};
            tuh_xfer_t xfer = {.daddr = addr,
                               .ep_addr = 0x00,
                               .setup = &request,
                               .buffer = NULL,
                               .complete_cb = NULL,
                               .user_data = 0};
            tuh_control_xfer(&xfer);
        }
        // set configuration
        {
            static tusb_control_request_t request = {.bmRequestType = 0x00,
                                                     .bRequest = 0x09,
                                                     .wValue = 0x01,
                                                     .wIndex = 0x00,
                                                     .wLength = 0x00};
            tuh_xfer_t xfer = {.daddr = addr,
                               .ep_addr = 0x00,
                               .setup = &request,
                               .buffer = NULL,
                               .complete_cb = NULL,
                               .user_data = 0};
            tuh_control_xfer(&xfer);
        }

        // // string
        // tuh_descriptor_get_string(addr, 0x02, 0x0409, NULL, 0x02, NULL, 0);
        // tuh_descriptor_get_string(addr, 0x02, 0x0409, NULL, 0x1e, NULL, 0);
        // vendor
        vendor_transfer(addr, 0xc1, 1, 0x0100, 0, 0x14);
        // vendor
        vendor_transfer(addr, 0xc1, 1, 0, 0, 0x08);
        vendor_transfer(addr, 0xc0, 1, 0, 0, 0x04);
    }
    sleep_ms(5);
    static endpoint_descriptor_t edpt_in = {.length = 0x07,
                                            .type = 0x05,
                                            .epaddr = 0x82,
                                            .attr = 0x03,
                                            .max_size = {0x00, 0x20},
                                            .interval = 0x02};
    pio_usb_host_endpoint_open(0, 0x01, (uint8_t*)&edpt_in, false);
    static endpoint_descriptor_t edpt_out = {.length = 0x07,
                                             .type = 0x05,
                                             .epaddr = 0x02,
                                             .attr = 0x03,
                                             .max_size = {0x00, 0x20},
                                             .interval = 0x08};
    pio_usb_host_endpoint_open(0, 0x01, (uint8_t*)&edpt_out, false);

    static uint8_t out1[3] = {0x01, 0x03, 0x02};
    static uint8_t out2[3] = {0x02, 0x08, 0x03};
    static uint8_t out3[3] = {0x01, 0x03, 0x02};
    pio_usb_host_endpoint_transfer(0, 0x01, edpt_in.epaddr, global_box,
                                   global_box_len);
    sleep_ms(10);
    pio_usb_host_endpoint_transfer(0, 0x01, edpt_out.epaddr, out1, 3);
    sleep_ms(10);
    pio_usb_host_endpoint_transfer(0, 0x01, edpt_out.epaddr, out2, 3);
    sleep_ms(10);
    pio_usb_host_endpoint_transfer(0, 0x01, edpt_in.epaddr, global_box,
                                   global_box_len);
    sleep_ms(10);
    pio_usb_host_endpoint_transfer(0, 0x01, edpt_out.epaddr, out3, 3);
    while (1) {
        sleep_ms(10);
        pio_usb_host_endpoint_transfer(0, 0x01, edpt_in.epaddr, global_box,
                                       global_box_len);
        parse_msg(global_box);
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const* desc_report, uint16_t desc_len) {
    (void)dev_addr;
    (void)instance;
    (void)desc_report;
    (void)desc_len;
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    (void)dev_addr;
    (void)instance;
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const* report, uint16_t len) {
    (void)dev_addr;
    (void)instance;
    (void)report;
    (void)len;
}

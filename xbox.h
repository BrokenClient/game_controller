#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    uint8_t id;
    uint8_t length;
    struct BUTTON_LEFT {
        uint8_t up      : 1;
        uint8_t down    : 1;
        uint8_t left    : 1;
        uint8_t right   : 1;
        uint8_t start   : 1;
        uint8_t back    : 1;
        uint8_t l3      : 1;
        uint8_t r3      : 1;
    }button_left;
    struct BUTTON_RIGHT {
        uint8_t lb      : 1;
        uint8_t rb      : 1;
        uint8_t xbox    : 1;
        uint8_t         : 1;
        uint8_t A       : 1;
        uint8_t B       : 1;
        uint8_t X       : 1;
        uint8_t Y       : 1;
    }button_right;
    uint8_t left_tri;
    uint8_t right_tri;
    int16_t left_joystick_x;
    int16_t left_joystick_y;
    int16_t right_joystick_x;
    int16_t right_joystick_y;
    uint8_t unused[6];
} xbox_msg;



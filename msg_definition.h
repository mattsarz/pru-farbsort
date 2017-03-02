#ifndef MSG_DEFINITION
#define MSG_DEFINITION

#endif // MSG_DEFINITION

typedef enum
{
    CMD_GET_ALL_INFO = 1,
    CMD_RESET = 2,
    CMD_VERBOSE_ON = 3,
    CMD_VERBOSE_OFF = 4,
    CMD_MODE_NORMAL = 5,
    CMD_MODE_DIAGNOSTIC = 6,
    CMD_START = 7,
    CMD_STOP = 8,
    CMD_MOTOR_START = 9,
    CMD_MOTOR_STOP = 0xA,
    CMD_VALVE_1_ON = 0xB,
    CMD_VALVE_1_OFF = 0xC,
    CMD_VALVE_2_ON = 0xD,
    CMD_VALVE_2_OFF = 0xE,
    CMD_VALVE_3_ON = 0xF,
    CMD_VALVE_3_OFF = 0x10,

    INFO_COLOR_BLUE     = 0x21,
    INFO_COLOR_RED      = 0x22,
    INFO_COLOR_WHITE    = 0x23,

    INFO_VERBOSE_ON = 0x80 + 3,
    INFO_VERBOSE_OFF = 0x80 + 4,
    INFO_MODE_NORMAL = 0x80 + 5,
    INFO_MODE_DIAGNOSTIC = 0x80 + 6,
    INFO_CTRL_START = 7,
    INFO_CTRL_STOP = 8,

    INFO_MOTOR_START = 0x80 + 9,
    INFO_MOTOR_STOP = 0x80 + 0xA,
    INFO_VALVE_1_ON = 0x80 + 0xB,
    INFO_VALVE_1_OFF =0x80 + 0xC,
    INFO_VALVE_2_ON = 0x80 + 0xD,
    INFO_VALVE_2_OFF = 0x80 + 0xE,
    INFO_VALVE_3_ON = 0x80 + 0xF,
    INFO_VALVE_3_OFF = 0x80 + 0x10,

    INFO_EMERGENCY_STOP_ON = 0x80 + 0x20,
    INFO_EMERGENCY_STOP_OFF = 0x80 + 0x21,
    INFO_CONVEYER_RUNNING = 0x80 + 0x22,
    INFO_CONVEYER_STOPPED = 0x80 + 0x23,

    INFO_LIGHT_BARRIER_1_DARK = 0x80 + 0x30,
    INFO_LIGHT_BARRIER_1_BRIGHT = 0x80 + 0x31,

    INFO_LIGHT_BARRIER_2_DARK = 0x80 + 0x32,
    INFO_LIGHT_BARRIER_2_BRIGHT = 0x80 + 0x33,

    INFO_COLOR_DETECT = 0x80 + 0x40

} Commands;

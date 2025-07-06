#include "powerSave.h"
#include <globals.h>
#include <interface.h>

#include <Wire.h>

// GPIO expander
#include <ExtensionIOXL9555.hpp>
ExtensionIOXL9555 io;

// Rotary encoder
#include <RotaryEncoder.h>
extern RotaryEncoder *encoder;
IRAM_ATTR void checkPosition();
RotaryEncoder *encoder = nullptr;
IRAM_ATTR void checkPosition() { encoder->tick(); }

// Battery
#define XPOWERS_CHIP_BQ25896
#include <XPowersLib.h>
PowersBQ25896 PPM;

// Fuel gauge
#include <bq27220.h>
BQ27220 bq;

// Keyboard
#include <Adafruit_TCA8418.h>
Adafruit_TCA8418 keyboard;

bool fn_key_pressed = false;
bool shift_key_pressed = false;
bool caps_lock = false;

#define KB_ROWS 4
#define KB_COLS 10

struct KeyValue_t {
    const char value_first;
    const char value_second;
    const char value_third;
};

const KeyValue_t _key_value_map[KB_ROWS][KB_COLS] = {
    {{'q', 'Q', '1'},
     {'w', 'W', '2'},
     {'e', 'E', '3'},
     {'r', 'R', '4'},
     {'t', 'T', '5'},
     {'y', 'Y', '6'},
     {'u', 'U', '7'},
     {'i', 'I', '8'},
     {'o', 'O', '9'},
     {'p', 'P', '0'}},

    {{'a', 'A', '*'},
     {'s', 'S', '/'},
     {'d', 'D', '+'},
     {'f', 'F', '-'},
     {'g', 'G', '='},
     {'h', 'H', ':'},
     {'j', 'J', '\''},
     {'k', 'K', '"'},
     {'l', 'L', '@'},
     {KEY_ENTER, KEY_ENTER, KEY_ENTER}},

    {{KEY_FN, KEY_FN, KEY_FN},
     {'z', 'Z', '_'},
     {'x', 'X', '$'},
     {'c', 'C', ';'},
     {'v', 'V', '?'},
     {'b', 'B', '!'},
     {'n', 'N', ','},
     {'m', 'M', '.'},
     {SHIFT, SHIFT, CAPS_LOCK},
     {KEY_BACKSPACE, KEY_BACKSPACE, KEY_BACKSPACE}},

    {{' ', ' ', ' '}}
};

char getKeyChar(uint8_t k) {
    char keyVal;
    if (fn_key_pressed) {
        keyVal = _key_value_map[k / 10][k % 10].value_third;
    } else if (shift_key_pressed ^ caps_lock) {
        keyVal = _key_value_map[k / 10][k % 10].value_second;
    } else {
        keyVal = _key_value_map[k / 10][k % 10].value_first;
    }
    return keyVal;
}

int handleSpecialKeys(uint8_t k, bool pressed) {
    switch (k) {
        case KEY_FN: fn_key_pressed = !fn_key_pressed; return 1;
        case KEY_LEFT_SHIFT: {
            shift_key_pressed = pressed;
            if (fn_key_pressed && shift_key_pressed) { caps_lock = !caps_lock; }
            return 1;
        }
        default: break;
    }
    return 0;
}

/***************************************************************************************
** Function name: _setup_gpio()
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {

    Wire.begin(SDA, SCL);

    pinMode(SEL_BTN, INPUT);
    pinMode(BK_BTN, INPUT);

    // before powering on, all CS signals should be pulled high and in an unselected state;
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);

    bool pmu_ret = false;
    pmu_ret = PPM.init(Wire, SDA, SCL, BQ25896_I2C_ADDRESS);
    if (pmu_ret) {
        PPM.setSysPowerDownVoltage(3300);
        PPM.setInputCurrentLimit(3250);
        Serial.printf("getInputCurrentLimit: %d mA\n", PPM.getInputCurrentLimit());
        PPM.disableCurrentLimitPin();
        PPM.setChargeTargetVoltage(4208);
        PPM.setPrechargeCurr(64);
        PPM.setChargerConstantCurr(832);
        PPM.getChargerConstantCurr();
        Serial.printf("getChargerConstantCurr: %d mA\n", PPM.getChargerConstantCurr());
        PPM.enableMeasure();
        PPM.enableCharge();
        PPM.enableOTG();
        PPM.disableOTG();
    }

    if (io.begin(Wire, 0x20)) {
        const uint8_t expands[] = {
            EXPANDS_KB_RST,
            EXPANDS_KB_EN,
            EXPANDS_SD_EN,
        };
        for (auto pin : expands) {
            io.pinMode(pin, OUTPUT);
            io.digitalWrite(pin, HIGH);
            delay(1);
        }
        io.pinMode(EXPANDS_SD_PULLEN, INPUT);
    } else {
        Serial.println("Initializing expander failed");
    }

    pinMode(ENCODER_KEY, INPUT);
    encoder = new RotaryEncoder(ENCODER_INA, ENCODER_INB, RotaryEncoder::LatchMode::FOUR3);

    // register interrupt routine
    attachInterrupt(digitalPinToInterrupt(ENCODER_INA), checkPosition, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_INB), checkPosition, CHANGE);

    // Initalise keyboard
    bool res = keyboard.begin(KB_I2C_ADDRESS, &Wire);
    if (!res) {
        Serial.println("Failed to find Keyboard");

    } else {
        Serial.println("Initializing Keyboard succeeded");
    }
    keyboard.matrix(KB_ROWS, KB_COLS);
    keyboard.flush();
}

/***************************************************************************************
** Function name: getBattery()
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    int percent = bq.getChargePcnt();
    if (percent == 65535) return -1;
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

/*********************************************************************
**  Function: setBrightness
**  set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    if (brightval == 0) {
        analogWrite(TFT_BL, brightval);
        analogWrite(KEYBOARD_BL, brightval);
    } else {
        int bl = MINBRIGHT + round(((255 - MINBRIGHT) * brightval / 100));
        analogWrite(TFT_BL, bl);
        analogWrite(KEYBOARD_BL, bl);
    }
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {

    static unsigned long tm = millis();
    static int _last_dir = 0;
    bool sel = !BTN_ACT;
    bool esc = !BTN_ACT;

    uint8_t keyValue = 0;
    uint8_t keyVal = '\0';

    if (millis() - tm < 500) return;

    _last_dir = (int)encoder->getDirection();
    sel = digitalRead(SEL_BTN);
    esc = digitalRead(BK_BTN);

    if (keyboard.available() > 0) {
        int keyValue = keyboard.getEvent();
        bool pressed = keyValue & 0x80;
        keyValue &= 0x7F;
        keyValue--;
        if (keyValue / 10 < 4) {
            if (handleSpecialKeys(keyValue, pressed) > 0) goto END;
            keyVal = getKeyChar(keyValue);
        }
        if (pressed && !wakeUpScreen() && keyVal != '\0') {
            KeyStroke.Clear();
            KeyStroke.hid_keys.push_back(keyVal);
            if (keyValue == KEY_BACKSPACE) KeyStroke.del = true;
            if (keyValue == KEY_ENTER) KeyStroke.enter = true;
            if (digitalRead(SEL_BTN) == BTN_ACT) KeyStroke.fn = true;
            KeyStroke.word.push_back(keyVal);
            KeyStroke.pressed = true;
        }
    } else KeyStroke.Clear();

    if (_last_dir != 0 || sel == BTN_ACT || esc == BTN_ACT || KeyStroke.enter) {
        if (!wakeUpScreen()) {
            AnyKeyPress = true;

            if (_last_dir < 0) PrevPress = true;
            if (_last_dir > 0) NextPress = true;
            if (sel == BTN_ACT) SelPress = true;
            if (esc == BTN_ACT) EscPress = true;
        } else goto END;
    }

END:
    if (sel == BTN_ACT || esc == BTN_ACT) tm = millis();
}

void powerOff() { PPM.shutdown(); }

void checkReboot() {}

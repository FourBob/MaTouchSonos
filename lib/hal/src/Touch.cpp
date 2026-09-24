#include "Touch.h"

#include <Arduino.h>
#include <Wire.h>

#include "board_config.h"

namespace hal {
namespace {

// Register ab 0x02 (wie im Makerfabs-Beispiel):
//   [0] Anzahl Finger
//   [1] Ereignis (Bit 7..6) | X Bit 11..8
//   [2] X Bit 7..0
//   [3] ID (Bit 7..4)       | Y Bit 11..8
//   [4] Y Bit 7..0
constexpr uint8_t kRegTouchData = 0x02;
constexpr uint8_t kReadLen = 5;
constexpr uint8_t kEventContact = 2;

bool readRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(true) != 0) return false;
    if (Wire.requestFrom(static_cast<uint8_t>(TOUCH_I2C_ADDR), len) != len) return false;
    for (uint8_t i = 0; i < len; ++i) buf[i] = static_cast<uint8_t>(Wire.read());
    return true;
}

}  // namespace

bool Touch::begin() {
    Wire.begin(TOUCH_I2C_SDA, TOUCH_I2C_SCL);  // 100 kHz wie im Makerfabs-Beispiel
    Wire.beginTransmission(TOUCH_I2C_ADDR);
    return Wire.endTransmission(true) == 0;
}

bool Touch::read(int16_t& x, int16_t& y) {
    uint8_t d[kReadLen];
    if (!readRegs(kRegTouchData, d, kReadLen)) return false;
    if ((d[1] >> 6) != kEventContact) return false;

    x = static_cast<int16_t>(((d[1] & 0x0F) << 8) | d[2]);
    y = static_cast<int16_t>(((d[3] & 0x0F) << 8) | d[4]);
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return false;  // Störwert verwerfen
    return true;
}

}  // namespace hal

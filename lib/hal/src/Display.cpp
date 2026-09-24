#include "Display.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "Touch.h"
#include "board_config.h"

namespace hal {
namespace {

// 3-Wire-SPI nur für die Initialisierungssequenz des ST7701.
Arduino_DataBus* spiBus = new Arduino_SWSPI(GFX_NOT_DEFINED /* DC */, LCD_SPI_CS, LCD_SPI_SCK, LCD_SPI_SDA,
                                            GFX_NOT_DEFINED /* MISO */);

Arduino_ESP32RGBPanel* rgbPanel = new Arduino_ESP32RGBPanel(
    LCD_DE, LCD_VSYNC, LCD_HSYNC, LCD_PCLK,
    LCD_R0, LCD_R1, LCD_R2, LCD_R3, LCD_R4,
    LCD_G0, LCD_G1, LCD_G2, LCD_G3, LCD_G4, LCD_G5,
    LCD_B0, LCD_B1, LCD_B2, LCD_B3, LCD_B4,
    LCD_HSYNC_POLARITY, LCD_HSYNC_FRONT_PORCH, LCD_HSYNC_PULSE_WIDTH, LCD_HSYNC_BACK_PORCH,
    LCD_VSYNC_POLARITY, LCD_VSYNC_FRONT_PORCH, LCD_VSYNC_PULSE_WIDTH, LCD_VSYNC_BACK_PORCH);

// Init-Sequenz "type5" ist die, die Makerfabs für dieses 2.1"-Panel verwendet.
Arduino_RGB_Display* gfx = new Arduino_RGB_Display(
    LCD_WIDTH, LCD_HEIGHT, rgbPanel, 0 /* rotation */, true /* auto_flush */,
    spiBus, GFX_NOT_DEFINED /* RST */, st7701_type5_init_operations, sizeof(st7701_type5_init_operations));

// LVGL zeichnet in zwei Teilpuffer (je 1/10 Bildschirm) und kopiert sie danach
// in den Framebuffer des Panels. Zwei Puffer: LVGL kann weiterzeichnen,
// während der vorige kopiert wird.
constexpr uint32_t kBufferPixels = LCD_WIDTH * LCD_HEIGHT / 10;

lv_disp_draw_buf_t drawBuf;
lv_disp_drv_t dispDrv;
lv_indev_drv_t touchDrv;

void flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* colors) {
    const int16_t w = static_cast<int16_t>(area->x2 - area->x1 + 1);
    const int16_t h = static_cast<int16_t>(area->y2 - area->y1 + 1);
    gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t*>(&colors->full), w, h);
    lv_disp_flush_ready(drv);
}

void touchReadCb(lv_indev_drv_t*, lv_indev_data_t* data) {
    int16_t x, y;
    if (Touch::read(x, y)) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

lv_color_t* allocBuffer() {
    // Bevorzugt schneller interner RAM, sonst PSRAM.
    void* p = heap_caps_malloc(kBufferPixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(kBufferPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    return static_cast<lv_color_t*>(p);
}

}  // namespace

bool Display::begin() {
    if (!gfx->begin()) {
        log_e("Display: gfx->begin() fehlgeschlagen");
        return false;
    }
    gfx->fillScreen(BLACK);

    pinMode(LCD_BACKLIGHT, OUTPUT);
    setBacklight(255);

    lv_init();

    lv_color_t* buf1 = allocBuffer();
    lv_color_t* buf2 = allocBuffer();
    if (!buf1 || !buf2) {
        log_e("Display: kein Speicher für LVGL-Puffer");
        return false;
    }
    lv_disp_draw_buf_init(&drawBuf, buf1, buf2, kBufferPixels);

    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = LCD_WIDTH;
    dispDrv.ver_res = LCD_HEIGHT;
    dispDrv.flush_cb = flushCb;
    dispDrv.draw_buf = &drawBuf;
    lv_disp_drv_register(&dispDrv);

    if (!Touch::begin()) {
        log_w("Touch: CST826 antwortet nicht (Adresse 0x%02X)", TOUCH_I2C_ADDR);
    }
    lv_indev_drv_init(&touchDrv);
    touchDrv.type = LV_INDEV_TYPE_POINTER;
    touchDrv.read_cb = touchReadCb;
    lv_indev_drv_register(&touchDrv);

    log_i("Display: %dx%d bereit", LCD_WIDTH, LCD_HEIGHT);
    return true;
}

void Display::setBacklight(uint8_t level) {
    // Schritt 0: nur an/aus. PWM-Dimmen folgt in Schritt 9.
    digitalWrite(LCD_BACKLIGHT, level > 0 ? HIGH : LOW);
}

}  // namespace hal

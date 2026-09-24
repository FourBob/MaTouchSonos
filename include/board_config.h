#pragma once

// ============================================================================
// Pinbelegung und Hardware-Parameter des
// Makerfabs MaTouch ESP32-S3 Rotary IPS Display with Touch 2.1" (ST7701), v1.1
//
// Quelle: Makerfabs-Beispiele (fw_test, fw_test_v2) und Schaltplan v1.1
// https://github.com/Makerfabs/MaTouch-ESP32-S3-Rotary-IPS-Display-with-Touch-2.1-ST7701
// Details: docs/HARDWARE.md
// ============================================================================

// --- Display: ST7701S, 480x480, RGB565 parallel ------------------------------
#define LCD_WIDTH   480
#define LCD_HEIGHT  480

// 3-Wire-SPI, nur für die Initialisierung des ST7701
#define LCD_SPI_CS    1
#define LCD_SPI_SCK   46
#define LCD_SPI_SDA   0

// RGB-Steuerleitungen
#define LCD_DE     2
#define LCD_VSYNC  42
#define LCD_HSYNC  3
#define LCD_PCLK   45

// RGB-Datenleitungen (Reihenfolge wie im Makerfabs-Beispiel fw_test_v2)
#define LCD_R0 4
#define LCD_R1 41
#define LCD_R2 5
#define LCD_R3 40
#define LCD_R4 6
#define LCD_G0 39
#define LCD_G1 7
#define LCD_G2 47
#define LCD_G3 8
#define LCD_G4 48
#define LCD_G5 9
#define LCD_B0 11
#define LCD_B1 15
#define LCD_B2 12
#define LCD_B3 16
#define LCD_B4 21

// Timing
#define LCD_HSYNC_POLARITY    1
#define LCD_HSYNC_FRONT_PORCH 10
#define LCD_HSYNC_PULSE_WIDTH 8
#define LCD_HSYNC_BACK_PORCH  50
#define LCD_VSYNC_POLARITY    1
#define LCD_VSYNC_FRONT_PORCH 10
#define LCD_VSYNC_PULSE_WIDTH 8
#define LCD_VSYNC_BACK_PORCH  20

// Hintergrundbeleuchtung
#define LCD_BACKLIGHT 38

// --- Touch: CST826 an I2C ---------------------------------------------------
#define TOUCH_I2C_SDA  17
#define TOUCH_I2C_SCL  18
#define TOUCH_I2C_ADDR 0x15

// --- Drehencoder + Taste ----------------------------------------------------
#define ENCODER_PIN_A 13  // CLK
#define ENCODER_PIN_B 10  // DT
#define BUTTON_PIN    14  // aktiv LOW

// Zustandswechsel pro spürbarer Rastung. Falls eine Rastung zwei Schritte zählt,
// auf 2 ändern; falls man zwei Rastungen für einen Schritt braucht, auf 8.
#define ENCODER_STEPS_PER_DETENT 4
// Auf 1 setzen, wenn Drehen im Uhrzeigersinn die Werte verkleinert.
#define ENCODER_INVERT 0

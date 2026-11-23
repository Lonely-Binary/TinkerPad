#pragma once

// TinkerPad hardware (prototype carrier: "蓝牙矩阵键盘 第四版").
//
// U20 on that schematic is a 44-pin socket for a stock ESP32-S3-DevKitC-1,
// not a soldered-down module. That is why the carrier's Type-C is 6-pin
// (VBUS/CC/GND only) and IO19/IO20 are left unconnected: flashing goes
// through the dev board's own USB, not the carrier.

// ---------------------------------------------------------------- I2C bus
// Shared by the keypad expander and the power-management IC.
#define PIN_I2C_SDA        9
#define PIN_I2C_SCL        8

// PCF8574T, A0/A1/A2 tied to GND.  P0-P3 = rows R1-R4, P4-P7 = columns C1-C4.
// INT# (pin 13) is NOT routed on this board, so the matrix can only be polled.
#define ADDR_PCF8574       0x20

// IP5306-I2C power management. Sleeps when idle and does not ACK while asleep —
// a missing 0x75 in a bus scan is not proof the chip is dead.
#define ADDR_IP5306        0x75

// -------------------------------------------------------------------- LEDs
// 16 x WS2812B-2020, one per key. Powered from +5V with 3.3V data and no
// level shifter: VIH is nominally 0.7 x VDD = 3.5V, so this is out of spec on
// paper even though it works in practice. Worth re-checking at full brightness.
#define PIN_LED_DATA       38
#define NUM_LEDS           16

// ------------------------------------------------------------------- Audio
// NS4168 class-D amp, standard I2S TX. CTRL is tied to A3V3, so the amp is
// always enabled — there is no hardware mute.
#define PIN_SPK_BCLK       17
#define PIN_SPK_LRCLK      16
#define PIN_SPK_DIN        18

// LMD2718 PDM microphone. L/R is tied to GND, so it drives the LEFT slot only.
#define PIN_MIC_CLK        40
#define PIN_MIC_DATA       39

// ----------------------------------------------------------------- Battery
// BAT through a 100k/100k divider, so the pin sees Vbat/2.
#define PIN_BATT_ADC       3
#define BATT_DIVIDER       2.0f

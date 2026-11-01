#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16

#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_40
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_39

#define DISPLAY_SDA             GPIO_NUM_10
#define DISPLAY_SCL             GPIO_NUM_9
#define DISPLAY_DC              GPIO_NUM_8
#define DISPLAY_CS              GPIO_NUM_14
#define DISPLAY_RES             GPIO_NUM_18
#define DISPLAY_RST             DISPLAY_RES

#define DISPLAY_WIDTH           320
#define DISPLAY_HEIGHT          240
#define DISPLAY_SWAP_XY         true
#define DISPLAY_MIRROR_X        false
#define DISPLAY_MIRROR_Y        true
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER       LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X        0
#define DISPLAY_OFFSET_Y        0
#define DISPLAY_SPI_MODE        0

#define DISPLAY_BACKLIGHT_PIN           GPIO_NUM_13
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

#define MOTOR_LEFT_IN1          GPIO_NUM_11
#define MOTOR_LEFT_IN2          GPIO_NUM_12
#define MOTOR_RIGHT_IN3         GPIO_NUM_21
#define MOTOR_RIGHT_IN4         GPIO_NUM_47

#define HC04_TRIG_GPIO          GPIO_NUM_1
#define HC04_ECHO_GPIO          GPIO_NUM_2

#define LED1_GPIO               GPIO_NUM_17
#define LED2_GPIO               GPIO_NUM_48

#define OBSTACLE_DISTANCE_CM    25
#define SPIN_360_MS             2200   // thoi gian quay 1 vong 360 do (ms)

// The nho microSD (SPI2_HOST, FAT32) -> /sdcard, phat nhac MP3 bang giong noi
// Luu y: GPIO19/20 la chan USB D-/D+ -> mat cong USB tu nhien (nap/log qua USB-UART 43/44)
#define SD_PIN_MISO             GPIO_NUM_42
#define SD_PIN_CLK              GPIO_NUM_41
#define SD_PIN_MOSI             GPIO_NUM_20
#define SD_PIN_CS               GPIO_NUM_19

// Man OLED phu SSD1306 0.96" 128x64, I2C dia chi 0x3C
// Luu y: GPIO46 la chan strapping. Dien tro keo len cua OLED lam GPIO46 = 1 -> khi nap
// firmware o che do download (giu BOOT) co the bao loi -> rut OLED (hoac VCC OLED) khi nap.
#define OLED2_PIN_SDA           GPIO_NUM_3
#define OLED2_PIN_SCL           GPIO_NUM_46
#define OLED2_HEIGHT            64
#define OLED2_FLIP              false   // true = lat 180 do neu hinh bi nguoc

#endif // _BOARD_CONFIG_H_
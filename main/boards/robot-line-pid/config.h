#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

// Robot Do Line PID: man ST7789 2.8", mic INMP441 + loa MAX98357 (giong RB5320),
// driver motor TB6612FNG (6 chan), cam bien do line 6 kenh.

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_17
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16

// Nut co dinh
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_40   // giu lau: bat / tat do line
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_39   // giu lau: hieu chinh cam bien line

// Man ST7789 2.8" (giong RB5320)
#define DISPLAY_SDA             GPIO_NUM_10
#define DISPLAY_SCL             GPIO_NUM_9
#define DISPLAY_DC              GPIO_NUM_8
#define DISPLAY_CS              GPIO_NUM_14
#define DISPLAY_RES             GPIO_NUM_18
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

// Driver motor TB6612FNG. STBY noi san len VCC, khong co chan dieu khien.
// speed_run: Motor A = banh trai, Motor B = banh phai.
// Doi driver khac: MotorDriverType::L298n, L293d, Drv8833, L9110s, Mx1508, Bts7960, Generic4
#define MOTOR_DRIVER            MotorDriverType::Tb6612
#define MOTOR_LEFT_IN1          GPIO_NUM_21   // AIN1, banh trai
#define MOTOR_LEFT_IN2          GPIO_NUM_47   // AIN2
#define MOTOR_LEFT_PWM          GPIO_NUM_12   // PWMA
#define MOTOR_RIGHT_IN1         GPIO_NUM_42   // BIN1, banh phai
#define MOTOR_RIGHT_IN2         GPIO_NUM_41   // BIN2
#define MOTOR_RIGHT_PWM         GPIO_NUM_11   // PWMB
#define MOTOR_STBY              GPIO_NUM_NC
// Banh nao quay nguoc thi doi true/false (khong can dao day)
#define MOTOR_INVERT_LEFT       false
#define MOTOR_INVERT_RIGHT      false
#define MOTOR_SWAP_LR           false         // true = lenh trai ra banh phai va nguoc lai
#define MOTOR_SPEED_PERCENT     20            // toc do cho lenh tien/lui/re bang giong noi

// Cam bien analog 6 kenh, S0 = TRAI nhat ... S5 = PHAI nhat
// ADC1 (on dinh khi Wi-Fi): S0=1, S2=2, S5=3
// ADC2: S1=19, S3=15, S4=20. Wi-Fi chiem ADC2 thi giu gia tri doc truoc.
#define LINE_S0                 GPIO_NUM_1
#define LINE_S1                 GPIO_NUM_19
#define LINE_S2                 GPIO_NUM_2
#define LINE_S3                 GPIO_NUM_15
#define LINE_S4                 GPIO_NUM_20
#define LINE_S5                 GPIO_NUM_3

// PID mac dinh (chinh bang giong noi: "chinh PID kp 0.05 kd 0.2")
#define LINE_KP                 0.04f
#define LINE_KI                 0.0f
#define LINE_KD                 0.1f
#define LINE_BASE_SPEED         50            // %
#define LINE_MAX_SPEED          100           // %
#define LINE_WHITE_ON_BLACK     false         // true = vach TRANG tren nen DEN

// Khong gan LED
#define LED1_GPIO               GPIO_NUM_NC

#endif // _BOARD_CONFIG_H_

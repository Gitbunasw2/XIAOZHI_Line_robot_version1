#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

// Board da nang: chan GPIO KHONG co dinh o day ma doc tu NVS (rb5320/device_json),
// duoc luu tu trang cau hinh Wi-Fi http://192.168.4.1 -> tab "Cau hinh Thiet bi".
// Cac gia tri duoi day chi la MAC DINH khi chua cau hinh (hoac o che do an toan).

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
// ES8311 (giong board Freenove 2.8): vao/ra cung 24 kHz
#define ES8311_SAMPLE_RATE       24000

// NVS: namespace + key do trang cau hinh ghi (/rb/device)
#define MULTI_NVS_NAMESPACE      "rb5320"
#define MULTI_NVS_KEY_CONFIG     "device_json"
#define MULTI_NVS_KEY_STATUS     "multi_status"
#define MULTI_NVS_KEY_FW         "multi_fw"      // ma firmware lan truoc (de dat lai giao dien sang)

// ---- Mac dinh: I2S Simplex (INMP441 + MAX98357) giong bread-compact-wifi ----
#define DEFAULT_MIC_WS    GPIO_NUM_4
#define DEFAULT_MIC_SCK   GPIO_NUM_5
#define DEFAULT_MIC_DIN   GPIO_NUM_6
#define DEFAULT_SPK_DOUT  GPIO_NUM_7
#define DEFAULT_SPK_BCLK  GPIO_NUM_15
#define DEFAULT_SPK_LRCK  GPIO_NUM_16

#define DEFAULT_BOOT_BUTTON GPIO_NUM_0

// ---- Man hinh ----
#define DEFAULT_LCD_SPI_MHZ      40
#define DEFAULT_ILI9488_SPI_MHZ  20
#define OLED_I2C_ADDRESS         0x3C

// ---- Do pin: cau chia ap tren chan ADC (ohm) ----
#define BATTERY_DIVIDER_UPPER    100000.0f
#define BATTERY_DIVIDER_LOWER    100000.0f

// ---- Che do an toan ----
// Neu board bi crash (panic/watchdog) lien tiep so lan nay -> bo qua man hinh/ngoai vi,
// dung chan am thanh mac dinh va tu mo trang cau hinh 192.168.4.1
#define SAFE_MODE_CRASH_LIMIT    3
// Chay on dinh qua thoi gian nay (giay) thi xoa bo dem crash
#define SAFE_MODE_STABLE_SEC     20

#endif // _BOARD_CONFIG_H_

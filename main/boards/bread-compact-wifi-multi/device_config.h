// Doc va kiem tra cau hinh chan tu JSON do trang 192.168.4.1 luu (rb5320/device_json).
// Dinh dang chinh: config.json (screen_type, driver, pin_mosi, i2s_mic_ws, ...).
// Van doc duoc dinh dang cu cua form (dev_scrtype, g_mosi, ...).
// File nay khong phu thuoc phan cung de co the test tren may tinh.
//
// BANG SO trong config.json:
//   screen_type          0 = TFT LCD SPI, 1 = OLED I2C, 2 = khong man hinh
//   oled_driver          0 = SSD1306, 1 = SH1106
//   driver (TFT)         0 ST7789, 1 ST7735, 2 ILI9341, 3 GC9A01, 4 ILI9488,
//                        5 ST7796, 6 ILI9486, 7 ILI9163C, 8 GC9107, 9 NV3023
//   rgb_order            0 = RGB, 1 = BGR
//   audio_codec_type     0 = I2S Simplex (INMP441 + MAX98357), 1 = I2S Duplex, 2 = ES8311
//   charging_active_level 0 = LOW khi sac (TP4056/BQ24), 1 = HIGH khi sac
//   language             0 = vi-VN, 1 = en-US (chi luu, ngon ngu chon luc build)
//   Them (khong bat buoc): spi_clock_mhz (0 = tu dong), codec_pa_inverted (0/1),
//                          wallpaper (1 = co hinh nen sang/toi, mac dinh 1)
//   Robot RB5320: robot_enable (0/1), pin_motor_l1, pin_motor_l2, pin_motor_r1, pin_motor_r2,
//                 pin_hc04_trig, pin_hc04_echo, pin_led1, pin_led2,
//                 obstacle_cm (mac dinh 25), spin_360_ms (mac dinh 2200), robot_idle_move (0/1)
//                 motor_driver: 0 4 chan (RB5320), 1 TB6612, 2 L298N, 3 L293D, 4 DRV8833, 5 L9110S,
//                               6 MX1508, 7 BTS7960
//                 pin_motor_pwm_l / pin_motor_pwm_r (EN/PWM: TB6612 PWMB/PWMA, L298N ENA/ENB, L293D EN1/EN2),
//                 pin_motor_stby (TB6612), motor_speed (20-100 %), motor_invert_l / motor_invert_r (0/1)
//                 func_avoid (0/1): tranh vat can bang HC-SR04 (pin_hc04_trig/echo, obstacle_cm)
//                 func_line (0/1): do line PID: pin_line_0..pin_line_5 (0 = trai nhat), line_kp, line_ki,
//                               line_kd (so thap phan), line_speed (%), line_white (1 = vach trang nen den)
//                 func_bt (0/1): dieu khien bang app Dabble qua Bluetooth AT-09 / HM-10:
//                               pin_at09_tx (ESP TX -> RXD), pin_at09_rx (ESP RX <- TXD), at09_baud
//   sd_enable / oled2_enable / bt_a2dp_enable (0/1): tat thi bo qua chan
//   The nho SD (SPI): pin_sd_miso, pin_sd_clk, pin_sd_mosi, pin_sd_cs  (-1 = khong co the)
//   OLED phu (I2C):   pin_oled2_sda, pin_oled2_scl, oled2_driver (0 SSD1306, 1 SH1106),
//                     oled2_height (64/32), oled2_flip (0/1 lat 180 do)
#ifndef _MULTI_DEVICE_CONFIG_H_
#define _MULTI_DEVICE_CONFIG_H_

#include <cJSON.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace multi {

enum class DisplayType { None, Lcd, OledSsd1306, OledSh1106 };
enum class LcdDriver { St7789, St7735, Ili9341, Gc9a01, Ili9488, St7796, Ili9486, Ili9163, Gc9107, Nv3023 };

inline const char* LcdDriverName(LcdDriver d) {
    static const char* names[] = {"ST7789", "ST7735", "ILI9341", "GC9A01", "ILI9488",
                                  "ST7796", "ILI9486", "ILI9163", "GC9107", "NV3023"};
    return names[(int)d];
}

constexpr int NC = -1;
constexpr int FIXED_BOOT_BUTTON = 0;
constexpr int FIXED_VOL_UP_BUTTON = 40;
constexpr int FIXED_VOL_DOWN_BUTTON = 39;

struct DeviceConfig {
    // Man hinh
    DisplayType display = DisplayType::None;
    LcdDriver driver = LcdDriver::St7789;
    int width = 240, height = 240, offset_x = 0, offset_y = 0;
    int spi_mode = 0, spi_mhz = 0;  // 0 = tu chon theo driver
    bool bgr = false, mirror_x = false, mirror_y = false, swap_xy = false;
    bool invert_color = false, backlight_invert = false;
    bool wallpaper = true;  // hinh nen sang/toi (chi man LCD)
    int mosi = NC, sclk = NC, dc = NC, rst = NC, cs = NC, bl = NC;
    int oled_sda = NC, oled_scl = NC;

    // Am thanh
    bool duplex = false;
    bool es8311 = false;           // chip codec ES8311 qua I2C
    int mclk = NC, codec_sda = NC, codec_scl = NC, pa = NC;
    bool pa_inverted = false;
    int mic_ws = 4, mic_sck = 5, mic_din = 6;
    int spk_dout = 7, spk_bclk = 15, spk_lrck = 16;

    // Ngoai vi
    int led = NC, led_count = 1;
    // Nut co dinh tren mach, KHONG cau hinh tu web: BOOT=0, Vol+=40, Vol-=39
    int boot_button = FIXED_BOOT_BUTTON, vol_up = FIXED_VOL_UP_BUTTON, vol_down = FIXED_VOL_DOWN_BUTTON;
    int battery_adc = NC, charge_pin = NC;
    bool charge_active_high = false;
    int ext_lamp = NC;

    // Robot RB5320
    bool robot = false;
    int motor_l1 = NC, motor_l2 = NC, motor_r1 = NC, motor_r2 = NC;
    int motor_pwm_l = NC, motor_pwm_r = NC;  // EN/PWM trai / phai
    int motor_speed = 20;
    int motor_driver = 0;                     // MotorDriverType
    int motor_stby = NC;                      // TB6612 STBY
    bool motor_invert_l = true, motor_invert_r = false;  // mac dinh = chieu quay cua RB5320
    bool motor_swap_lr = false;               // doi banh trai <-> phai, ap dung ca do line
    bool func_avoid = false;                  // tranh vat can (HC-SR04)
    bool func_line = false;                   // do line PID
    int line_pins[6] = {NC, NC, NC, NC, NC, NC};
    float line_kp = 0.04f, line_ki = 0.0f, line_kd = 0.1f;
    int line_speed = 50;
    int line_max_speed = 140;              // duty 0..255, toc do thang khi do line
    int line_recovery = 80;                // duty 0..255, quay tai cho khi mat vach
    bool line_mix_reverse = true;          // true: banh lech duoc lui. false: chi chay toi 0..255
    bool line_white = false;
    bool func_bt = false;                     // app Dabble qua AT-09
    int at09_tx = NC, at09_rx = NC, at09_baud = 9600;
    int LineCount() const {
        int n = 0;
        for (int p : line_pins) n += p != NC;
        return n;
    }
    int hc04_trig = NC, hc04_echo = NC;
    int robot_led1 = NC, robot_led2 = NC;
    int obstacle_cm = 25, spin_360_ms = 2200;
    bool robot_idle_move = true;

    // The nho microSD (SPI) -> /sdcard, phat nhac MP3
    int sd_miso = NC, sd_clk = NC, sd_mosi = NC, sd_cs = NC;

    // Man OLED phu (I2C, dia chi 0x3C): mat robot, gio, Wi-Fi, pin, khoang cach, chu AI
    int oled2_sda = NC, oled2_scl = NC;
    bool oled2_sh1106 = false, oled2_flip = false;
    int oled2_height = 64;

    bool HasSd() const { return sd_miso != NC && sd_clk != NC && sd_mosi != NC && sd_cs != NC; }
    bool HasOled2() const { return oled2_sda != NC && oled2_scl != NC; }
};

struct ParseResult {
    DeviceConfig cfg;
    bool found = false;               // co du lieu trong NVS
    std::vector<std::string> warnings;
};

// ---------- tien ich ----------
inline std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

inline bool GetString(cJSON* root, const char* key, std::string& out) {
    cJSON* it = cJSON_GetObjectItem(root, key);
    if (cJSON_IsString(it) && it->valuestring) { out = it->valuestring; return true; }
    return false;
}

// Gia tri tu form la chuoi ("12", "", "-1") hoac so. Rong / khong co -> giu mac dinh `def`.
inline int GetInt(cJSON* root, const char* key, int def, bool* present = nullptr) {
    if (present) *present = false;
    cJSON* it = cJSON_GetObjectItem(root, key);
    if (cJSON_IsNumber(it)) { if (present) *present = true; return (int)it->valuedouble; }
    if (cJSON_IsString(it) && it->valuestring) {
        const char* s = it->valuestring;
        while (*s == ' ') s++;
        if (*s == '\0') return def;
        char* end = nullptr;
        long v = std::strtol(s, &end, 10);
        if (end == s) return def;
        if (present) *present = true;
        return (int)v;
    }
    return def;
}

inline float GetFloat(cJSON* root, const char* key, float def) {
    cJSON* it = cJSON_GetObjectItem(root, key);
    if (cJSON_IsNumber(it)) return (float)it->valuedouble;
    if (cJSON_IsString(it) && it->valuestring && it->valuestring[0]) {
        std::string t = it->valuestring;
        for (auto& ch : t) if (ch == ',') ch = '.';
        char* end = nullptr;
        float v = std::strtof(t.c_str(), &end);
        if (end != t.c_str()) return v;
    }
    return def;
}

inline bool GetBool(cJSON* root, const char* key, bool def) {
    cJSON* it = cJSON_GetObjectItem(root, key);
    if (cJSON_IsBool(it)) return cJSON_IsTrue(it);
    if (cJSON_IsNumber(it)) return it->valuedouble != 0;
    if (cJSON_IsString(it) && it->valuestring) {
        std::string s = Lower(it->valuestring);
        return s == "1" || s == "true" || s == "on" || s == "yes";
    }
    return def;
}

// Chan GPIO: trong form, de trong hoac -1 = khong gan
inline int GetPin(cJSON* root, const char* key, int def) {
    bool present = false;
    int v = GetInt(root, key, def, &present);
    if (!present) {
        // key co nhung rong -> khong gan; key khong co -> mac dinh
        return cJSON_GetObjectItem(root, key) ? NC : def;
    }
    return v < 0 ? NC : v;
}

// ESP32-S3: GPIO 0-21 va 33-48 ton tai; 26-32 la flash/PSRAM.
// 33-37 dung cho PSRAM Octal (module N8R8/N16R8) -> truyen octal_psram = true.
inline bool IsUsablePin(int pin, bool octal_psram) {
    if (pin < 0) return false;
    if (pin <= 21) return true;
    if (pin >= 33 && pin <= 48) return !(octal_psram && pin <= 37);
    return false;
}

inline std::string PinName(int pin) { return "GPIO" + std::to_string(pin); }

// OLED va ES8311 cung chan SDA/SCL -> dung chung 1 bus I2C
inline bool OledSharesCodecBus(const DeviceConfig& c) {
    return c.es8311 && (c.display == DisplayType::OledSsd1306 || c.display == DisplayType::OledSh1106) &&
           c.oled_sda == c.codec_sda && c.oled_scl == c.codec_scl;
}

inline bool IsMainOled(const DeviceConfig& c) {
    return c.display == DisplayType::OledSsd1306 || c.display == DisplayType::OledSh1106;
}

// OLED phu cung SDA/SCL voi ES8311 -> dung chung bus I2C cua codec (khac dia chi nen khong sao)
inline bool Oled2SharesCodecBus(const DeviceConfig& c) {
    return c.es8311 && c.HasOled2() && c.oled2_sda == c.codec_sda && c.oled2_scl == c.codec_scl;
}

// ---------- phan tich ----------
inline void ReadConfigJson(cJSON* root, DeviceConfig& c, ParseResult& r) {
    int st = GetInt(root, "screen_type", 2);
    int oled = GetInt(root, "oled_driver", 0);
    if (st == 0) {
        c.display = DisplayType::Lcd;
    } else if (st == 1) {
        c.display = oled == 1 ? DisplayType::OledSh1106 : DisplayType::OledSsd1306;
    } else {
        c.display = DisplayType::None;
        if (st != 2) r.warnings.push_back("screen_type = " + std::to_string(st) + " không hợp lệ, tắt màn hình");
    }

    static const LcdDriver kDrivers[] = {LcdDriver::St7789, LcdDriver::St7735, LcdDriver::Ili9341,
                                         LcdDriver::Gc9a01, LcdDriver::Ili9488, LcdDriver::St7796,
                                         LcdDriver::Ili9486, LcdDriver::Ili9163, LcdDriver::Gc9107,
                                         LcdDriver::Nv3023};
    int drv = GetInt(root, "driver", 0);
    if (drv < 0 || drv >= (int)(sizeof(kDrivers) / sizeof(kDrivers[0]))) {
        r.warnings.push_back("driver = " + std::to_string(drv) + " không hợp lệ, dùng ST7789");
        drv = 0;
    }
    c.driver = kDrivers[drv];

    c.width    = GetInt(root, "width", c.width);
    c.height   = GetInt(root, "height", c.height);
    c.offset_x = GetInt(root, "offset_x", 0);
    c.offset_y = GetInt(root, "offset_y", 0);
    c.spi_mode = GetInt(root, "spi_mode", 0);
    c.spi_mhz  = GetInt(root, "spi_clock_mhz", 0);
    c.bgr      = GetInt(root, "rgb_order", 0) == 1;
    c.mirror_x = GetBool(root, "mirror_x", false);
    c.mirror_y = GetBool(root, "mirror_y", false);
    c.swap_xy  = GetBool(root, "swap_xy", false);
    c.invert_color = GetBool(root, "invert_color", false);
    c.backlight_invert = GetBool(root, "backlight_invert", false);
    c.wallpaper = GetBool(root, "wallpaper", true);

    c.mosi = GetPin(root, "pin_mosi", NC);
    c.sclk = GetPin(root, "pin_clk", NC);
    c.dc   = GetPin(root, "pin_dc", NC);
    c.rst  = GetPin(root, "pin_rst", NC);
    c.cs   = GetPin(root, "pin_cs", NC);
    c.bl   = GetPin(root, "pin_backlight", NC);
    c.oled_sda = GetPin(root, "oled_sda", NC);
    c.oled_scl = GetPin(root, "oled_scl", NC);

    int codec = GetInt(root, "audio_codec_type", 0);
    c.duplex = codec == 1;
    c.es8311 = codec == 2;
    if (codec < 0 || codec > 2) {
        r.warnings.push_back("audio_codec_type = " + std::to_string(codec) + " không hợp lệ, dùng I2S Simplex");
    }
    c.mic_ws   = GetPin(root, "i2s_mic_ws", c.mic_ws);
    c.mic_sck  = GetPin(root, "i2s_mic_sck", c.mic_sck);
    c.mic_din  = GetPin(root, "i2s_mic_din", c.mic_din);
    c.spk_dout = GetPin(root, "i2s_spk_dout", c.spk_dout);
    c.spk_bclk = GetPin(root, "i2s_spk_bclk", c.spk_bclk);
    c.spk_lrck = GetPin(root, "i2s_spk_lrck", c.spk_lrck);
    c.mclk      = GetPin(root, "i2s_mclk", NC);
    c.codec_sda = GetPin(root, "codec_i2c_sda", NC);
    c.codec_scl = GetPin(root, "codec_i2c_scl", NC);
    c.pa        = GetPin(root, "codec_pa_pin", NC);
    c.pa_inverted = GetBool(root, "codec_pa_inverted", false);

    c.led       = GetPin(root, "pin_led", NC);
    c.led_count = GetInt(root, "led_count", 1);
    // pin_boot / pin_vol_up / pin_vol_dn bi bo qua: nut co dinh 0 / 40 / 39
    c.boot_button = FIXED_BOOT_BUTTON;
    c.vol_up    = FIXED_VOL_UP_BUTTON;
    c.vol_down  = FIXED_VOL_DOWN_BUTTON;
    c.battery_adc = GetPin(root, "pin_battery_level", NC);
    c.charge_pin  = GetPin(root, "pin_charging", NC);
    c.charge_active_high = GetInt(root, "charging_active_level", 0) == 1;
    c.ext_lamp  = GetPin(root, "pin_lamp", NC);

    c.robot     = GetBool(root, "robot_enable", false);
    c.motor_l1  = GetPin(root, "pin_motor_l1", NC);
    c.motor_l2  = GetPin(root, "pin_motor_l2", NC);
    c.motor_r1  = GetPin(root, "pin_motor_r1", NC);
    c.motor_r2  = GetPin(root, "pin_motor_r2", NC);
    c.motor_pwm_l = GetPin(root, "pin_motor_pwm_l", NC);
    c.motor_pwm_r = GetPin(root, "pin_motor_pwm_r", NC);
    c.motor_speed = GetInt(root, "motor_speed", 20);
    c.motor_driver = GetInt(root, "motor_driver", 0);
    c.motor_stby = GetPin(root, "pin_motor_stby", NC);
    c.motor_invert_l = GetBool(root, "motor_invert_l", true);
    c.motor_invert_r = GetBool(root, "motor_invert_r", false);
    c.motor_swap_lr = GetBool(root, "motor_swap_lr", false);
    // file cu chua co func_avoid: co du chan HC-SR04 = bat
    c.func_avoid = GetBool(root, "func_avoid", GetPin(root, "pin_hc04_trig", NC) != NC &&
                                                  GetPin(root, "pin_hc04_echo", NC) != NC);
    c.func_line = GetBool(root, "func_line", false);
    for (int i = 0; i < 6; i++) {
        std::string k = "pin_line_" + std::to_string(i);
        c.line_pins[i] = GetPin(root, k.c_str(), NC);
    }
    c.line_kp = GetFloat(root, "line_kp", 0.04f);
    c.line_ki = GetFloat(root, "line_ki", 0.0f);
    c.line_kd = GetFloat(root, "line_kd", 0.1f);
    c.line_speed = GetInt(root, "line_speed", 50);
    c.line_max_speed = GetInt(root, "line_max_speed", 140);
    c.line_recovery = GetInt(root, "line_recovery", 80);
    c.line_mix_reverse = GetBool(root, "line_mix_reverse", true);
    c.line_white = GetBool(root, "line_white", false);
    c.func_bt = GetBool(root, "func_bt", false);
    c.at09_tx = GetPin(root, "pin_at09_tx", NC);
    c.at09_rx = GetPin(root, "pin_at09_rx", NC);
    c.at09_baud = GetInt(root, "at09_baud", 9600);
    c.hc04_trig = GetPin(root, "pin_hc04_trig", NC);
    c.hc04_echo = GetPin(root, "pin_hc04_echo", NC);
    c.robot_led1 = GetPin(root, "pin_led1", NC);
    c.robot_led2 = GetPin(root, "pin_led2", NC);
    c.obstacle_cm = GetInt(root, "obstacle_cm", 25);
    c.spin_360_ms = GetInt(root, "spin_360_ms", 2200);
    c.robot_idle_move = GetBool(root, "robot_idle_move", true);

    c.sd_miso = GetPin(root, "pin_sd_miso", NC);
    c.sd_clk  = GetPin(root, "pin_sd_clk", NC);
    c.sd_mosi = GetPin(root, "pin_sd_mosi", NC);
    c.sd_cs   = GetPin(root, "pin_sd_cs", NC);
    c.oled2_sda = GetPin(root, "pin_oled2_sda", NC);
    c.oled2_scl = GetPin(root, "pin_oled2_scl", NC);
    c.oled2_sh1106 = GetInt(root, "oled2_driver", 0) == 1;
    c.oled2_height = GetInt(root, "oled2_height", 64) == 32 ? 32 : 64;
    c.oled2_flip = GetBool(root, "oled2_flip", false);
    if (!GetBool(root, "sd_enable", true)) c.sd_miso = c.sd_clk = c.sd_mosi = c.sd_cs = NC;
    if (!GetBool(root, "oled2_enable", true)) c.oled2_sda = c.oled2_scl = NC;
}

inline ParseResult ParseDeviceConfig(const char* json, bool octal_psram) {
    ParseResult r;
    DeviceConfig& c = r.cfg;
    if (json == nullptr || json[0] == '\0') return r;

    cJSON* root = cJSON_Parse(json);
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        r.warnings.push_back("Cấu hình JSON lỗi, dùng mặc định");
        return r;
    }
    r.found = true;

    bool config_json = false;
    for (cJSON* it = root->child; it != nullptr && !config_json; it = it->next) {
        const char* k = it->string ? it->string : "";
        config_json = std::strncmp(k, "pin_", 4) == 0 || std::strncmp(k, "i2s_", 4) == 0 ||
                      std::strncmp(k, "codec_", 6) == 0 || std::strcmp(k, "screen_type") == 0 ||
                      std::strcmp(k, "robot_enable") == 0;
    }
    if (config_json) {
        ReadConfigJson(root, c, r);
    } else {
    // ===== Dinh dang cu cua form =====
    // Loai man: gia tri moi (none/lcd/oled_ssd1306/oled_sh1106) hoac chu cu tu form
    std::string scr;
    GetString(root, "dev_scrtype", scr);
    std::string s = Lower(scr);
    if (s.empty() || s.rfind("none", 0) == 0 || s.find("không") != std::string::npos) {
        c.display = DisplayType::None;
    } else if (s.find("sh1106") != std::string::npos && s.find("ssd1306") == std::string::npos) {
        c.display = DisplayType::OledSh1106;
    } else if (s.find("oled") != std::string::npos) {
        c.display = DisplayType::OledSsd1306;
    } else if (s.find("lcd") != std::string::npos || s.find("tft") != std::string::npos) {
        c.display = DisplayType::Lcd;
    } else {
        c.display = DisplayType::None;
        r.warnings.push_back("Không nhận ra loại màn \"" + scr + "\", tắt màn hình");
    }

    std::string drv;
    GetString(root, "dev_drv", drv);
    std::string d = Lower(drv);
    auto has = [&](const char* k) { return d.find(k) != std::string::npos; };
    if (has("9488"))                        c.driver = LcdDriver::Ili9488;
    else if (has("9486"))                   c.driver = LcdDriver::Ili9486;
    else if (has("9341") || has("9342"))    c.driver = LcdDriver::Ili9341;
    else if (has("9163"))                   c.driver = LcdDriver::Ili9163;
    else if (has("gc9107") || has("gc9106")) c.driver = LcdDriver::Gc9107;
    else if (has("gc9a01"))                 c.driver = LcdDriver::Gc9a01;
    else if (has("7796"))                   c.driver = LcdDriver::St7796;
    else if (has("7735"))                   c.driver = LcdDriver::St7735;
    else if (has("nv3023"))                 c.driver = LcdDriver::Nv3023;
    else                                    c.driver = LcdDriver::St7789;

    c.width    = GetInt(root, "dev_w", c.width);
    c.height   = GetInt(root, "dev_h", c.height);
    c.offset_x = GetInt(root, "dev_ox", 0);
    c.offset_y = GetInt(root, "dev_oy", 0);
    c.spi_mode = GetInt(root, "dev_spi", 0);
    c.spi_mhz  = GetInt(root, "dev_clk", 0);
    std::string rgb;
    GetString(root, "dev_rgb", rgb);
    c.bgr = Lower(rgb) == "bgr";
    c.mirror_x = GetBool(root, "dev_mx", false);
    c.mirror_y = GetBool(root, "dev_my", false);
    c.swap_xy  = GetBool(root, "dev_swap", false);
    c.invert_color = GetBool(root, "dev_inv", false);
    c.backlight_invert = GetBool(root, "dev_blinv", false);

    c.mosi = GetPin(root, "g_mosi", NC);
    c.sclk = GetPin(root, "g_clk", NC);
    c.dc   = GetPin(root, "g_dc", NC);
    c.rst  = GetPin(root, "g_rst", NC);
    c.cs   = GetPin(root, "g_cs", NC);
    c.bl   = GetPin(root, "g_bl", NC);
    c.oled_sda = GetPin(root, "g_oled_sda", NC);
    c.oled_scl = GetPin(root, "g_oled_scl", NC);

    std::string codec;
    GetString(root, "dev_codec", codec);
    c.duplex = Lower(codec).find("duplex") != std::string::npos;
    c.mic_ws   = GetPin(root, "g_ws", c.mic_ws);
    c.mic_sck  = GetPin(root, "g_sck", c.mic_sck);
    c.mic_din  = GetPin(root, "g_din", c.mic_din);
    c.spk_dout = GetPin(root, "g_dout", c.spk_dout);
    c.spk_bclk = GetPin(root, "g_bclk", c.spk_bclk);
    c.spk_lrck = GetPin(root, "g_lrck", c.spk_lrck);

    c.led       = GetPin(root, "g_led", NC);
    c.led_count = GetInt(root, "g_ledn", 1);
    // g_wake / g_vp / g_vm tu web bi bo qua: nut co dinh 0 / 40 / 39
    c.boot_button = FIXED_BOOT_BUTTON;
    c.vol_up    = FIXED_VOL_UP_BUTTON;
    c.vol_down  = FIXED_VOL_DOWN_BUTTON;
    c.battery_adc = GetPin(root, "g_bat", NC);
    c.charge_pin  = GetPin(root, "g_chg", NC);
    c.charge_active_high = GetBool(root, "dev_chg_high", false);
    c.ext_lamp  = GetPin(root, "g_ext", NC);
    }
    cJSON_Delete(root);

    // ---- Kiem tra & sua ----
    if (c.led_count < 1) c.led_count = 1;
    if (c.led_count > 128) c.led_count = 128;
    if (c.spi_mode < 0 || c.spi_mode > 3) c.spi_mode = 0;
    if (c.spi_mhz < 0 || c.spi_mhz > 80) c.spi_mhz = 0;

    auto check_pin = [&](int& pin, const char* label) {
        if (pin != NC && !IsUsablePin(pin, octal_psram)) {
            r.warnings.push_back(std::string(label) + ": " + PinName(pin) + " không dùng được, bỏ qua");
            pin = NC;
        }
    };

    check_pin(c.led, "LED");
    check_pin(c.ext_lamp, "Đèn phụ");
    check_pin(c.charge_pin, "Báo sạc");
    check_pin(c.battery_adc, "Báo pin");
    // Chi GPIO1-20 co ADC tren ESP32-S3 (1-10: ADC1, 11-20: ADC2)
    if (c.battery_adc != NC && (c.battery_adc < 1 || c.battery_adc > 20)) {
        r.warnings.push_back("Báo pin: " + PinName(c.battery_adc) + " không có ADC (chỉ GPIO1-20), bỏ qua");
        c.battery_adc = NC;
    }

    // Am thanh: thieu/sai chan -> dung bo chan mac dinh (de khong crash)
    auto reset_audio = [&c]() {
        DeviceConfig def;
        c.duplex = false;
        c.es8311 = false;
        c.mic_ws = def.mic_ws; c.mic_sck = def.mic_sck; c.mic_din = def.mic_din;
        c.spk_dout = def.spk_dout; c.spk_bclk = def.spk_bclk; c.spk_lrck = def.spk_lrck;
        c.mclk = NC; c.codec_sda = NC; c.codec_scl = NC; c.pa = NC;
    };
    {
        std::vector<std::pair<int, const char*>> need;
        if (c.es8311) {
            need = {{c.codec_sda, "Codec I2C SDA"}, {c.codec_scl, "Codec I2C SCL"}, {c.spk_bclk, "BCLK"},
                    {c.spk_lrck, "LRCK"}, {c.spk_dout, "DOUT Loa"}, {c.mic_din, "DIN Mic"}};
        } else if (c.duplex) {
            need = {{c.spk_bclk, "BCLK"}, {c.spk_lrck, "LRCK"}, {c.spk_dout, "DOUT Loa"}, {c.mic_din, "DIN Mic"}};
        } else {
            need = {{c.mic_ws, "WS Mic"}, {c.mic_sck, "SCK Mic"}, {c.mic_din, "DIN Mic"},
                    {c.spk_dout, "DOUT Loa"}, {c.spk_bclk, "BCLK"}, {c.spk_lrck, "LRCK"}};
        }
        for (auto& n : need) {
            if (!IsUsablePin(n.first, octal_psram)) {
                r.warnings.push_back(std::string("Âm thanh: chân ") + n.second +
                                     " thiếu hoặc sai, dùng chân âm thanh mặc định");
                reset_audio();
                break;
            }
        }
        if (c.es8311) {
            check_pin(c.mclk, "MCLK");
            check_pin(c.pa, "PA loa");
        }
    }

    // Robot: can du 4 chan motor
    if (c.robot) {
        if (c.obstacle_cm < 5 || c.obstacle_cm > 200) c.obstacle_cm = 25;
        if (c.spin_360_ms < 300 || c.spin_360_ms > 10000) c.spin_360_ms = 2200;
        if (!IsUsablePin(c.motor_l1, octal_psram) || !IsUsablePin(c.motor_l2, octal_psram) ||
            !IsUsablePin(c.motor_r1, octal_psram) || !IsUsablePin(c.motor_r2, octal_psram)) {
            r.warnings.push_back("Robot cần đủ 4 chân motor hợp lệ -> tắt robot");
            c.robot = false;
        }
        if (c.motor_speed < 20 || c.motor_speed > 100) c.motor_speed = 20;
        if (c.motor_driver < 0 || c.motor_driver > 7) {
            r.warnings.push_back("motor_driver = " + std::to_string(c.motor_driver) + " không hợp lệ, dùng 4 chân");
            c.motor_driver = 0;
        }
        // driver khong co chan EN/PWM rieng -> bo qua chan PWM/STBY (khong tinh trung)
        const bool has_en = c.motor_driver == 1 || c.motor_driver == 2 || c.motor_driver == 3;
        if (!has_en) c.motor_pwm_l = c.motor_pwm_r = NC;
        if (c.motor_driver != 1) c.motor_stby = NC;
        check_pin(c.motor_pwm_l, "PWM motor trái");
        check_pin(c.motor_pwm_r, "PWM motor phải");
        check_pin(c.motor_stby, "STBY");
        if (c.motor_driver == 1 && (c.motor_pwm_l == NC || c.motor_pwm_r == NC)) {
            r.warnings.push_back("ℹ TB6612 thiếu chân PWMA/PWMB: motor đó chỉ chạy nếu chân PWM nối thẳng lên 3.3V");
        }
        if (!c.func_avoid) c.hc04_trig = c.hc04_echo = NC;
        if (c.func_line) {
            if (c.line_speed < 20 || c.line_speed > 100) c.line_speed = 50;
            if (c.line_max_speed < 0 || c.line_max_speed > 255) c.line_max_speed = 140;
            if (c.line_recovery < 0 || c.line_recovery > 255) c.line_recovery = 80;
            if (c.line_kp < 0 || c.line_kp > 10) c.line_kp = 0.04f;
            if (c.line_ki < 0 || c.line_ki > 1) c.line_ki = 0;
            if (c.line_kd < 0 || c.line_kd > 50) c.line_kd = 0.1f;
            for (int i = 0; i < 6; i++) {
                std::string l = "Cảm biến line S" + std::to_string(i);
                check_pin(c.line_pins[i], l.c_str());
            }
            if (c.LineCount() < 2) {
                r.warnings.push_back("Dò line cần ít nhất 2 cảm biến hợp lệ -> tắt dò line");
                c.func_line = false;
            }
        }
        if (!c.func_line) for (int& p : c.line_pins) p = NC;
        if (c.func_bt) {
            check_pin(c.at09_tx, "AT-09 TX");
            check_pin(c.at09_rx, "AT-09 RX");
            if (c.at09_rx == 46) {
                r.warnings.push_back("AT-09: ESP RX không được dùng GPIO46 (TXD luôn mức 1, hỏng chế độ nạp) -> tắt AT-09");
                c.at09_tx = c.at09_rx = NC;
            }
            if (c.at09_tx == NC || c.at09_rx == NC || c.at09_tx == c.at09_rx) {
                if (c.at09_tx != NC || c.at09_rx != NC)
                    r.warnings.push_back("AT-09 cần đủ chân TX và RX khác nhau -> tắt điều khiển Bluetooth");
                c.func_bt = false;
            }
            static const int kBauds[] = {9600, 19200, 38400, 57600, 115200};
            if (std::find(std::begin(kBauds), std::end(kBauds), c.at09_baud) == std::end(kBauds)) c.at09_baud = 9600;
        }
        if (!c.func_bt) c.at09_tx = c.at09_rx = NC;
        check_pin(c.hc04_trig, "HC-SR04 TRIG");
        check_pin(c.hc04_echo, "HC-SR04 ECHO");
        check_pin(c.robot_led1, "LED 1");
        check_pin(c.robot_led2, "LED 2");
        if ((c.hc04_trig == NC) != (c.hc04_echo == NC)) {
            r.warnings.push_back("HC-SR04 cần cả TRIG và ECHO -> bỏ cảm biến");
            c.hc04_trig = c.hc04_echo = NC;
        }
    }

    // The nho: can du 4 chan
    if (c.sd_miso != NC || c.sd_clk != NC || c.sd_mosi != NC || c.sd_cs != NC) {
        if (!IsUsablePin(c.sd_miso, octal_psram) || !IsUsablePin(c.sd_clk, octal_psram) ||
            !IsUsablePin(c.sd_mosi, octal_psram) || !IsUsablePin(c.sd_cs, octal_psram)) {
            r.warnings.push_back("Thẻ nhớ cần đủ 4 chân MISO, CLK, MOSI, CS hợp lệ -> bỏ thẻ nhớ");
            c.sd_miso = c.sd_clk = c.sd_mosi = c.sd_cs = NC;
        }
    }

    // OLED phu: can SDA + SCL
    if (c.oled2_sda != NC || c.oled2_scl != NC) {
        if (!IsUsablePin(c.oled2_sda, octal_psram) || !IsUsablePin(c.oled2_scl, octal_psram) ||
            c.oled2_sda == c.oled2_scl) {
            r.warnings.push_back("OLED phụ cần chân SDA, SCL hợp lệ -> bỏ OLED phụ");
            c.oled2_sda = c.oled2_scl = NC;
        }
    }

    if (!c.robot) {
        c.func_line = c.func_bt = false;
        for (int& p : c.line_pins) p = NC;
        c.at09_tx = c.at09_rx = NC;
    }

    // Man hinh
    if (c.display == DisplayType::Lcd) {
        check_pin(c.cs, "CS");
        check_pin(c.rst, "RST");
        check_pin(c.bl, "BL");
        if (!IsUsablePin(c.mosi, octal_psram) || !IsUsablePin(c.sclk, octal_psram) ||
            !IsUsablePin(c.dc, octal_psram)) {
            r.warnings.push_back("Màn LCD cần đủ chân MOSI, CLK, DC hợp lệ -> tắt màn hình");
            c.display = DisplayType::None;
        }
        if (c.width < 16 || c.width > 800 || c.height < 16 || c.height > 800) {
            r.warnings.push_back("Kích thước màn không hợp lệ -> tắt màn hình");
            c.display = DisplayType::None;
        }
    } else if (c.display == DisplayType::OledSsd1306 || c.display == DisplayType::OledSh1106) {
        if (!IsUsablePin(c.oled_sda, octal_psram) || !IsUsablePin(c.oled_scl, octal_psram)) {
            r.warnings.push_back("Màn OLED cần chân SDA, SCL hợp lệ -> tắt màn hình");
            c.display = DisplayType::None;
        }
        c.width = 128;
        if (c.height != 32 && c.height != 64) c.height = (c.height > 48) ? 64 : 32;
    }

    // OLED phu: ESP32-S3 chi co 2 bus I2C. OLED chinh dung 1 bus, ES8311 dung 1 bus.
    if (c.HasOled2() && !Oled2SharesCodecBus(c)) {
        bool same_as_main = IsMainOled(c) && c.oled2_sda == c.oled_sda && c.oled2_scl == c.oled_scl;
        int busy = (IsMainOled(c) ? 1 : 0) + ((c.es8311 && !OledSharesCodecBus(c)) ? 1 : 0);
        if (same_as_main) {
            r.warnings.push_back("OLED phụ trùng SDA/SCL với OLED chính (cùng địa chỉ 0x3C) -> bỏ OLED phụ");
            c.oled2_sda = c.oled2_scl = NC;
        } else if (busy >= 2) {
            r.warnings.push_back("Hết bus I2C (OLED chính + ES8311) -> bỏ OLED phụ. Gợi ý: nối OLED phụ chung "
                                 "SDA/SCL với ES8311");
            c.oled2_sda = c.oled2_scl = NC;
        }
    }

    // Trung chan: chi xet nhung chan thuc su duoc dung.
    // Thu tu uu tien: nut co dinh > am thanh > man hinh > ngoai vi phu. Lap lai den khi het trung.
    enum Group { kFixed, kAudio, kDisplay, kRobot, kLine, kBt, kSd, kOled2, kExtra };
    struct Use { int pin; const char* name; Group group; int* ref; };
    for (int guard = 0; guard < 16; guard++) {
        std::vector<Use> uses;
        auto add = [&](int& pin, const char* name, Group g) { if (pin != NC) uses.push_back({pin, name, g, &pin}); };
        // Nut co dinh dung dau tien -> khong bao gio bi bo, thiet bi khac trung chan se bi bo
        add(c.boot_button, "Nút BOOT (GPIO0)", kFixed);
        add(c.vol_up, "Nút tăng âm (GPIO40)", kFixed);
        add(c.vol_down, "Nút giảm âm (GPIO39)", kFixed);
        if (c.es8311) {
            add(c.spk_bclk, "BCLK", kAudio); add(c.spk_lrck, "LRCK", kAudio);
            add(c.spk_dout, "DOUT Loa", kAudio); add(c.mic_din, "DIN Mic", kAudio);
            add(c.mclk, "MCLK", kAudio); add(c.codec_sda, "Codec I2C SDA", kAudio);
            add(c.codec_scl, "Codec I2C SCL", kAudio); add(c.pa, "PA loa", kAudio);
        } else if (c.duplex) {
            add(c.spk_bclk, "BCLK", kAudio); add(c.spk_lrck, "LRCK", kAudio);
            add(c.spk_dout, "DOUT Loa", kAudio); add(c.mic_din, "DIN Mic", kAudio);
        } else {
            add(c.mic_ws, "WS Mic", kAudio); add(c.mic_sck, "SCK Mic", kAudio); add(c.mic_din, "DIN Mic", kAudio);
            add(c.spk_dout, "DOUT Loa", kAudio); add(c.spk_bclk, "BCLK", kAudio); add(c.spk_lrck, "LRCK", kAudio);
        }
        if (c.display == DisplayType::Lcd) {
            add(c.mosi, "MOSI", kDisplay); add(c.sclk, "CLK", kDisplay); add(c.dc, "DC", kDisplay);
            add(c.rst, "RST", kDisplay); add(c.cs, "CS", kDisplay); add(c.bl, "BL", kDisplay);
        } else if (c.display != DisplayType::None && !OledSharesCodecBus(c)) {
            // OLED dung chung bus I2C voi ES8311 (cung SDA/SCL) thi khong tinh la trung
            add(c.oled_sda, "OLED SDA", kDisplay); add(c.oled_scl, "OLED SCL", kDisplay);
        }
        if (c.robot) {
            add(c.motor_l1, "Motor trái IN1", kRobot); add(c.motor_l2, "Motor trái IN2", kRobot);
            add(c.motor_r1, "Motor phải IN3", kRobot); add(c.motor_r2, "Motor phải IN4", kRobot);
            add(c.motor_pwm_l, "PWM motor trái", kRobot); add(c.motor_pwm_r, "PWM motor phải", kRobot);
            add(c.motor_stby, "STBY TB6612", kRobot);
            static const char* kLineNames[6] = {"Cảm biến line S0", "Cảm biến line S1", "Cảm biến line S2",
                                                "Cảm biến line S3", "Cảm biến line S4", "Cảm biến line S5"};
            if (c.func_line)
                for (int i = 0; i < 6; i++) add(c.line_pins[i], kLineNames[i], kLine);
            if (c.func_bt) {
                add(c.at09_tx, "AT-09 TX", kBt); add(c.at09_rx, "AT-09 RX", kBt);
            }
            add(c.hc04_trig, "HC-SR04 TRIG", kExtra); add(c.hc04_echo, "HC-SR04 ECHO", kExtra);
            add(c.robot_led1, "LED 1", kExtra); add(c.robot_led2, "LED 2", kExtra);
        }
        if (c.HasSd()) {
            add(c.sd_miso, "Thẻ nhớ MISO", kSd); add(c.sd_clk, "Thẻ nhớ CLK", kSd);
            add(c.sd_mosi, "Thẻ nhớ MOSI", kSd); add(c.sd_cs, "Thẻ nhớ CS", kSd);
        }
        if (c.HasOled2() && !Oled2SharesCodecBus(c)) {
            add(c.oled2_sda, "OLED phụ SDA", kOled2); add(c.oled2_scl, "OLED phụ SCL", kOled2);
        }
        add(c.led, "LED", kExtra); add(c.ext_lamp, "Đèn phụ", kExtra);
        add(c.battery_adc, "Báo pin", kExtra); add(c.charge_pin, "Báo sạc", kExtra);

        const Use* first = nullptr;
        const Use* second = nullptr;
        for (size_t i = 0; i < uses.size() && !second; i++) {
            for (size_t j = 0; j < i; j++) {
                if (uses[i].pin == uses[j].pin) { first = &uses[j]; second = &uses[i]; break; }
            }
        }
        if (!second) break;

        std::string msg = "Trùng " + PinName(second->pin) + ": " + first->name + " và " + second->name;
        // bo thiet bi uu tien thap hon (nhom lon hon); bang nhau -> bo cai sau
        const Use* victim = first->group > second->group ? first : second;
        if (victim->group == kExtra) {
            *victim->ref = NC;
            // HC-SR04 thieu 1 chan thi bo ca cam bien
            if (victim->ref == &c.hc04_trig || victim->ref == &c.hc04_echo) c.hc04_trig = c.hc04_echo = NC;
            r.warnings.push_back(msg + " -> bỏ " + victim->name);
        } else if (victim->group == kRobot) {
            c.robot = false;
            r.warnings.push_back(msg + " -> tắt robot");
        } else if (victim->group == kLine) {
            c.func_line = false;
            for (int& p : c.line_pins) p = NC;
            r.warnings.push_back(msg + " -> tắt dò line");
        } else if (victim->group == kBt) {
            c.func_bt = false;
            c.at09_tx = c.at09_rx = NC;
            r.warnings.push_back(msg + " -> tắt điều khiển Bluetooth AT-09");
        } else if (victim->group == kSd) {
            c.sd_miso = c.sd_clk = c.sd_mosi = c.sd_cs = NC;
            r.warnings.push_back(msg + " -> bỏ thẻ nhớ");
        } else if (victim->group == kOled2) {
            c.oled2_sda = c.oled2_scl = NC;
            r.warnings.push_back(msg + " -> bỏ OLED phụ");
        } else if (victim->group == kDisplay) {
            c.display = DisplayType::None;
            r.warnings.push_back(msg + " -> tắt màn hình");
        } else {
            // trung giua 2 chan am thanh -> dung bo chan mac dinh
            reset_audio();
            r.warnings.push_back(msg + " -> dùng chân âm thanh mặc định");
        }
    }

    // Luu y chan dac biet (chi canh bao, van dung)
    {
        std::vector<int> used = {c.mosi, c.sclk, c.dc, c.rst, c.cs, c.bl, c.led, c.ext_lamp, c.battery_adc,
                                 c.charge_pin, c.sd_miso, c.sd_clk, c.sd_mosi, c.sd_cs, c.oled2_sda, c.oled2_scl};
        if (IsMainOled(c)) { used.push_back(c.oled_sda); used.push_back(c.oled_scl); }
        if (c.robot) {
            for (int p : {c.motor_l1, c.motor_l2, c.motor_r1, c.motor_r2, c.motor_pwm_l, c.motor_pwm_r, c.motor_stby,
                          c.hc04_trig, c.hc04_echo, c.robot_led1, c.robot_led2}) used.push_back(p);
            for (int p : c.line_pins) used.push_back(p);
            used.push_back(c.at09_tx);
            used.push_back(c.at09_rx);
        }
        for (int p : {c.mic_ws, c.mic_sck, c.mic_din, c.spk_dout, c.spk_bclk, c.spk_lrck, c.mclk, c.codec_sda,
                      c.codec_scl, c.pa}) used.push_back(p);
        auto uses = [&](int pin) { return std::find(used.begin(), used.end(), pin) != used.end(); };
        if (uses(43) || uses(44))
            r.warnings.push_back("ℹ GPIO43/44 (TX/RX) đang dùng: lúc nạp firmware phải tháo dây 2 chân này ra rồi mới nạp");
        if (c.robot && c.func_line) {
            std::string dig, adc2;
            for (int i = 0; i < 6; i++) {
                int p = c.line_pins[i];
                if (p == NC) continue;
                if (p > 20) dig += " S" + std::to_string(i);
                else if (p >= 11) adc2 += " S" + std::to_string(i);
            }
            if (!dig.empty())
                r.warnings.push_back("ℹ Cảm biến line" + dig + " không có ADC -> đọc digital 0/1 (analog: GPIO1-10)");
            if (!adc2.empty())
                r.warnings.push_back("ℹ Cảm biến line" + adc2 + " ở ADC2 (GPIO11-20): kém ổn định khi bật Wi-Fi");
            bool line_adc1 = false;
            for (int p : c.line_pins) line_adc1 |= (p >= 1 && p <= 10);
            if (c.battery_adc != NC && line_adc1 && c.battery_adc <= 10)
                r.warnings.push_back("ℹ Báo pin dùng chung ADC1 với cảm biến line -> cảm biến line đọc digital");
        }
        if (uses(19) || uses(20))
            r.warnings.push_back("ℹ GPIO19/20 là chân USB D-/D+: mất cổng USB native (nạp/xem log qua cổng UART)");
        // I2C luon co dien tro keo len -> GPIO46 = 1 luc khoi dong -> khong vao duoc che do nap (download)
        bool i2c46 = c.oled2_sda == 46 || c.oled2_scl == 46 ||
                     (IsMainOled(c) && (c.oled_sda == 46 || c.oled_scl == 46)) ||
                     (c.es8311 && (c.codec_sda == 46 || c.codec_scl == 46));
        if (c.HasSd() && (c.sd_cs == 46 || c.sd_mosi == 46 || c.sd_miso == 46 || c.sd_clk == 46))
            r.warnings.push_back("ℹ GPIO46 là chân strapping, dùng cho thẻ nhớ: nếu nạp firmware báo lỗi vào chế độ "
                                 "nạp thì rút module thẻ ra rồi nạp lại");
        if (i2c46)
            r.warnings.push_back("ℹ GPIO46 là chân strapping, I2C kéo nó lên mức 1: khi nạp firmware nếu báo lỗi "
                                 "vào chế độ nạp thì rút OLED/thiết bị I2C ra rồi nạp lại");
    }
    return r;
}

}  // namespace multi

#endif  // _MULTI_DEVICE_CONFIG_H_

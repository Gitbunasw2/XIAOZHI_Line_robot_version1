// Man OLED PHU (SSD1306 / SH1106 I2C, 128x64 hoac 128x32) chay doc lap voi man chinh.
// Hien: gio + Wi-Fi + pin + trang thai, 2 mat robot theo cam xuc (chop mat, liec),
// khoang cach HC-SR04, chu AI dang noi (tieng Viet bo dau, tu cuon trang).
// Nhan cam xuc / chu qua WithAuxHook<Display> (bao quanh man chinh).
#ifndef _MULTI_OLED_AUX_H_
#define _MULTI_OLED_AUX_H_

#include "oled_canvas.h"
#include "application.h"
#include "device_state.h"
#include "wifi_manager.h"

#include <driver/i2c_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_ssd1306.h>
#include <esp_lcd_panel_sh1106.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <ctime>
#include <functional>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

class OledAux;
// Man phu dang chay (nullptr = khong co). Display hook gui cam xuc / chu vao day.
inline OledAux* g_oled_aux = nullptr;

class OledAux {
public:
    struct Sources {
        std::function<bool(int& level, bool& charging)> battery;  // tra false = khong co pin
        std::function<int()> distance_cm;                         // -2 = khong co cam bien
    };

    // Tao I2C bus rieng cho man phu. port: I2C_NUM_0 / I2C_NUM_1.
    static i2c_master_bus_handle_t NewBus(int port, int sda, int scl) {
        i2c_master_bus_config_t cfg = {};
        cfg.i2c_port = (i2c_port_t)port;
        cfg.sda_io_num = (gpio_num_t)sda;
        cfg.scl_io_num = (gpio_num_t)scl;
        cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        cfg.glitch_ignore_cnt = 7;
        cfg.flags.enable_internal_pullup = 1;
        i2c_master_bus_handle_t bus = nullptr;
        if (i2c_new_master_bus(&cfg, &bus) != ESP_OK) {
            ESP_LOGE(TAG, "I2C bus OLED phu (SDA %d, SCL %d) loi", sda, scl);
            return nullptr;
        }
        return bus;
    }

    // height 64/32, flip = lat 180 do, sh1106 = chip SH1106. Tra ve nullptr neu man khong tra loi.
    static OledAux* Create(i2c_master_bus_handle_t bus, int height, bool flip, bool sh1106, uint8_t addr = 0x3C) {
        if (bus == nullptr) return nullptr;
        if (i2c_master_probe(bus, addr, 100) != ESP_OK) {
            ESP_LOGE(TAG, "Khong thay OLED phu o dia chi 0x%02X (kiem tra SDA/SCL)", addr);
            return nullptr;
        }
        esp_lcd_panel_io_handle_t io = nullptr;
        esp_lcd_panel_io_i2c_config_t io_config = {};
        io_config.dev_addr = addr;
        io_config.scl_speed_hz = 400 * 1000;
        io_config.control_phase_bytes = 1;
        io_config.dc_bit_offset = 6;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        if (esp_lcd_new_panel_io_i2c(bus, &io_config, &io) != ESP_OK) return nullptr;

        esp_lcd_panel_handle_t panel = nullptr;
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;
        esp_lcd_panel_ssd1306_config_t ssd1306_config = {};
        ssd1306_config.height = (uint8_t)(height == 32 ? 32 : 64);
        panel_config.vendor_config = &ssd1306_config;
        esp_err_t err = sh1106 ? esp_lcd_new_panel_sh1106(io, &panel_config, &panel)
                               : esp_lcd_new_panel_ssd1306(io, &panel_config, &panel);
        if (err != ESP_OK || esp_lcd_panel_reset(panel) != ESP_OK || esp_lcd_panel_init(panel) != ESP_OK) {
            ESP_LOGE(TAG, "Khoi tao OLED phu that bai");
            return nullptr;
        }
        // 0.96" thuong lap nguoc: mirror ca 2 chieu = dung chieu (giong bread-compact-wifi)
        esp_lcd_panel_mirror(panel, !flip, !flip);
        esp_lcd_panel_invert_color(panel, false);
        esp_lcd_panel_disp_on_off(panel, true);
        auto* aux = new OledAux(panel, ssd1306_config.height);
        g_oled_aux = aux;
        ESP_LOGI(TAG, "OLED phu %s 128x%d san sang", sh1106 ? "SH1106" : "SSD1306", ssd1306_config.height);
        return aux;
    }

    void SetSources(Sources s) {
        std::lock_guard<std::mutex> lock(mutex_);
        sources_ = std::move(s);
    }

    void SetEmotion(const char* emotion) {
        std::lock_guard<std::mutex> lock(mutex_);
        eyes_ = oled::EyesFromEmotion(emotion ? emotion : "");
    }

    // role: "assistant" (AI noi), "user" (nguoi noi), "system" (thong bao)
    void SetChatMessage(const char* role, const char* content) {
        std::string text = oled::Utf8ToAscii(content);
        std::lock_guard<std::mutex> lock(mutex_);
        if (role != nullptr && strcmp(role, "user") == 0 && !text.empty()) {
            text = "> " + text;
        }
        if (text.empty() && role != nullptr && strcmp(role, "system") != 0) {
            return;  // giu chu cu khi AI gui chuoi rong
        }
        lines_ = oled::WrapText(text, 21);
        page_ = 0;
        page_time_us_ = esp_timer_get_time();
    }

    void SetMusic(bool playing) { music_ = playing; }
    void SetSleep(bool sleep) { sleep_ = sleep; }
    void PowerOff() {
        off_ = true;
        esp_lcd_panel_disp_on_off(panel_, false);
    }

private:
    static constexpr const char* TAG = "OledAux";
    esp_lcd_panel_handle_t panel_;
    oled::Canvas canvas_;
    std::vector<uint8_t> last_;
    std::mutex mutex_;
    Sources sources_;
    oled::Eyes eyes_ = oled::Eyes::Normal;
    std::vector<std::string> lines_;
    size_t page_ = 0;
    int64_t page_time_us_ = 0;
    std::atomic<bool> music_{false};
    std::atomic<bool> sleep_{false};
    std::atomic<bool> off_{false};

    OledAux(esp_lcd_panel_handle_t panel, int height) : panel_(panel), canvas_(height) {
        xTaskCreate(
            [](void* arg) {
                static_cast<OledAux*>(arg)->Loop();
                vTaskDelete(nullptr);
            },
            "oled_aux", 4096, this, 2, nullptr);
    }

    static const char* StateText(DeviceState s) {
        switch (s) {
            case kDeviceStateStarting: return "Khoi dong";
            case kDeviceStateWifiConfiguring: return "Cai WiFi";
            case kDeviceStateIdle: return "San sang";
            case kDeviceStateConnecting: return "Ket noi";
            case kDeviceStateListening: return "Dang nghe";
            case kDeviceStateSpeaking: return "Dang noi";
            case kDeviceStateNotifying: return "Thong bao";
            case kDeviceStateUpgrading: return "Cap nhat";
            case kDeviceStateActivating: return "Kich hoat";
            case kDeviceStateAudioTesting: return "Thu mic";
            case kDeviceStateFatalError: return "Loi";
            default: return "";
        }
    }

    static int WifiLevel() {
        auto& wifi = WifiManager::GetInstance();
        if (!wifi.IsConnected()) return -1;
        int rssi = wifi.GetRssi();
        if (rssi >= -55) return 4;
        if (rssi >= -65) return 3;
        if (rssi >= -75) return 2;
        if (rssi >= -85) return 1;
        return 0;
    }

    void Loop() {
        const int kFrameMs = 80;
        int frame = 0;
        int64_t next_blink = esp_timer_get_time() + 3000000;
        int blink_step = -1;
        int64_t next_look = esp_timer_get_time() + 2000000;
        int look_x = 0, look_y = 0;
        int wifi = -1, battery = -1, distance = -2;
        bool charging = false;
        static const float kBlink[] = {0.5f, 1.0f, 1.0f, 0.5f};

        while (true) {
            vTaskDelay(pdMS_TO_TICKS(kFrameMs));
            if (off_) continue;
            int64_t now = esp_timer_get_time();
            frame++;

            Sources src;
            oled::Frame f;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                src = sources_;
                f.eyes = eyes_;
                if (lines_.size() > 2 && now - page_time_us_ > 1800000) {  // cuon 1 dong moi 1.8 s
                    if (page_ + 2 < lines_.size()) page_++;
                    page_time_us_ = now;
                }
                if (page_ < lines_.size()) f.line1 = lines_[page_];
                if (page_ + 1 < lines_.size()) f.line2 = lines_[page_ + 1];
            }

            // cap nhat cham: Wi-Fi 2 s, pin 5 s, khoang cach 0.5 s
            if (frame % 25 == 1) wifi = WifiLevel();
            if (frame % 62 == 1) {
                int lv = 0;
                bool ch = false;
                if (src.battery && src.battery(lv, ch)) {
                    battery = lv;
                    charging = ch;
                } else {
                    battery = -1;
                }
            }
            if (frame % 6 == 1) distance = src.distance_cm ? src.distance_cm() : -2;

            // chop mat ngau nhien 2.5..6 s, liec mat 2..5 s
            if (blink_step < 0 && now >= next_blink) blink_step = 0;
            if (blink_step >= 0) {
                f.blink = kBlink[blink_step++];
                if (blink_step >= 4) {
                    blink_step = -1;
                    next_blink = now + 2500000 + (esp_random() % 3500000);
                }
            }
            if (now >= next_look) {
                static const int kx[] = {-5, 0, 0, 5}, ky[] = {-2, 0, 0, 2};
                look_x = kx[esp_random() % 4];
                look_y = ky[esp_random() % 4];
                next_look = now + 2000000 + (esp_random() % 3000000);
            }
            auto state = Application::GetInstance().GetDeviceState();
            f.look_x = look_x;
            f.look_y = look_y;
            if (state == kDeviceStateSpeaking) f.look_y += (frame / 3) % 2;  // nhun nhay khi noi
            if (sleep_) f.eyes = oled::Eyes::Sleepy;

            time_t t = time(nullptr);
            struct tm tm_now;
            localtime_r(&t, &tm_now);
            if (tm_now.tm_year >= 2024 - 1900) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
                f.time = buf;
            }
            f.state = music_ && state == kDeviceStateIdle ? "Nhac" : StateText(state);
            f.wifi = wifi;
            f.battery = battery;
            f.charging = charging;
            f.distance_cm = distance;

            oled::RenderFrame(canvas_, f);
            if (last_.size() == canvas_.Size() && memcmp(last_.data(), canvas_.Data(), canvas_.Size()) == 0) {
                continue;  // khong doi -> khong gui I2C
            }
            last_.assign(canvas_.Data(), canvas_.Data() + canvas_.Size());
            esp_lcd_panel_draw_bitmap(panel_, 0, 0, oled::Canvas::W, canvas_.Height(), canvas_.Data());
        }
    }
};

// Bao quanh lop man chinh: chuyen cam xuc + chu sang man phu (neu co).
template <class Base>
class WithAuxHook : public Base {
public:
    using Base::Base;
    void SetEmotion(const char* emotion) override {
        Base::SetEmotion(emotion);
        if (g_oled_aux) g_oled_aux->SetEmotion(emotion);
    }
    void SetChatMessage(const char* role, const char* content) override {
        Base::SetChatMessage(role, content);
        if (g_oled_aux) g_oled_aux->SetChatMessage(role, content);
    }
};

#endif  // _MULTI_OLED_AUX_H_

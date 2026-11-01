// Board da nang "bread-compact-wifi-multi" (ESP32-S3, Wi-Fi)
// Chan GPIO doc luc khoi dong tu NVS rb5320/device_json, duoc luu tu trang
// http://192.168.4.1 -> tab "Cau hinh Thiet bi". Khong can build lai firmware khi doi chan.

#include "wifi_board.h"
#include "wifi_manager.h"
#include "codecs/no_audio_codec.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "display/oled_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "led/circular_strip.h"
#include "adc_battery_monitor.h"
#include "assets/lang_config.h"
#include "lvgl_theme.h"
#include "lvgl_font.h"
#include "device_config.h"
#include "rb_robot.h"
#include "line_follower.h"
#include "dabble_at09.h"
#include "multi_splash.h"
#include "multi_wallpaper.h"
#include "oled_aux.h"
#include "sd_card.h"
#include "sd_music.h"

#include <esp_log.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_mac.h>
#include <esp_app_desc.h>
#include "settings.h"
#include <lvgl.h>
#include <nvs.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_interface.h>
#include <esp_lcd_ili9341.h>
#include <esp_lcd_gc9a01.h>
#include <esp_lcd_st7796.h>
#include <esp_lcd_ili9488.h>
#include <esp_lcd_panel_sh1106.h>
#include <esp_lcd_nv3023.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>

#define TAG "MultiWifiBoard"

#ifndef ES8311_CODEC_DEFAULT_ADDR
#define ES8311_CODEC_DEFAULT_ADDR 0x30  // dia chi I2C mac dinh cua ES8311 (8-bit)
#endif

using multi::DeviceConfig;
using multi::DisplayType;
using multi::LcdDriver;

LV_FONT_DECLARE(font_noto_sans_basic_14_1);
LV_FONT_DECLARE(font_material_symbols_14_1);

// ---------------- Che do an toan: dem so lan crash lien tiep ----------------
#define MULTI_RTC_MAGIC 0x4D554C54u  // "MULT"
static RTC_NOINIT_ATTR uint32_t s_rtc_magic;
static RTC_NOINIT_ATTR uint32_t s_crash_count;

static bool UpdateCrashCounter() {
    esp_reset_reason_t reason = esp_reset_reason();
    if (s_rtc_magic != MULTI_RTC_MAGIC) {
        s_rtc_magic = MULTI_RTC_MAGIC;
        s_crash_count = 0;
    }
    bool crashed = reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
                   reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT ||
                   reason == ESP_RST_BROWNOUT;
    s_crash_count = crashed ? s_crash_count + 1 : 0;
    ESP_LOGI(TAG, "Reset reason %d, crash count %lu", (int)reason, (unsigned long)s_crash_count);
    return s_crash_count >= SAFE_MODE_CRASH_LIMIT;
}

// ---------------- ILI9488: SPI 18-bit, dao byte RGB565 tu LVGL ----------------
static esp_err_t (*s_ili9488_draw_bitmap)(esp_lcd_panel_t*, int, int, int, int, const void*) = nullptr;
static int s_ili9488_buffer_pixels = 0;

static esp_err_t Ili9488DrawBitmapSwapped(esp_lcd_panel_t* panel, int x_start, int y_start,
                                          int x_end, int y_end, const void* color_data) {
    const int w = x_end - x_start;
    const int h = y_end - y_start;
    uint16_t* px = (uint16_t*)color_data;
    const size_t count = (size_t)w * (size_t)h;
    for (size_t i = 0; i < count; i++) {
        px[i] = (uint16_t)((px[i] << 8) | (px[i] >> 8));
    }
    int rows = std::max(1, s_ili9488_buffer_pixels / std::max(1, w));
    for (int y = y_start; y < y_end; y += rows) {
        int y2 = std::min(y + rows, y_end);
        esp_err_t err = s_ili9488_draw_bitmap(panel, x_start, y, x_end, y2, px + (size_t)(y - y_start) * w);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

// ---------------- Chuoi khoi tao cho cac chip SPI dung driver chung ----------------
// Driver ILI9341 gui SLPOUT, MADCTL, COLMOD (16-bit) roi chay cac lenh duoi day,
// nen dung chung duoc cho cac chip MIPI-DBI khac. Khong dat MADCTL/COLMOD o day.
static const ili9341_lcd_init_cmd_t st7735_init_cmds[] = {
    {0xB1, (uint8_t[]){0x01, 0x2C, 0x2D}, 3, 0},                    // FRMCTR1
    {0xB2, (uint8_t[]){0x01, 0x2C, 0x2D}, 3, 0},                    // FRMCTR2
    {0xB3, (uint8_t[]){0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}, 6, 0},  // FRMCTR3
    {0xB4, (uint8_t[]){0x07}, 1, 0},                                // INVCTR
    {0xC0, (uint8_t[]){0xA2, 0x02, 0x84}, 3, 0},                    // PWCTR1
    {0xC1, (uint8_t[]){0xC5}, 1, 0},
    {0xC2, (uint8_t[]){0x0A, 0x00}, 2, 0},
    {0xC3, (uint8_t[]){0x8A, 0x2A}, 2, 0},
    {0xC4, (uint8_t[]){0x8A, 0xEE}, 2, 0},
    {0xC5, (uint8_t[]){0x0E}, 1, 0},                                // VMCTR1
    {0xE0, (uint8_t[]){0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
                       0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10}, 16, 0},
    {0xE1, (uint8_t[]){0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                       0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10}, 16, 0},
    {0x13, (uint8_t[]){0x00}, 0, 10},                               // NORON
};

static const ili9341_lcd_init_cmd_t ili9486_init_cmds[] = {
    {0xC2, (uint8_t[]){0x44}, 1, 0},
    {0xC5, (uint8_t[]){0x00, 0x00, 0x00, 0x00}, 4, 0},
    {0xE0, (uint8_t[]){0x0F, 0x1F, 0x1C, 0x0C, 0x0F, 0x08, 0x48, 0x98,
                       0x37, 0x0A, 0x13, 0x04, 0x11, 0x0D, 0x00}, 15, 0},
    {0xE1, (uint8_t[]){0x0F, 0x32, 0x2E, 0x0B, 0x0D, 0x05, 0x47, 0x75,
                       0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00}, 15, 0},
};

static const ili9341_lcd_init_cmd_t ili9163_init_cmds[] = {
    {0x26, (uint8_t[]){0x04}, 1, 0},                                // gamma curve
    {0xB1, (uint8_t[]){0x08, 0x08}, 2, 0},
    {0xB4, (uint8_t[]){0x07}, 1, 0},
    {0xC0, (uint8_t[]){0x0A, 0x02}, 2, 0},
    {0xC1, (uint8_t[]){0x02}, 1, 0},
    {0xC5, (uint8_t[]){0x50, 0x5B}, 2, 0},
    {0xC7, (uint8_t[]){0x40}, 1, 0},
    {0xF2, (uint8_t[]){0x01}, 1, 0},
    {0xE0, (uint8_t[]){0x3F, 0x25, 0x1C, 0x1E, 0x20, 0x12, 0x2A, 0x90,
                       0x24, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00}, 15, 0},
    {0xE1, (uint8_t[]){0x20, 0x20, 0x20, 0x20, 0x05, 0x00, 0x15, 0xA7,
                       0x3D, 0x18, 0x25, 0x2A, 0x2B, 0x2B, 0x3A}, 15, 0},
    {0x13, (uint8_t[]){0x00}, 0, 10},
};

// GC9107/GC9106 (lay tu board bread-compact-wifi-lcd), chay bang driver GC9A01
static const gc9a01_lcd_init_cmd_t gc9107_init_cmds[] = {
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0, (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C, 0x04, 0x12, 0x14, 0x1f}, 14, 0},
    {0xf1, (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D, 0x0C, 0x1A, 0x14, 0x1E}, 14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x29, (uint8_t[]){0x00}, 0, 20},
};

// ---------------- OLED: dung font 1-bit 14px thay vi font LCD ----------------
class MultiOledDisplay : public OledDisplay {
public:
    MultiOledDisplay(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel, int w, int h, bool mx, bool my)
        : OledDisplay(io, panel, w, h, mx, my) {
        if (auto dark = LvglThemeManager::GetInstance().GetTheme("dark")) {
            dark->set_text_font(std::make_shared<LvglBuiltInFont>(&font_noto_sans_basic_14_1));
            dark->set_icon_font(std::make_shared<LvglBuiltInFont>(&font_material_symbols_14_1));
        }
    }
    // Font trong assets la font LCD 4-bit 16px -> khong ap len OLED
    bool SetTextFont(std::shared_ptr<LvglFont> text_font) override {
        (void)text_font;
        return false;
    }
};

// ======================================================================
class MultiWifiBoard : public WifiBoard {
private:
    DeviceConfig cfg_;
    bool safe_mode_ = false;
    std::string status_;

    Display* display_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    i2c_master_bus_handle_t i2c_bus_ = nullptr;        // OLED (I2C_NUM_0)
    i2c_master_bus_handle_t codec_i2c_bus_ = nullptr;  // ES8311
    i2c_port_t codec_i2c_port_ = I2C_NUM_1;

    std::unique_ptr<Button> boot_button_;
    std::unique_ptr<Button> volume_up_button_;
    std::unique_ptr<Button> volume_down_button_;
    std::unique_ptr<Led> led_;
    std::unique_ptr<PwmBacklight> backlight_;
    std::unique_ptr<AudioCodec> audio_codec_;
    std::unique_ptr<LampController> lamp_;
    std::unique_ptr<AdcBatteryMonitor> battery_;
    std::unique_ptr<RbRobot> robot_;
    std::unique_ptr<LineFollower> line_;
    std::unique_ptr<At09Gamepad> gamepad_;
    std::unique_ptr<SdMusicPlayer> music_;
    OledAux* oled_aux_ = nullptr;
    bool sd_mounted_ = false;
    std::function<void()> hide_splash_;
    esp_timer_handle_t stable_timer_ = nullptr;

    // ---------- cau hinh ----------
    void LoadConfig() {
#if CONFIG_SPIRAM_MODE_OCT
        const bool octal_psram = true;
#else
        const bool octal_psram = false;
#endif
        std::string json;
        nvs_handle_t nvs;
        if (nvs_open(MULTI_NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
            size_t len = 0;
            if (nvs_get_str(nvs, MULTI_NVS_KEY_CONFIG, nullptr, &len) == ESP_OK && len > 1) {
                json.resize(len);
                if (nvs_get_str(nvs, MULTI_NVS_KEY_CONFIG, json.data(), &len) == ESP_OK) {
                    json.resize(len - 1);
                } else {
                    json.clear();
                }
            }
            nvs_close(nvs);
        }

        auto result = multi::ParseDeviceConfig(json.c_str(), octal_psram);
        cfg_ = result.cfg;
        if (!result.found) {
            status_ = "Chưa có cấu hình: dùng chân âm thanh mặc định, không có màn hình.";
        }
        for (auto& w : result.warnings) {
            ESP_LOGW(TAG, "%s", w.c_str());
            status_ += (w.rfind("ℹ", 0) == 0 ? "\n" : "\n⚠ ") + w;
        }
    }

    void SaveStatus() {
        nvs_handle_t nvs;
        if (nvs_open(MULTI_NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_str(nvs, MULTI_NVS_KEY_STATUS, status_.c_str());
            nvs_commit(nvs);
            nvs_close(nvs);
        }
    }

    void StartStableTimer() {
        esp_timer_create_args_t args = {};
        args.callback = [](void*) {
            s_crash_count = 0;
            ESP_LOGI(TAG, "Running stable, crash counter cleared");
        };
        args.name = "multi_stable";
        if (esp_timer_create(&args, &stable_timer_) == ESP_OK) {
            esp_timer_start_once(stable_timer_, (uint64_t)SAFE_MODE_STABLE_SEC * 1000000ULL);
        }
    }

    // ---------- giao dien SANG mac dinh sau moi lan nap firmware ----------
    // Lua chon sang/toi luu trong NVS (display/theme) khong bi xoa khi nap lai, nen moi khi
    // phat hien firmware moi (ma ELF khac) thi dat lai "light". Nguoi dung doi sang "dark" sau do
    // van duoc giu cho toi lan nap firmware tiep theo.
    void ResetThemeOnNewFirmware() {
        const esp_app_desc_t* app = esp_app_get_description();
        char sha[17] = {};
        for (int i = 0; i < 8; i++) {
            snprintf(sha + i * 2, 3, "%02x", app->app_elf_sha256[i]);
        }
        Settings multi_settings(MULTI_NVS_NAMESPACE, true);
        if (multi_settings.GetString(MULTI_NVS_KEY_FW) != sha) {
            Settings display_settings("display", true);
            display_settings.SetString("theme", "light");
            multi_settings.SetString(MULTI_NVS_KEY_FW, sha);
            ESP_LOGI(TAG, "Firmware moi (%s) -> giao dien SANG", sha);
        }
    }

    // ---------- man hinh ----------
    void InitializeDisplay() {
        ResetThemeOnNewFirmware();
        bool ok = false;
        if (cfg_.display == DisplayType::Lcd) {
            ok = InitializeLcd();
        } else if (cfg_.display == DisplayType::OledSsd1306 || cfg_.display == DisplayType::OledSh1106) {
            ok = InitializeOled();
        }
        if (!ok) {
            if (cfg_.display != DisplayType::None) {
                status_ += "\n⚠ Khởi tạo màn hình thất bại, kiểm tra chân và loại driver";
            }
            display_ = new WithAuxHook<NoDisplay>();
            // Khong co man hinh van phai khoi tao LVGL: font/emoji trong assets
            // duoc tao bang lv_malloc sau khi vao mang (neu khong se crash LoadProhibited)
            if (!lv_is_initialized()) {
                lv_init();
            }
        }
    }

    bool InitializeLcd() {
        const int max_side = std::max(cfg_.width, cfg_.height);
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = (gpio_num_t)cfg_.mosi;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = (gpio_num_t)cfg_.sclk;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = max_side * 40 * 3;
        if (spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) {
            ESP_LOGE(TAG, "SPI bus init failed");
            return false;
        }

        int mhz = cfg_.spi_mhz;
        if (mhz <= 0) {
            mhz = cfg_.driver == LcdDriver::Ili9488 ? DEFAULT_ILI9488_SPI_MHZ : DEFAULT_LCD_SPI_MHZ;
        }
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = (gpio_num_t)cfg_.cs;
        io_config.dc_gpio_num = (gpio_num_t)cfg_.dc;
        io_config.spi_mode = cfg_.spi_mode;
        io_config.pclk_hz = mhz * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        if (esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io_) != ESP_OK) {
            ESP_LOGE(TAG, "Panel IO init failed");
            return false;
        }

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = (gpio_num_t)cfg_.rst;
        panel_config.rgb_ele_order = cfg_.bgr ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;

        // vendor config phai con song den khi esp_lcd_panel_init() chay xong
        ili9341_vendor_config_t ili_vendor = {};
        gc9a01_vendor_config_t gc_vendor = {};

        esp_err_t err = ESP_FAIL;
        switch (cfg_.driver) {
            case LcdDriver::Ili9341:
                err = esp_lcd_new_panel_ili9341(panel_io_, &panel_config, &panel_);
                break;
            case LcdDriver::St7735:
                ili_vendor.init_cmds = st7735_init_cmds;
                ili_vendor.init_cmds_size = sizeof(st7735_init_cmds) / sizeof(st7735_init_cmds[0]);
                panel_config.vendor_config = &ili_vendor;
                err = esp_lcd_new_panel_ili9341(panel_io_, &panel_config, &panel_);
                break;
            case LcdDriver::Ili9486:
                ili_vendor.init_cmds = ili9486_init_cmds;
                ili_vendor.init_cmds_size = sizeof(ili9486_init_cmds) / sizeof(ili9486_init_cmds[0]);
                panel_config.vendor_config = &ili_vendor;
                err = esp_lcd_new_panel_ili9341(panel_io_, &panel_config, &panel_);
                break;
            case LcdDriver::Ili9163:
                ili_vendor.init_cmds = ili9163_init_cmds;
                ili_vendor.init_cmds_size = sizeof(ili9163_init_cmds) / sizeof(ili9163_init_cmds[0]);
                panel_config.vendor_config = &ili_vendor;
                err = esp_lcd_new_panel_ili9341(panel_io_, &panel_config, &panel_);
                break;
            case LcdDriver::Gc9a01:
                err = esp_lcd_new_panel_gc9a01(panel_io_, &panel_config, &panel_);
                break;
            case LcdDriver::Gc9107:
                gc_vendor.init_cmds = gc9107_init_cmds;
                gc_vendor.init_cmds_size = sizeof(gc9107_init_cmds) / sizeof(gc9107_init_cmds[0]);
                panel_config.vendor_config = &gc_vendor;
                err = esp_lcd_new_panel_gc9a01(panel_io_, &panel_config, &panel_);
                break;
            case LcdDriver::Nv3023:
                err = esp_lcd_new_panel_nv3023(panel_io_, &panel_config, &panel_);
                break;
            case LcdDriver::St7796:
                err = esp_lcd_new_panel_st7796(panel_io_, &panel_config, &panel_);
                break;
            case LcdDriver::Ili9488:
                panel_config.bits_per_pixel = 18;  // SPI chi ho tro 18-bit
                s_ili9488_buffer_pixels = max_side * 20;
                err = esp_lcd_new_panel_ili9488(panel_io_, &panel_config, s_ili9488_buffer_pixels, &panel_);
                if (err == ESP_OK) {
                    s_ili9488_draw_bitmap = panel_->draw_bitmap;
                    panel_->draw_bitmap = Ili9488DrawBitmapSwapped;
                }
                break;
            case LcdDriver::St7789:
            default:
                err = esp_lcd_new_panel_st7789(panel_io_, &panel_config, &panel_);
                break;
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LCD driver init failed: %s", esp_err_to_name(err));
            return false;
        }

        if (esp_lcd_panel_reset(panel_) != ESP_OK || esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "LCD panel reset/init failed");
            return false;
        }
        esp_lcd_panel_invert_color(panel_, cfg_.invert_color);
        esp_lcd_panel_swap_xy(panel_, cfg_.swap_xy);
        esp_lcd_panel_mirror(panel_, cfg_.mirror_x, cfg_.mirror_y);

        if (cfg_.bl != multi::NC) {
            backlight_ = std::make_unique<PwmBacklight>((gpio_num_t)cfg_.bl, cfg_.backlight_invert);
        }

        // Man tron GC9A01: le 15% de logo nam gon trong vong tron
        int margin = cfg_.driver == LcdDriver::Gc9a01 ? 15 : 6;
        auto lcd = new WithSplash<WithAuxHook<SpiLcdDisplay>>(margin, panel_io_, panel_, cfg_.width, cfg_.height,
                                                 cfg_.offset_x, cfg_.offset_y,
                                                 cfg_.mirror_x, cfg_.mirror_y, cfg_.swap_xy);
        hide_splash_ = [lcd]() { lcd->HideSplash(); };
        display_ = lcd;
        if (cfg_.wallpaper && !ApplyRbWallpapers(cfg_.width, cfg_.height)) {
            status_ += "\n⚠ Không đủ PSRAM cho hình nền, chạy không hình nền";
        }
        return true;
    }

    bool InitializeOled() {
        i2c_master_bus_config_t bus_config = {};
        bus_config.i2c_port = (i2c_port_t)0;
        bus_config.sda_io_num = (gpio_num_t)cfg_.oled_sda;
        bus_config.scl_io_num = (gpio_num_t)cfg_.oled_scl;
        bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_config.glitch_ignore_cnt = 7;
        bus_config.flags.enable_internal_pullup = 1;
        if (i2c_new_master_bus(&bus_config, &i2c_bus_) != ESP_OK) {
            ESP_LOGE(TAG, "I2C bus init failed");
            return false;
        }

        esp_lcd_panel_io_i2c_config_t io_config = {};
        io_config.dev_addr = OLED_I2C_ADDRESS;
        io_config.scl_speed_hz = 400 * 1000;
        io_config.control_phase_bytes = 1;
        io_config.dc_bit_offset = 6;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        if (esp_lcd_new_panel_io_i2c(i2c_bus_, &io_config, &panel_io_) != ESP_OK) {
            ESP_LOGE(TAG, "OLED IO init failed");
            return false;
        }

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;
        esp_lcd_panel_ssd1306_config_t ssd1306_config = {};
        ssd1306_config.height = static_cast<uint8_t>(cfg_.height);
        panel_config.vendor_config = &ssd1306_config;

        esp_err_t err = cfg_.display == DisplayType::OledSh1106
                            ? esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_)
                            : esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OLED driver init failed");
            return false;
        }
        if (esp_lcd_panel_reset(panel_) != ESP_OK || esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "OLED not responding (check SDA/SCL, address 0x3C)");
            return false;
        }
        esp_lcd_panel_invert_color(panel_, false);
        esp_lcd_panel_disp_on_off(panel_, true);

        auto oled = new WithSplash<WithAuxHook<MultiOledDisplay>>(4, panel_io_, panel_, cfg_.width, cfg_.height,
                                                     cfg_.mirror_x, cfg_.mirror_y);
        hide_splash_ = [oled]() { oled->HideSplash(); };
        display_ = oled;
        return true;
    }

    // ---------- ES8311 ----------
    void InitializeCodecI2c() {
        if (!cfg_.es8311) {
            return;
        }
        // OLED cung SDA/SCL -> dung chung bus cua OLED
        if (multi::OledSharesCodecBus(cfg_) && i2c_bus_ != nullptr) {
            codec_i2c_bus_ = i2c_bus_;
            codec_i2c_port_ = I2C_NUM_0;
            return;
        }
        i2c_master_bus_config_t bus_config = {};
        bus_config.i2c_port = I2C_NUM_1;
        bus_config.sda_io_num = (gpio_num_t)cfg_.codec_sda;
        bus_config.scl_io_num = (gpio_num_t)cfg_.codec_scl;
        bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_config.glitch_ignore_cnt = 7;
        bus_config.flags.enable_internal_pullup = 1;
        if (i2c_new_master_bus(&bus_config, &codec_i2c_bus_) != ESP_OK) {
            ESP_LOGE(TAG, "Codec I2C bus init failed -> dung I2S Simplex mac dinh");
            status_ += "\n⚠ Không khởi tạo được I2C cho ES8311, dùng âm thanh I2S mặc định";
            DeviceConfig def;
            cfg_.es8311 = false;
            cfg_.duplex = false;
            cfg_.mic_ws = def.mic_ws; cfg_.mic_sck = def.mic_sck; cfg_.mic_din = def.mic_din;
            cfg_.spk_dout = def.spk_dout; cfg_.spk_bclk = def.spk_bclk; cfg_.spk_lrck = def.spk_lrck;
            codec_i2c_bus_ = nullptr;
        }
    }

    // ---------- nut ----------
    void InitializeButtons() {
        if (cfg_.boot_button != multi::NC) {
            boot_button_ = std::make_unique<Button>((gpio_num_t)cfg_.boot_button);
            boot_button_->OnClick([this]() {
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateStarting) {
                    EnterWifiConfigMode();
                    return;
                }
                app.ToggleChatState();
            });
        }
        if (cfg_.vol_up != multi::NC) {
            volume_up_button_ = std::make_unique<Button>((gpio_num_t)cfg_.vol_up);
            volume_up_button_->OnClick([this]() { ChangeVolume(+10); });
            volume_up_button_->OnLongPress([this]() {
                GetAudioCodec()->SetOutputVolume(100);
                GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
            });
        }
        if (cfg_.vol_down != multi::NC) {
            volume_down_button_ = std::make_unique<Button>((gpio_num_t)cfg_.vol_down);
            volume_down_button_->OnClick([this]() { ChangeVolume(-10); });
            volume_down_button_->OnLongPress([this]() {
                GetAudioCodec()->SetOutputVolume(0);
                GetDisplay()->ShowNotification(Lang::Strings::MUTED);
            });
        }
    }

    void ChangeVolume(int delta) {
        auto codec = GetAudioCodec();
        int volume = std::clamp(codec->output_volume() + delta, 0, 100);
        codec->SetOutputVolume(volume);
        GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
    }

    // ---------- ngoai vi ----------
    void InitializePeripherals() {
        if (cfg_.led != multi::NC) {
            if (cfg_.led_count > 1) {
                led_ = std::make_unique<CircularStrip>((gpio_num_t)cfg_.led, (uint16_t)cfg_.led_count);
            } else {
                led_ = std::make_unique<SingleLed>((gpio_num_t)cfg_.led);
            }
        }
        if (cfg_.ext_lamp != multi::NC) {
            lamp_ = std::make_unique<LampController>((gpio_num_t)cfg_.ext_lamp);
        }
        if (cfg_.robot) {
            RbRobotPins rp;
            rp.motor.type = (MotorDriverType)cfg_.motor_driver;
            rp.motor.l1 = cfg_.motor_l1; rp.motor.l2 = cfg_.motor_l2;
            rp.motor.r1 = cfg_.motor_r1; rp.motor.r2 = cfg_.motor_r2;
            rp.motor.pwm_l = cfg_.motor_pwm_l; rp.motor.pwm_r = cfg_.motor_pwm_r;
            rp.motor.stby = cfg_.motor_stby;
            rp.motor.invert_l = cfg_.motor_invert_l; rp.motor.invert_r = cfg_.motor_invert_r;
            rp.motor.swap_lr = cfg_.motor_swap_lr;
            rp.motor.speed = cfg_.motor_speed;
            rp.trig = cfg_.hc04_trig; rp.echo = cfg_.hc04_echo;
            rp.led1 = cfg_.robot_led1; rp.led2 = cfg_.robot_led2;
            rp.obstacle_cm = cfg_.obstacle_cm;
            rp.spin_ms = cfg_.spin_360_ms;
            rp.idle_move = cfg_.robot_idle_move;
            robot_ = std::make_unique<RbRobot>(rp);
        }
        if (cfg_.charge_pin != multi::NC) {
            gpio_config_t io = {};
            io.pin_bit_mask = 1ULL << cfg_.charge_pin;
            io.mode = GPIO_MODE_INPUT;
            // TP4056/BQ24: CHRG la open-drain keo LOW khi sac -> can pull-up
            io.pull_up_en = cfg_.charge_active_high ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE;
            io.pull_down_en = GPIO_PULLDOWN_DISABLE;
            gpio_config(&io);
        }
        if (cfg_.battery_adc != multi::NC) {
            adc_unit_t unit;
            adc_channel_t channel;
            if (adc_oneshot_io_to_channel(cfg_.battery_adc, &unit, &channel) == ESP_OK) {
                battery_ = std::make_unique<AdcBatteryMonitor>(unit, channel, BATTERY_DIVIDER_UPPER,
                                                               BATTERY_DIVIDER_LOWER, GPIO_NUM_NC);
            } else {
                status_ += "\n⚠ Báo pin: GPIO" + std::to_string(cfg_.battery_adc) + " không có ADC";
            }
        }
        // Do line tao SAU bao pin (cung dung ADC oneshot)
        if (robot_ && cfg_.func_line) {
            LineConfig lc;
            for (int i = 0; i < 6; i++) lc.pins[i] = cfg_.line_pins[i];
            lc.params.kp = cfg_.line_kp;
            lc.params.ki = cfg_.line_ki;
            lc.params.kd = cfg_.line_kd;
            lc.params.base_speed = cfg_.line_speed;
            lc.params.run_duty = cfg_.line_max_speed;
            lc.params.recovery_duty = cfg_.line_recovery;
            lc.params.mix_reverse = cfg_.line_mix_reverse;
            lc.params.white_line = cfg_.line_white;
            line_ = std::make_unique<LineFollower>(robot_->Motor(), robot_.get(), lc);
        }
        // App Dabble qua AT-09: START = tranh vat can (hoac do line neu chi bat do line)
        if (robot_ && cfg_.func_bt) {
            bool start_is_line = cfg_.func_line && !cfg_.func_avoid;
            gamepad_ = std::make_unique<At09Gamepad>(cfg_.at09_tx, cfg_.at09_rx, cfg_.at09_baud, robot_.get(),
                                                     line_.get(), start_is_line);
        }
    }

    // ---------- the nho + nhac MP3 ----------
    void InitializeSdCard() {
        if (!cfg_.HasSd()) {
            return;
        }
        sd_mounted_ = MountSdCard(cfg_.sd_miso, cfg_.sd_clk, cfg_.sd_mosi, cfg_.sd_cs, SPI2_HOST);
        if (!sd_mounted_) {
            status_ += "\n⚠ Không đọc được thẻ nhớ (thẻ phải FAT32, kiểm tra chân MISO/CLK/MOSI/CS)";
            return;
        }
        music_ = std::make_unique<SdMusicPlayer>(SD_MOUNT_POINT, [this](bool playing) {
            if (oled_aux_) oled_aux_->SetMusic(playing);
        });
    }

    // ---------- man OLED phu ----------
    void InitializeOledAux() {
        if (!cfg_.HasOled2()) {
            return;
        }
        i2c_master_bus_handle_t bus = nullptr;
        if (multi::Oled2SharesCodecBus(cfg_) && codec_i2c_bus_ != nullptr) {
            bus = codec_i2c_bus_;  // chung bus voi ES8311
        } else {
            // chon bus I2C con trong: OLED chinh dung I2C_NUM_0, ES8311 dung codec_i2c_port_
            bool used[2] = {i2c_bus_ != nullptr, false};
            if (codec_i2c_bus_ != nullptr) used[codec_i2c_port_ == I2C_NUM_0 ? 0 : 1] = true;
            int port = !used[0] ? 0 : (!used[1] ? 1 : -1);
            if (port >= 0) bus = OledAux::NewBus(port, cfg_.oled2_sda, cfg_.oled2_scl);
        }
        oled_aux_ = OledAux::Create(bus, cfg_.oled2_height, cfg_.oled2_flip, cfg_.oled2_sh1106);
        if (oled_aux_ == nullptr) {
            status_ += "\n⚠ Không thấy OLED phụ (SDA GPIO" + std::to_string(cfg_.oled2_sda) + ", SCL GPIO" +
                       std::to_string(cfg_.oled2_scl) + ", địa chỉ 0x3C)";
            return;
        }
        if (robot_ && robot_->HasDistanceSensor()) {
            robot_->StartDistancePoll();
        }
        OledAux::Sources src;
        if (battery_) {
            src.battery = [this](int& level, bool& charging) {
                bool discharging = false;
                return GetBatteryLevel(level, charging, discharging);
            };
        }
        src.distance_cm = [this]() {
            return robot_ && robot_->HasDistanceSensor() ? robot_->LastDistanceCm() : -2;
        };
        oled_aux_->SetSources(src);
    }

    bool ReadCharging() {
        if (cfg_.charge_pin == multi::NC) {
            return battery_ ? battery_->IsCharging() : false;
        }
        int level = gpio_get_level((gpio_num_t)cfg_.charge_pin);
        return cfg_.charge_active_high ? level == 1 : level == 0;
    }

    std::string Describe() {
        std::string s;
        switch (cfg_.display) {
            case DisplayType::Lcd:
                s = std::string("Màn LCD ") + multi::LcdDriverName(cfg_.driver) + " " +
                    std::to_string(cfg_.width) + "x" + std::to_string(cfg_.height);
                break;
            case DisplayType::OledSsd1306: s = "Màn OLED SSD1306 128x" + std::to_string(cfg_.height); break;
            case DisplayType::OledSh1106:  s = "Màn OLED SH1106 128x" + std::to_string(cfg_.height); break;
            default: s = "Không màn hình"; break;
        }
        s += cfg_.es8311 ? ", âm thanh ES8311" : (cfg_.duplex ? ", âm thanh Duplex" : ", âm thanh Simplex");
        if (cfg_.robot) {
            s += std::string(", robot (driver ") + MotorDriverName((MotorDriverType)cfg_.motor_driver) + ")";
            if (cfg_.func_line) s += ", dò line PID " + std::to_string(cfg_.LineCount()) + " cảm biến";
            if (cfg_.func_bt) s += ", Bluetooth AT-09";
            if (cfg_.func_avoid && cfg_.hc04_trig != multi::NC) s += ", tránh vật cản";
        }
        if (sd_mounted_) s += ", thẻ nhớ";
        if (oled_aux_) s += ", OLED phụ";
        return s;
    }

public:
    MultiWifiBoard() {
        safe_mode_ = UpdateCrashCounter();
        LoadConfig();

        if (safe_mode_) {
            ESP_LOGW(TAG, "SAFE MODE: board crashed %lu times in a row", (unsigned long)s_crash_count);
            DeviceConfig def;           // giu cau hinh trong NVS de nguoi dung sua
            def.boot_button = 0;
            cfg_ = def;                 // chi dung chan am thanh mac dinh + nut BOOT
            status_ = "CHẾ ĐỘ AN TOÀN: board khởi động lỗi " + std::to_string(s_crash_count) +
                      " lần liên tiếp với cấu hình đang lưu. Đã tắt màn hình và ngoại vi, "
                      "dùng chân âm thanh mặc định. Hãy sửa cấu hình rồi bấm Khởi động lại.";
        }

        InitializeDisplay();
        InitializeCodecI2c();
        InitializeButtons();
        if (!safe_mode_) {
            InitializePeripherals();
            InitializeOledAux();
            InitializeSdCard();
        }

        if (!safe_mode_) {
            std::string head = "Đang chạy: " + Describe() + ".";
            status_ = status_.empty() ? head : head + "\n" + status_;
        }
        SaveStatus();
        StartStableTimer();

        if (backlight_) {
            backlight_->RestoreBrightness();
        }
    }

    ~MultiWifiBoard() {
        if (stable_timer_) {
            esp_timer_stop(stable_timer_);
            esp_timer_delete(stable_timer_);
        }
    }

    void StartNetwork() override {
        if (!safe_mode_) {
            WifiBoard::StartNetwork();
            return;
        }
        // Che do an toan: vao thang trang cau hinh 192.168.4.1, KHONG ket noi Wi-Fi nha truoc
        // (dung station ngay sau khi vua bat se bi treo).
        auto& wifi_manager = WifiManager::GetInstance();
        WifiManagerConfig config;
        config.ssid_prefix = "LINE-AI-RB";
        config.language = Lang::CODE;
        config.show_ota_config = true;
        config.show_sleep_config = true;
        uint8_t mac[6];
        if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
            char hostname[32];
            snprintf(hostname, sizeof(hostname), "%s-%02X%02X", config.ssid_prefix.c_str(), mac[4], mac[5]);
            config.station_hostname = hostname;
        }
        wifi_manager.Initialize(config);
        wifi_manager.SetEventCallback([this](WifiEvent event, const std::string& data) {
            switch (event) {
                case WifiEvent::Scanning:        OnNetworkEvent(NetworkEvent::Scanning); break;
                case WifiEvent::Connecting:      OnNetworkEvent(NetworkEvent::Connecting, data); break;
                case WifiEvent::Connected:       OnNetworkEvent(NetworkEvent::Connected, data); break;
                case WifiEvent::Disconnected:    OnNetworkEvent(NetworkEvent::Disconnected); break;
                case WifiEvent::ConfigModeEnter: OnNetworkEvent(NetworkEvent::WifiConfigModeEnter); break;
                case WifiEvent::ConfigModeExit:  OnNetworkEvent(NetworkEvent::WifiConfigModeExit); break;
            }
        });
        vTaskDelay(pdMS_TO_TICKS(1500));
        StartWifiConfigMode();
    }

    void SetNetworkEventCallback(NetworkEventCallback callback) override {
        WifiBoard::SetNetworkEventCallback([this, callback](NetworkEvent event, const std::string& data) {
            if (hide_splash_ &&
                (event == NetworkEvent::Connected || event == NetworkEvent::WifiConfigModeEnter)) {
                hide_splash_();
                hide_splash_ = nullptr;
            }
            if (callback) {
                callback(event, data);
            }
        });
    }

    Led* GetLed() override {
        return led_ ? led_.get() : Board::GetLed();
    }

    AudioCodec* GetAudioCodec() override {
        if (!audio_codec_) {
            if (cfg_.es8311 && codec_i2c_bus_ != nullptr) {
                audio_codec_ = std::make_unique<Es8311AudioCodec>(
                    codec_i2c_bus_, codec_i2c_port_, ES8311_SAMPLE_RATE, ES8311_SAMPLE_RATE,
                    (gpio_num_t)cfg_.mclk, (gpio_num_t)cfg_.spk_bclk, (gpio_num_t)cfg_.spk_lrck,
                    (gpio_num_t)cfg_.spk_dout, (gpio_num_t)cfg_.mic_din, (gpio_num_t)cfg_.pa,
                    ES8311_CODEC_DEFAULT_ADDR, cfg_.mclk != multi::NC, cfg_.pa_inverted);
            } else if (cfg_.duplex) {
                audio_codec_ = std::make_unique<NoAudioCodecDuplex>(
                    AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                    (gpio_num_t)cfg_.spk_bclk, (gpio_num_t)cfg_.spk_lrck,
                    (gpio_num_t)cfg_.spk_dout, (gpio_num_t)cfg_.mic_din);
            } else {
                audio_codec_ = std::make_unique<NoAudioCodecSimplex>(
                    AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                    (gpio_num_t)cfg_.spk_bclk, (gpio_num_t)cfg_.spk_lrck, (gpio_num_t)cfg_.spk_dout,
                    (gpio_num_t)cfg_.mic_sck, (gpio_num_t)cfg_.mic_ws, (gpio_num_t)cfg_.mic_din);
            }
        }
        return audio_codec_.get();
    }

    Display* GetDisplay() override {
        return display_;
    }

    Backlight* GetBacklight() override {
        return backlight_.get();
    }

    bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        if (!battery_) {
            return false;
        }
        charging = ReadCharging();
        discharging = !charging;
        level = battery_->GetBatteryLevel();
        return true;
    }
};

DECLARE_BOARD(MultiWifiBoard);

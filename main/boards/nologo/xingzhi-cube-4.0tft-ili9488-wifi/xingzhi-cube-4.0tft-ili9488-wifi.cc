#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "power_save_timer.h"
#include "led/single_led.h"
#include "assets/lang_config.h"
#include "power_manager.h"
#include "lvgl_theme.h"
#include "lvgl_image.h"
#include "display.h"
#include <memory>
#include <functional>

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_interface.h>
#include <esp_lcd_ili9488.h>


#include <driver/rtc_io.h>
#include <esp_sleep.h>

#define TAG "XINGZHI_CUBE_4_0TFT_ILI9488_WIFI"

LV_IMAGE_DECLARE(bg_light);
LV_IMAGE_DECLARE(bg_dark);
LV_IMAGE_DECLARE(boot_logo);

class LvglStaticImage : public LvglImage {
public:
    explicit LvglStaticImage(const lv_img_dsc_t* dsc) : dsc_(dsc) {}
    const lv_img_dsc_t* image_dsc() const override { return dsc_; }
private:
    const lv_img_dsc_t* dsc_;
};


class SplashLcdDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;

    void SetupUI() override {
        SpiLcdDisplay::SetupUI();
        ShowBootLogo();
    }

    void ShowBootLogo() {
        DisplayLockGuard lock(this);
        if (splash_ != nullptr) {
            return;
        }
        splash_ = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(splash_);
        lv_obj_set_size(splash_, LV_HOR_RES, LV_VER_RES);
        lv_obj_set_pos(splash_, 0, 0);
        lv_obj_set_style_bg_color(splash_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(splash_, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(splash_, 0, 0);
        lv_obj_set_style_border_width(splash_, 0, 0);
        lv_obj_set_style_radius(splash_, 0, 0);
        lv_obj_clear_flag(splash_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_move_foreground(splash_);

        lv_obj_t* img = lv_image_create(splash_);
        lv_image_set_src(img, &boot_logo);
        lv_obj_center(img);
    }

    void HideBootLogo() {
        DisplayLockGuard lock(this);
        if (splash_ == nullptr) {
            return;
        }
        lv_obj_del(splash_);
        splash_ = nullptr;
    }

private:
    lv_obj_t* splash_ = nullptr;
};

// ILI9488 qua SPI chi ho tro 18-bit (RGB666). Driver atanisoft/esp_lcd_ili9488
// tu chuyen RGB565 -> RGB666 trong draw_bitmap, nhung doc pixel theo thu tu
// byte little-endian. SpiLcdDisplay cau hinh LVGL voi swap_bytes = 1 (big-endian),
// nen o day dao byte lai truoc khi dua vao driver ILI9488.
#define ILI9488_COLOR_BUFFER_PIXELS (DISPLAY_WIDTH * 20)

static esp_err_t (*s_ili9488_draw_bitmap)(esp_lcd_panel_t*, int, int, int, int, const void*) = nullptr;

static esp_err_t Ili9488DrawBitmapSwapped(esp_lcd_panel_t* panel, int x_start, int y_start,
                                          int x_end, int y_end, const void* color_data) {
    const int w = x_end - x_start;
    const int h = y_end - y_start;
    uint16_t* px = (uint16_t*)color_data;  // buffer cua LVGL, sua tai cho la an toan
    const size_t count = (size_t)w * (size_t)h;
    for (size_t i = 0; i < count; i++) {
        px[i] = (uint16_t)((px[i] << 8) | (px[i] >> 8));
    }
    // Driver khong kiem tra kich thuoc bo dem chuyen mau -> chia theo dai hang cho an toan
    int rows_per_chunk = ILI9488_COLOR_BUFFER_PIXELS / w;
    if (rows_per_chunk < 1) {
        rows_per_chunk = 1;
    }
    for (int y = y_start; y < y_end; y += rows_per_chunk) {
        int y2 = y + rows_per_chunk;
        if (y2 > y_end) {
            y2 = y_end;
        }
        esp_err_t err = s_ili9488_draw_bitmap(panel, x_start, y, x_end, y2,
                                              px + (size_t)(y - y_start) * w);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

class XINGZHI_CUBE_4_0TFT_ILI9488_WIFI : public WifiBoard {
private:
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    SplashLcdDisplay* display_;
    PowerSaveTimer* power_save_timer_;
    PowerManager* power_manager_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    void InitializePowerManager() {
        power_manager_ = new PowerManager(GPIO_NUM_38);
        power_manager_->OnChargingStatusChanged([this](bool is_charging) {
            if (is_charging) {
                power_save_timer_->SetEnabled(false);
            } else {
                power_save_timer_->SetEnabled(true);
            }
        });
    }

    void InitializePowerSaveTimer() {
        rtc_gpio_init(GPIO_NUM_21);
        rtc_gpio_set_direction(GPIO_NUM_21, RTC_GPIO_MODE_OUTPUT_ONLY);
        rtc_gpio_set_level(GPIO_NUM_21, 1);

        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(1);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->OnShutdownRequest([this]() {
            ESP_LOGI(TAG, "Shutting down");
            rtc_gpio_set_level(GPIO_NUM_21, 0);
            rtc_gpio_hold_en(GPIO_NUM_21);
            esp_lcd_panel_disp_on_off(panel_, false);
            esp_deep_sleep_start();
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SDA;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SCL;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        volume_up_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    void ApplyRobot5320Wallpapers() {
        auto& mgr = LvglThemeManager::GetInstance();
        if (auto light = mgr.GetTheme("light")) {
            light->set_background_image(std::make_shared<LvglStaticImage>(&bg_light));
        }
        if (auto dark = mgr.GetTheme("dark")) {
            dark->set_background_image(std::make_shared<LvglStaticImage>(&bg_dark));
        }
    }

    void InitializeIli9488Display() {
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS;
        io_config.dc_gpio_num = DISPLAY_DC;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = DISPLAY_SPI_CLOCK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io_));

        ESP_LOGD(TAG, "Install ILI9488 LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RES;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 18;  // SPI bat buoc 18-bit
        // Bo dem chuyen mau (so pixel) >= bo dem LVGL cua SpiLcdDisplay (width * 20)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9488(panel_io_, &panel_config,
                                                  ILI9488_COLOR_BUFFER_PIXELS, &panel_));

        s_ili9488_draw_bitmap = panel_->draw_bitmap;
        panel_->draw_bitmap = Ili9488DrawBitmapSwapped;

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, DISPLAY_INVERT_COLOR));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));

        display_ = new SplashLcdDisplay(panel_io_, panel_,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                    DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                    DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        ApplyRobot5320Wallpapers();
    }

public:
    XINGZHI_CUBE_4_0TFT_ILI9488_WIFI() :
        boot_button_(BOOT_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializePowerManager();
        InitializePowerSaveTimer();
        InitializeSpi();
        InitializeButtons();
        InitializeIli9488Display();
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = power_manager_->IsCharging();
        discharging = power_manager_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }
        level = power_manager_->GetBatteryLevel();
        return true;
    }

    virtual void SetNetworkEventCallback(NetworkEventCallback callback) override {
        WifiBoard::SetNetworkEventCallback([this, callback](NetworkEvent event, const std::string& data) {
            if (display_ != nullptr &&
                (event == NetworkEvent::Connected ||
                 event == NetworkEvent::WifiConfigModeEnter)) {
                display_->HideBootLogo();
            }
            if (callback) {
                callback(event, data);
            }
        });
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }
};

DECLARE_BOARD(XINGZHI_CUBE_4_0TFT_ILI9488_WIFI);
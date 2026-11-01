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
#include "mcp_server.h"
#include "device_state.h"
#include "display.h"
#include "../bread-compact-wifi-multi/rb_robot.h"
#include "../bread-compact-wifi-multi/oled_aux.h"
#include "../bread-compact-wifi-multi/sd_card.h"
#include "../bread-compact-wifi-multi/sd_music.h"
// Bien the "RB AT09 + HC04": config.h cua board do dinh nghia AT09_PIN_TX / AT09_PIN_RX
#if defined(AT09_PIN_TX) && defined(AT09_PIN_RX)
#include "../bread-compact-wifi-multi/dabble_at09.h"
#define RB_HAS_AT09 1
#endif

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp_random.h>
#include <esp_rom_sys.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <string>

#define TAG "RB5320"

LV_IMAGE_DECLARE(boot_logo);

// Hien logo RB5320 khi khoi dong, an di khi da ket noi Wi-Fi / vao che do cau hinh
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

// Man chinh: logo khoi dong + chuyen cam xuc / chu sang man OLED phu
using Rb5320Display = WithAuxHook<SplashLcdDisplay>;

class RB5320 : public WifiBoard {
private:
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    Rb5320Display* display_ = nullptr;
    PowerSaveTimer* power_save_timer_ = nullptr;
    PowerManager* power_manager_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    RbRobot* robot_ = nullptr;
    OledAux* oled_aux_ = nullptr;
    SdMusicPlayer* music_ = nullptr;
#ifdef RB_HAS_AT09
    At09Gamepad* gamepad_ = nullptr;  // app Dabble qua Bluetooth AT-09
#endif

    void InitializePowerManager() {
        power_manager_ = new PowerManager(GPIO_NUM_38);
        power_manager_->OnChargingStatusChanged([this](bool is_charging) {
            if (is_charging) power_save_timer_->SetEnabled(false);
            else             power_save_timer_->SetEnabled(true);
        });
    }

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(1);
            if (robot_) robot_->Stop();
            if (oled_aux_) oled_aux_->SetSleep(true);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();
            if (oled_aux_) oled_aux_->SetSleep(false);
        });
        power_save_timer_->OnShutdownRequest([this]() {
            ESP_LOGI(TAG, "Shutting down");
            if (robot_) robot_->Stop();
            if (oled_aux_) oled_aux_->PowerOff();
            if (panel_) esp_lcd_panel_disp_on_off(panel_, false);
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
            int vol = codec->output_volume() + 10;
            if (vol > 100) vol = 100;
            codec->SetOutputVolume(vol);
            GetDisplay()->ShowNotification("Vol " + std::to_string(vol));
        });

        volume_down_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto codec = GetAudioCodec();
            int vol = codec->output_volume() - 10;
            if (vol < 0) vol = 0;
            codec->SetOutputVolume(vol);
            if (vol == 0) GetDisplay()->ShowNotification(Lang::Strings::MUTED);
            else          GetDisplay()->ShowNotification("Vol " + std::to_string(vol));
        });

        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    void InitializeSt7789Display() {
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS;
        io_config.dc_gpio_num = DISPLAY_DC;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io_));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io_, &panel_config, &panel_));

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, DISPLAY_INVERT_COLOR));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));

        display_ = new Rb5320Display(
            panel_io_, panel_,
            DISPLAY_WIDTH, DISPLAY_HEIGHT,
            DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY
        );
    }

    // Motor, HC-SR04, 2 LED, lenh giong noi (dung chung voi board Multi)
    void InitializeRobot() {
        RbRobotPins pins;
        pins.motor.type = MotorDriverType::Generic4;  // driver 4 chan, giu nguyen nhu cu
        pins.motor.l1 = MOTOR_LEFT_IN1;
        pins.motor.l2 = MOTOR_LEFT_IN2;
        pins.motor.r1 = MOTOR_RIGHT_IN3;
        pins.motor.r2 = MOTOR_RIGHT_IN4;
        pins.motor.invert_l = true;
        pins.motor.invert_r = false;
        pins.trig = HC04_TRIG_GPIO;
        pins.echo = HC04_ECHO_GPIO;
        pins.led1 = LED1_GPIO;
        pins.led2 = LED2_GPIO;
        pins.obstacle_cm = OBSTACLE_DISTANCE_CM;
        pins.spin_ms = SPIN_360_MS;
        pins.idle_move = true;
        robot_ = new RbRobot(pins);
    }

    // The nho + phat nhac MP3
    void InitializeSdCard() {
        if (!MountSdCard(SD_PIN_MISO, SD_PIN_CLK, SD_PIN_MOSI, SD_PIN_CS, SPI2_HOST)) {
            return;
        }
        music_ = new SdMusicPlayer(SD_MOUNT_POINT, [this](bool playing) {
            if (oled_aux_) oled_aux_->SetMusic(playing);
        });
    }

    // Man OLED phu: gio, Wi-Fi, pin, mat robot, khoang cach, chu AI
    void InitializeOledAux() {
        if (OLED2_PIN_SDA == GPIO_NUM_NC || OLED2_PIN_SCL == GPIO_NUM_NC) {
            return;  // khong gan OLED phu
        }
        auto bus = OledAux::NewBus(0, OLED2_PIN_SDA, OLED2_PIN_SCL);
        oled_aux_ = OledAux::Create(bus, OLED2_HEIGHT, OLED2_FLIP, false);
        if (oled_aux_ == nullptr) {
            return;
        }
        robot_->StartDistancePoll();
        OledAux::Sources src;
        src.battery = [this](int& level, bool& charging) {
            level = power_manager_->GetBatteryLevel();
            charging = power_manager_->IsCharging();
            return true;
        };
        src.distance_cm = [this]() { return robot_->HasDistanceSensor() ? robot_->LastDistanceCm() : -2; };
        oled_aux_->SetSources(src);
    }

public:
    RB5320() :
        boot_button_(BOOT_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {

        InitializePowerManager();
        InitializePowerSaveTimer();
        InitializeSpi();
        InitializeButtons();
        InitializeSt7789Display();
        InitializeRobot();
        InitializeOledAux();
        InitializeSdCard();
#ifdef RB_HAS_AT09
        // START = tranh vat can, SELECT = lai tay
        gamepad_ = new At09Gamepad(AT09_PIN_TX, AT09_PIN_RX, AT09_BAUD, robot_, nullptr, false);
#endif

        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

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
        if (level != PowerSaveLevel::LOW_POWER) power_save_timer_->WakeUp();
        WifiBoard::SetPowerSaveLevel(level);
    }
};

DECLARE_BOARD(RB5320);

// Robot Do Line PID: ST7789 2.8" + INMP441/MAX98357 + TB6612 + cam bien line 6 kenh.
// Code robot / driver / do line dung chung voi board Multi (bread-compact-wifi-multi/).
#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "settings.h"
#include "../bread-compact-wifi-multi/motor_driver.h"
#include "../bread-compact-wifi-multi/rb_robot.h"
#include "../bread-compact-wifi-multi/line_follower.h"
#include "../bread-compact-wifi-multi/multi_splash.h"
#include "../bread-compact-wifi-multi/multi_wallpaper.h"
// Bien the "RB Do Line PID + AT09": config.h dinh nghia AT09_PIN_TX / AT09_PIN_RX
#if defined(AT09_PIN_TX) && defined(AT09_PIN_RX)
#include "../bread-compact-wifi-multi/dabble_at09.h"
#define RB_HAS_AT09 1
#endif

#include <esp_app_desc.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <driver/spi_common.h>
#include <nvs.h>

#include <cJSON.h>

#include <string>

#define TAG "RobotLinePid"

class RobotLinePid : public WifiBoard {
private:
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    WithSplash<SpiLcdDisplay>* display_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    RbRobot* robot_ = nullptr;
    LineFollower* line_ = nullptr;
#ifdef RB_HAS_AT09
    At09Gamepad* gamepad_ = nullptr;  // app Dabble qua Bluetooth AT-09
#endif

    // Moi lan nap firmware moi -> giao dien SANG (giong board Multi)
    void ResetThemeOnNewFirmware() {
        const esp_app_desc_t* app = esp_app_get_description();
        char sha[17] = {};
        for (int i = 0; i < 8; i++) snprintf(sha + i * 2, 3, "%02x", app->app_elf_sha256[i]);
        Settings fw("line_pid", true);
        if (fw.GetString("fw") != sha) {
            Settings("display", true).SetString("theme", "light");
            fw.SetString("fw", sha);
        }
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

    void InitializeDisplay() {
        ResetThemeOnNewFirmware();
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
        panel_config.reset_gpio_num = DISPLAY_RES;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io_, &panel_config, &panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, DISPLAY_INVERT_COLOR));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));

        display_ = new WithSplash<SpiLcdDisplay>(6, panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                                 DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                                 DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        ApplyRbWallpapers(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    }

    void InitializeRobot() {
        RbRobotPins pins;
        pins.motor.type = MOTOR_DRIVER;
        pins.motor.l1 = MOTOR_LEFT_IN1;
        pins.motor.l2 = MOTOR_LEFT_IN2;
        pins.motor.r1 = MOTOR_RIGHT_IN1;
        pins.motor.r2 = MOTOR_RIGHT_IN2;
        pins.motor.pwm_l = MOTOR_LEFT_PWM;
        pins.motor.pwm_r = MOTOR_RIGHT_PWM;
        pins.motor.stby = MOTOR_STBY;
        pins.motor.invert_l = MOTOR_INVERT_LEFT;
        pins.motor.invert_r = MOTOR_INVERT_RIGHT;
        pins.motor.swap_lr = MOTOR_SWAP_LR;
        pins.motor.speed_run = true;
        pins.motor.speed = MOTOR_SPEED_PERCENT;
        pins.led1 = LED1_GPIO;
        pins.idle_move = false;  // robot do line: khong tu quay khi ranh
        robot_ = new RbRobot(pins);

        LineConfig lc;
        const int sensors[6] = {LINE_S0, LINE_S1, LINE_S2, LINE_S3, LINE_S4, LINE_S5};
        for (int i = 0; i < 6; i++) lc.pins[i] = sensors[i];
        lc.params.kp = LINE_KP;
        lc.params.ki = LINE_KI;
        lc.params.kd = LINE_KD;
        lc.params.base_speed = LINE_BASE_SPEED;
        lc.params.max_speed = LINE_MAX_SPEED;
        lc.params.run_duty = 140;
        lc.params.recovery_duty = 80;
        lc.params.white_line = LINE_WHITE_ON_BLACK;
        LoadLineDuty(lc.params);
        line_ = new LineFollower(robot_->Motor(), robot_, lc);
#ifdef RB_HAS_AT09
        // START = do line, SELECT = lai tay, TRON = hieu chinh cam bien
        gamepad_ = new At09Gamepad(AT09_PIN_TX, AT09_PIN_RX, AT09_BAUD, robot_, line_, true);
#endif
    }

    // Toc do thang / mat vach lay tu trang Wi-Fi (rb5320/device_json), neu da luu.
    static void LoadLineDuty(line::Params& p) {
        nvs_handle_t nvs;
        if (nvs_open("rb5320", NVS_READONLY, &nvs) != ESP_OK) return;
        size_t len = 0;
        if (nvs_get_str(nvs, "device_json", nullptr, &len) == ESP_OK && len > 1 && len < 8000) {
            std::string json(len, '\0');
            if (nvs_get_str(nvs, "device_json", json.data(), &len) == ESP_OK) {
                cJSON* root = cJSON_Parse(json.c_str());
                if (root) {
                    auto take = [](cJSON* item, int& dst) {
                        if (!cJSON_IsNumber(item)) return;
                        int v = item->valueint;
                        if (v >= 0 && v <= 255) dst = v;
                    };
                    take(cJSON_GetObjectItem(root, "line_max_speed"), p.run_duty);
                    take(cJSON_GetObjectItem(root, "line_recovery"), p.recovery_duty);
                    cJSON* rev = cJSON_GetObjectItem(root, "line_mix_reverse");
                    if (cJSON_IsBool(rev)) p.mix_reverse = cJSON_IsTrue(rev);
                    else if (cJSON_IsNumber(rev)) p.mix_reverse = rev->valueint != 0;
                    cJSON_Delete(root);
                }
            }
        }
        nvs_close(nvs);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
        volume_up_button_.OnClick([this]() { ChangeVolume(+10); });
        volume_down_button_.OnClick([this]() { ChangeVolume(-10); });
        // Giu lau Vol+: bat / tat do line
        volume_up_button_.OnLongPress([this]() {
            if (line_->IsRunning()) {
                line_->Stop();
                GetDisplay()->ShowNotification("Dung do line");
            } else {
                line_->Start();
                GetDisplay()->ShowNotification("Do line");
            }
        });
        // Giu lau Vol-: hieu chinh cam bien (dat robot tren vach)
        volume_down_button_.OnLongPress([this]() {
            GetDisplay()->ShowNotification("Hieu chinh cam bien...");
            xTaskCreate(
                [](void* arg) {
                    auto* self = static_cast<RobotLinePid*>(arg);
                    std::string r = self->line_->CalibrateNow();
                    Application::GetInstance().Schedule([self, r]() { self->GetDisplay()->ShowNotification(r); });
                    vTaskDelete(nullptr);
                },
                "line_cal", 4096, this, 4, nullptr);
        });
    }

    void ChangeVolume(int delta) {
        auto codec = GetAudioCodec();
        int vol = std::clamp(codec->output_volume() + delta, 0, 100);
        codec->SetOutputVolume(vol);
        if (vol == 0) GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        else GetDisplay()->ShowNotification("Vol " + std::to_string(vol));
    }

public:
    RobotLinePid()
        : boot_button_(BOOT_BUTTON_GPIO),
          volume_up_button_(VOLUME_UP_BUTTON_GPIO),
          volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializeSpi();
        InitializeDisplay();
        InitializeRobot();
        InitializeButtons();
        GetBacklight()->RestoreBrightness();
    }

    AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                               AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK,
                                               AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK,
                                               AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    Display* GetDisplay() override { return display_; }

    Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    void SetNetworkEventCallback(NetworkEventCallback callback) override {
        WifiBoard::SetNetworkEventCallback([this, callback](NetworkEvent event, const std::string& data) {
            if (display_ != nullptr &&
                (event == NetworkEvent::Connected || event == NetworkEvent::WifiConfigModeEnter)) {
                display_->HideSplash();
            }
            if (callback) callback(event, data);
        });
    }
};

DECLARE_BOARD(RobotLinePid);

// Chuc nang robot RB5320 (motor, HC-SR04, 2 LED, lenh giong noi) dung cho board Multi.
// Motor qua MotorDriver (motor_driver.h): 4 chan (RB5320), TB6612, L298N, L293D, DRV8833, L9110S,
// MX1508, BTS7960.
// Lay tu main/boards/rb5320/rb5320.cc, chan GPIO doc tu cau hinh 192.168.4.1.
#ifndef _MULTI_RB_ROBOT_H_
#define _MULTI_RB_ROBOT_H_

#include "application.h"
#include "mcp_server.h"
#include "device_state.h"
#include "motor_driver.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <atomic>
#include <functional>
#include <string>

struct RbRobotPins {
    MotorPins motor;                         // driver + chan motor
    int trig = -1, echo = -1;                // HC-SR04 (khong bat buoc)
    int led1 = -1, led2 = -1;                // LED thuong (khong bat buoc)
    int obstacle_cm = 25;                    // khoang cach ne vat can
    int spin_ms = 2200;                      // thoi gian quay 1 vong 360 do
    bool idle_move = true;                   // tu quay / lac khi ranh
};

class RbRobot {
public:
    explicit RbRobot(const RbRobotPins& pins) : pins_(pins) {
        hc04_mutex_ = xSemaphoreCreateMutex();
        motor_ = new MotorDriver(pins_.motor);
        InitializeLeds();
        InitializeHc04();
        InitializeTools();
        xTaskCreate(IdleSpinTask, "idle_spin", 4096, this, 3, &idle_spin_task_handle_);
        xTaskCreate(SpeakWatchTask, "speak_watch", 3072, this, 3, nullptr);
    }

    void Stop() {
        StopExternal();
        manual_ = false;
        external_ = false;
        StopAutoAvoid();
        MotorStop();
    }

    bool HasDistanceSensor() const { return HasHc04(); }
    bool HasPwm() const { return motor_->CanControlSpeed(); }
    MotorDriver* Motor() { return motor_; }

    // Toc do motor 0..100 % cho cac lenh tien/lui/re (driver co PWM)
    void SetSpeed(int percent) { motor_->SetDefaultSpeed(percent); }

    // Che do dieu khien ngoai (vd dang do line): tat lac lu / tu quay / tranh vat can
    void SetExternalControl(bool on) {
        external_ = on;
        if (on) StopAutoAvoid();
    }
    bool ExternalControl() const { return external_; }
    // ---- Dieu khien tu tay cam (app Dabble qua AT-09) ----
    bool HasAvoid() const { return HasHc04(); }
    bool IsAvoiding() const { return auto_avoid_enabled_; }
    // Bat tranh vat can (dung do line / lai tay neu dang chay)
    bool StartAvoidMode() {
        if (!HasHc04()) return false;
        StopExternal();
        manual_ = false;
        external_ = false;
        StartAutoAvoid();
        return true;
    }
    // Lai tay: left/right -100..100. 0,0 = tha tay (dung).
    void ManualDrive(int left, int right) {
        if (left != 0 || right != 0) {
            if (!manual_) {
                StopExternal();
                StopAutoAvoid();
                manual_ = true;
                external_ = true;  // tat lac lu / tu quay khi dang lai
            }
            motor_->SpeedRun(left, right);
        } else {
            motor_->Stop();
            if (manual_) {
                manual_ = false;
                external_ = false;
            }
        }
    }
    void ToggleLeds() {
        bool on = !led1_on_;
        led_force_on_ = on;
        led_force_off_ = !on;
        SetLeds(on);
    }
    void Wiggle() {
        StopAutoAvoid();
        motor_command_active_ = true;
        MotorWiggleSide();
        motor_command_active_ = false;
    }
    void Jerk() {
        StopAutoAvoid();
        motor_command_active_ = true;
        MotorJerk();
        motor_command_active_ = false;
    }

    // Goi khi co lenh dieu khien tay (tien/lui/dung...) -> dung che do ngoai (do line)
    void SetStopHook(std::function<void()> hook) { stop_hook_ = std::move(hook); }

    // Khoang cach do gan nhat (cm), -1 = loi doc. Khong chan (dung cho man OLED phu).
    int LastDistanceCm() const { return last_cm_; }

    // Do khoang cach dinh ky (0.4 s) khi khong o che do tu tranh vat can (che do do tu do).
    void StartDistancePoll() {
        if (!HasHc04() || poll_started_) return;
        poll_started_ = true;
        xTaskCreate(
            [](void* arg) {
                auto* self = static_cast<RbRobot*>(arg);
                while (true) {
                    if (!self->auto_avoid_enabled_) self->ReadDistanceCm();
                    vTaskDelay(pdMS_TO_TICKS(400));
                }
            },
            "hc04_poll", 2560, this, 2, nullptr);
    }

private:
    static constexpr const char* TAG = "RbRobot";
    RbRobotPins pins_;
    MotorDriver* motor_ = nullptr;
    std::atomic<bool> external_{false};
    std::atomic<bool> manual_{false};
    std::function<void()> stop_hook_;

    void StopExternal() {
        if (external_ && stop_hook_) stop_hook_();
    }
    SemaphoreHandle_t hc04_mutex_ = nullptr;
    std::atomic<int> last_cm_{-1};
    bool poll_started_ = false;

    bool led1_on_ = false;
    bool led2_on_ = false;
    bool led_force_on_ = false;
    bool led_force_off_ = false;

    volatile bool auto_avoid_enabled_ = false;
    int avoid_count_ = 0;
    TaskHandle_t avoid_task_handle_ = nullptr;
    TaskHandle_t idle_spin_task_handle_ = nullptr;
    TaskHandle_t speak_wiggle_task_handle_ = nullptr;
    volatile bool motor_command_active_ = false;

    void SetLeds(bool on) {
        led1_on_ = on;
        led2_on_ = on;
        if (pins_.led1 >= 0) gpio_set_level((gpio_num_t)pins_.led1, on ? 1 : 0);
        if (pins_.led2 >= 0) gpio_set_level((gpio_num_t)pins_.led2, on ? 1 : 0);
    }

    static void LedBootTask(void* arg) {
        RbRobot* self = static_cast<RbRobot*>(arg);
        vTaskDelay(pdMS_TO_TICKS(300000));  // 5 phút
        if (!self->led_force_on_ && !self->led_force_off_) {
            self->SetLeds(false);
            ESP_LOGI(TAG, "LED auto off after 5 minutes");
        }
        vTaskDelete(nullptr);
    }

    void MotorStop() { motor_->Stop(); }
    void MotorForward() { motor_->Forward(); }
    void MotorBackward() { motor_->Backward(); }
    void MotorTurnLeft() { motor_->TurnLeft(); }
    void MotorTurnRight() { motor_->TurnRight(); }

    void MotorSpinLeft(int ms = -1) {
        if (ms < 0) ms = pins_.spin_ms;
        MotorTurnLeft();
        vTaskDelay(pdMS_TO_TICKS(ms));
        MotorStop();
    }

    void MotorSpinRight(int ms = -1) {
        if (ms < 0) ms = pins_.spin_ms;
        MotorTurnRight();
        vTaskDelay(pdMS_TO_TICKS(ms));
        MotorStop();
    }

    void MotorWiggleSide() {
        for (int i = 0; i < 3; i++) {
            MotorTurnLeft();
            vTaskDelay(pdMS_TO_TICKS(180));
            MotorTurnRight();
            vTaskDelay(pdMS_TO_TICKS(180));
        }
        MotorStop();
    }

    void MotorJerk() {
        for (int i = 0; i < 3; i++) {
            MotorForward();
            vTaskDelay(pdMS_TO_TICKS(150));
            MotorBackward();
            vTaskDelay(pdMS_TO_TICKS(150));
        }
        MotorStop();
    }

    static void SpeakWiggleTask(void* arg) {
        RbRobot* self = static_cast<RbRobot*>(arg);
        // Ngẫu nhiên: lắc lư trái-phải HOẶC cà giựt tiến-lùi (đỡ chán)
        bool do_jerk = (esp_random() % 2) == 0;
        for (int i = 0; i < 4; i++) {
            if (Application::GetInstance().GetDeviceState() != kDeviceStateSpeaking) break;
            if (self->motor_command_active_ || self->external_) break;
            if (do_jerk) {
                self->MotorForward();
                vTaskDelay(pdMS_TO_TICKS(120));
                self->MotorBackward();
                vTaskDelay(pdMS_TO_TICKS(120));
            } else {
                self->MotorTurnLeft();
                vTaskDelay(pdMS_TO_TICKS(140));
                self->MotorTurnRight();
                vTaskDelay(pdMS_TO_TICKS(140));
            }
        }
        if (!self->motor_command_active_) {
            self->MotorStop();
        }
        self->speak_wiggle_task_handle_ = nullptr;
        vTaskDelete(nullptr);
    }

    void StartSpeakWiggle() {
        if (speak_wiggle_task_handle_ || auto_avoid_enabled_ || motor_command_active_ || external_) return;
        xTaskCreate(SpeakWiggleTask, "speak_wiggle", 3072, this, 4, &speak_wiggle_task_handle_);
    }

    static void SpeakWatchTask(void* arg) {
        RbRobot* self = static_cast<RbRobot*>(arg);
        bool was_speaking = false;
        while (true) {
            auto state = Application::GetInstance().GetDeviceState();
            bool speaking = (state == kDeviceStateSpeaking);
            if (speaking && !was_speaking && !self->motor_command_active_) {
                self->StartSpeakWiggle();
            }
            was_speaking = speaking;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    static void IdleSpinTask(void* arg) {
        RbRobot* self = static_cast<RbRobot*>(arg);
        while (true) {
            int wait_ms = 6000 + (esp_random() % 2001);
            vTaskDelay(pdMS_TO_TICKS(wait_ms));

            auto state = Application::GetInstance().GetDeviceState();
            if (!self->pins_.idle_move || state == kDeviceStateSpeaking || state == kDeviceStateListening ||
                self->auto_avoid_enabled_ || self->external_) {
                continue;
            }

            int action = esp_random() % 4;
            switch (action) {
                case 0:
                    ESP_LOGI(TAG, "Idle: spin LEFT 360");
                    self->MotorSpinLeft(self->pins_.spin_ms);
                    break;
                case 1:
                    ESP_LOGI(TAG, "Idle: spin RIGHT 360");
                    self->MotorSpinRight(self->pins_.spin_ms);
                    break;
                case 2:
                    ESP_LOGI(TAG, "Idle: wiggle side");
                    self->MotorWiggleSide();
                    break;
                default:
                    ESP_LOGI(TAG, "Idle: jerk forward-back");
                    self->MotorJerk();
                    break;
            }
            self->MotorStop();
        }
    }

    void InitializeLeds() {
        gpio_config_t io_conf = {};
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = 0;
        if (pins_.led1 >= 0) io_conf.pin_bit_mask |= 1ULL << pins_.led1;
        if (pins_.led2 >= 0) io_conf.pin_bit_mask |= 1ULL << pins_.led2;
        if (io_conf.pin_bit_mask == 0) return;
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        SetLeds(true);
        led_force_on_ = false;
        led_force_off_ = false;
        xTaskCreate(LedBootTask, "led_boot", 2048, this, 3, nullptr);
        ESP_LOGI(TAG, "LEDs ON (auto off after 5 min)");
    }

    bool HasHc04() const { return pins_.trig >= 0 && pins_.echo >= 0; }

    void InitializeHc04() {
        if (!HasHc04()) return;
        gpio_config_t out_conf = {};
        out_conf.mode = GPIO_MODE_OUTPUT;
        out_conf.pin_bit_mask = (1ULL << (gpio_num_t)pins_.trig);
        ESP_ERROR_CHECK(gpio_config(&out_conf));
        gpio_set_level((gpio_num_t)pins_.trig, 0);

        gpio_config_t in_conf = {};
        in_conf.mode = GPIO_MODE_INPUT;
        in_conf.pin_bit_mask = (1ULL << (gpio_num_t)pins_.echo);
        ESP_ERROR_CHECK(gpio_config(&in_conf));
        ESP_LOGI(TAG, "HC-SR04 initialized on %d/%d", (gpio_num_t)pins_.trig, (gpio_num_t)pins_.echo);
    }

    float ReadDistanceCm() {
        if (!HasHc04()) return -1;
        // 1 lan do tai 1 thoi diem (task tranh vat can, lenh giong noi, man OLED)
        xSemaphoreTake(hc04_mutex_, portMAX_DELAY);
        float cm = MeasureOnce();
        xSemaphoreGive(hc04_mutex_);
        last_cm_ = cm < 0 ? -1 : (int)(cm + 0.5f);
        return cm;
    }

    float MeasureOnce() {
        gpio_set_level((gpio_num_t)pins_.trig, 0);
        esp_rom_delay_us(2);
        gpio_set_level((gpio_num_t)pins_.trig, 1);
        esp_rom_delay_us(10);
        gpio_set_level((gpio_num_t)pins_.trig, 0);

        int64_t t0 = esp_timer_get_time();
        while (gpio_get_level((gpio_num_t)pins_.echo) == 0) {
            if (esp_timer_get_time() - t0 > 20000) return -1;
            taskYIELD();
        }
        int64_t t1 = esp_timer_get_time();
        while (gpio_get_level((gpio_num_t)pins_.echo) == 1) {
            if (esp_timer_get_time() - t1 > 20000) return -1;
            taskYIELD();
        }
        int64_t t2 = esp_timer_get_time();
        float cm = (t2 - t1) / 58.0f;
        if (cm < 2.0f || cm > 400.0f) return -1;
        return cm;
    }

    // consecutive obstacles without clear path
    int consecutive_blocks_ = 0;
    int64_t last_clear_time_us_ = 0;

    static void AvoidTask(void* arg) {
        RbRobot* self = static_cast<RbRobot*>(arg);
        self->consecutive_blocks_ = 0;
        self->last_clear_time_us_ = esp_timer_get_time();

        while (self->auto_avoid_enabled_) {
            float dist = self->ReadDistanceCm();
            ESP_LOGI(TAG, "Distance: %.1f cm  blocks=%d  avoid=%d",
                     dist, self->consecutive_blocks_, self->avoid_count_);

            bool blocked = (dist > 0 && dist < self->pins_.obstacle_cm);

            if (blocked) {
                self->consecutive_blocks_++;
                self->last_clear_time_us_ = esp_timer_get_time();

                // 7 lần liên tiếp bị chặn → tức giận + quay 180
                if (self->consecutive_blocks_ >= 11) {
                    ESP_LOGW(TAG, "Blocked 11 times -> angry + 180 turn");
                    self->MotorStop();
                    vTaskDelay(pdMS_TO_TICKS(150));
                    // lắc lư + cà giựt
                    self->MotorWiggleSide();
                    self->MotorJerk();
                    // quay đầu 180 độ
                    self->MotorSpinRight(self->pins_.spin_ms / 2);
                    self->MotorStop();
                    self->consecutive_blocks_ = 0;
                    self->avoid_count_ = 0;
                    self->MotorForward();
                    vTaskDelay(pdMS_TO_TICKS(300));
                    continue;
                }

                // Né vật cản: 3 lần trái rồi 3 lần phải
                self->MotorStop();
                vTaskDelay(pdMS_TO_TICKS(150));
                self->MotorBackward();
                vTaskDelay(pdMS_TO_TICKS(500));
                self->MotorStop();
                vTaskDelay(pdMS_TO_TICKS(150));

                int phase = self->avoid_count_ % 6;  // 0,1,2 = trái; 3,4,5 = phải
                if (phase < 3) {
                    ESP_LOGI(TAG, "Dodge LEFT (%d/3)", phase + 1);
                    self->MotorTurnLeft();
                } else {
                    ESP_LOGI(TAG, "Dodge RIGHT (%d/3)", phase - 2);
                    self->MotorTurnRight();
                }
                vTaskDelay(pdMS_TO_TICKS(450));
                self->MotorStop();
                vTaskDelay(pdMS_TO_TICKS(100));

                self->avoid_count_++;

                // Hết 3 trái + 3 phải → quay 180 độ đổi hướng
                if (self->avoid_count_ > 0 && (self->avoid_count_ % 6) == 0) {
                    ESP_LOGI(TAG, "6 dodges done -> turn 180");
                    self->MotorSpinLeft(self->pins_.spin_ms / 2);
                    self->MotorStop();
                    vTaskDelay(pdMS_TO_TICKS(200));
                }

                self->MotorForward();
            } else {
                // Đường trống
                self->consecutive_blocks_ = 0;
                int64_t now = esp_timer_get_time();
                int64_t clear_ms = (now - self->last_clear_time_us_) / 1000;

                // Đi thẳng không vật cản >= 7 giây → quay vòng chọn hướng mới
                if (clear_ms >= 7000) {
                    static const int spin_choices[] = {3000, 5000, 7000, 9000};
                    int spin_ms = spin_choices[esp_random() % 4];
                    bool left = (esp_random() % 2) == 0;
                    ESP_LOGI(TAG, "Clear 7s -> spin %s %dms then forward",
                             left ? "LEFT" : "RIGHT", spin_ms);
                    self->MotorStop();
                    if (left) self->MotorSpinLeft(spin_ms);
                    else      self->MotorSpinRight(spin_ms);
                    self->MotorStop();
                    self->last_clear_time_us_ = esp_timer_get_time();
                    self->avoid_count_ = 0;
                }

                self->MotorForward();
            }

            vTaskDelay(pdMS_TO_TICKS(180));
        }

        self->MotorStop();
        self->avoid_task_handle_ = nullptr;
        vTaskDelete(nullptr);
    }

    void StartAutoAvoid() {
        if (auto_avoid_enabled_ || !HasHc04() || external_) return;
        auto_avoid_enabled_ = true;
        avoid_count_ = 0;
        if (xTaskCreate(AvoidTask, "avoid_task", 8192, this, 5, &avoid_task_handle_) != pdPASS) {
            auto_avoid_enabled_ = false;
            ESP_LOGE(TAG, "Create avoid task failed");
        } else {
            ESP_LOGI(TAG, "Auto avoid started");
        }
    }

    void StopAutoAvoid() {
        auto_avoid_enabled_ = false;
    }

    void RunMotorWhileSpeaking(void (RbRobot::*move_fn)()) {
        StopExternal();
        if (auto_avoid_enabled_) StopAutoAvoid();
        motor_command_active_ = true;
        // Dừng lắc lư nếu đang chạy
        speak_wiggle_task_handle_ = nullptr;
        (this->*move_fn)();
        xTaskCreate([](void* arg) {
            auto* self = static_cast<RbRobot*>(arg);
            for (int i = 0; i < 30; ++i) {
                if (self->auto_avoid_enabled_) {
                    self->motor_command_active_ = false;
                    vTaskDelete(nullptr);
                    return;
                }
                if (Application::GetInstance().GetDeviceState() == kDeviceStateSpeaking) break;
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            while (Application::GetInstance().GetDeviceState() == kDeviceStateSpeaking) {
                if (self->auto_avoid_enabled_) {
                    self->motor_command_active_ = false;
                    vTaskDelete(nullptr);
                    return;
                }
                // Giữ đúng hướng lệnh, không để task khác xen vào
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            if (!self->auto_avoid_enabled_) self->MotorStop();
            self->motor_command_active_ = false;
            vTaskDelete(nullptr);
        }, "motor_speak_sync", 4096, this, 5, nullptr);
    }

    void InitializeTools() {
        auto& mcp = McpServer::GetInstance();

        mcp.AddTool("self.chassis.go_forward",
            "Tien, di toi, chay thang, di thang, tien len, di ve phia truoc, "
            "di tiep, chay tiep, tien lai, di ra truoc, move forward, go forward, "
            "di len phia truoc, chay ve phia truoc, tien len di, di toi di.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                RunMotorWhileSpeaking(&RbRobot::MotorForward);
                return true;
            });

        mcp.AddTool("self.chassis.go_back",
            "Lui, di lui, lui lai, di ve phia sau, thut lui, lui xuong, "
            "di ra sau, reverse, move back, go back, lui lai di, di lui lai.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                RunMotorWhileSpeaking(&RbRobot::MotorBackward);
                return true;
            });

        mcp.AddTool("self.chassis.turn_left",
            "Re trai, cua trai, quay trai, sang trai, trai, quay sang trai, "
            "re ben trai, cua ben trai, turn left, go left, trai nay, qua trai.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                RunMotorWhileSpeaking(&RbRobot::MotorTurnLeft);
                return true;
            });

        mcp.AddTool("self.chassis.turn_right",
            "Re phai, cua phai, quay phai, sang phai, phai, quay sang phai, "
            "re ben phai, cua ben phai, turn right, go right, phai nay, qua phai.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                RunMotorWhileSpeaking(&RbRobot::MotorTurnRight);
                return true;
            });

        mcp.AddTool("self.chassis.stop",
            "Dung, dung lai, dung xe, dung yen, stop, thang lai, dung chay nua, "
            "dung ngay, dung lai di, ngung lai, dung het, thoi dung.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                StopExternal();
                StopAutoAvoid();
                MotorStop();
                return true;
            });

        mcp.AddTool("self.chassis.start_auto_avoid",
            "Chay tu dong, bat tu dong, che do tu dong, tranh vat can, "
            "tu tranh chuong ngai, tu chay, auto, autonomous, di tu dong, "
            "bat tranh vat can, bat dau tranh vat can, tu ne, tu tranh, "
            "chay mot minh, tu hanh trinh.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                StopExternal();
                StartAutoAvoid();
                return true;
            });

        mcp.AddTool("self.chassis.stop_auto_avoid",
            "Dung tu dong, tat tu dong, tat tranh vat can, dung che do tu dong, "
            "het tu dong, stop auto, tat auto, thoi tu dong, ngung tu chay.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                StopExternal();
                StopAutoAvoid();
                MotorStop();
                return true;
            });

        mcp.AddTool("self.led.turn_on",
            "Bat den, mo den, bat led, light on, bat sang, mo sang, "
            "bat den len, mo den len, sang len, bat het den.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                led_force_on_ = true;
                led_force_off_ = false;
                SetLeds(true);
                return true;
            });

        mcp.AddTool("self.led.turn_off",
            "Tat den, dong den, tat led, light off, tat sang, "
            "tat den di, dong den lai, tat het den, tat sang di.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                led_force_on_ = false;
                led_force_off_ = true;
                SetLeds(false);
                return true;
            });

        mcp.AddTool("self.sensor.get_distance",
            "Do khoang cach, doc khoang cach, khoang cach bao nhieu, "
            "cam bien sieu am, distance, do vat can, con bao xa, "
            "cach bao nhieu, khoang cach hien tai, do xa gan.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                float d = ReadDistanceCm();
                if (!HasHc04()) return "Chua gan cam bien sieu am";
                if (d < 0) return "Loi doc cam bien";
                return std::to_string((int)d) + " cm";
            });

        if (HasPwm()) {
            mcp.AddTool("self.chassis.set_speed",
                "Chinh toc do xe / robot (phan tram 20-100): chay nhanh len, chay cham lai, giam toc, "
                "tang toc, toc do toi da, set speed. Mac dinh 20.",
                PropertyList({Property("speed", kPropertyTypeInteger, 20, 100)}),
                [this](const PropertyList& props) -> ReturnValue {
                    SetSpeed(props["speed"].value<int>());
                    return "Toc do " + std::to_string(motor_->Speed()) + "%";
                });
        }

        mcp.AddTool("self.chassis.wiggle",
            "Lac lu, lac lu trai phai, lung tung, lac qua lac lai, "
            "wiggle, sway, lac nhe, lac lu di, vui ve lac lu.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                if (auto_avoid_enabled_) StopAutoAvoid();
                motor_command_active_ = true;
                MotorWiggleSide();
                motor_command_active_ = false;
                return true;
            });

        mcp.AddTool("self.chassis.jerk",
            "Ca giut, tien lui ca giut, giat manh, giat lui giat toi, "
            "jerk, shake, ca giut di, the hien manh, giat giat.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                if (auto_avoid_enabled_) StopAutoAvoid();
                motor_command_active_ = true;
                MotorJerk();
                motor_command_active_ = false;
                return true;
            });

        mcp.AddTool("self.emotion.surprise",
            "Bat ngo, soc, ngac nhien, tuc gian, gian du, shock, angry, "
            "the hien su bat ngo, the hien su tuc gian, gian qua, soc qua, "
            "ngac nhien qua, bi soc, ca giut, giat minh, phan ung bat ngo, "
            "the hien cam xuc manh, gian du len.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                if (auto_avoid_enabled_) StopAutoAvoid();
                for (int i = 0; i < 3; i++) {
                    MotorForward();
                    vTaskDelay(pdMS_TO_TICKS(150));
                    MotorBackward();
                    vTaskDelay(pdMS_TO_TICKS(150));
                }
                MotorStop();
                return true;
            });
    }
};

#endif  // _MULTI_RB_ROBOT_H_

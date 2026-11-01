// Driver 2 motor DC (cau H) cho robot: chon loai driver, dieu khien toc do -100..100 %.
//
//  Loai                        Chan moi motor                  Cach dieu khien
//  0 4 chan (nhu RB5320)       IN1, IN2                        bat/tat, khong chinh toc do
//  1 TB6612FNG                 IN1, IN2, PWM (+ STBY chung)    IN chieu, PWM toc do
//  2 L298N                     IN1, IN2, EN (ENA/ENB)          IN chieu, EN toc do (bo trong = cam jumper)
//  3 L293D                     IN1, IN2, EN                    giong L298N
//  4 DRV8833                   IN1, IN2                        PWM tren chan IN
//  5 L9110S / HG7881           IA, IB                          PWM tren chan IN
//  6 MX1508                    IN1, IN2                        PWM tren chan IN
//  7 BTS7960 / IBT-2           RPWM, LPWM (R_EN, L_EN noi VCC) PWM tren chan IN
//
// PWM: LEDC timer 2, kenh 4..7, 20 kHz (khong nghe tieng rit).
// (timer 0 / kenh 0: den nen man, timer 1 / kenh 0: LED GPIO)
#ifndef _MULTI_MOTOR_DRIVER_H_
#define _MULTI_MOTOR_DRIVER_H_

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_log.h>

#include <algorithm>
#include <cstdlib>
#include <mutex>

enum class MotorDriverType : int {
    Generic4 = 0,
    Tb6612 = 1,
    L298n = 2,
    L293d = 3,
    Drv8833 = 4,
    L9110s = 5,
    Mx1508 = 6,
    Bts7960 = 7,
};
constexpr int kMotorDriverCount = 8;

inline const char* MotorDriverName(MotorDriverType t) {
    static const char* names[] = {"4 chan", "TB6612FNG", "L298N", "L293D", "DRV8833", "L9110S", "MX1508", "BTS7960"};
    int i = (int)t;
    return (i >= 0 && i < kMotorDriverCount) ? names[i] : "?";
}

// Driver co chan EN/PWM rieng (IN1/IN2 = chieu)
inline bool MotorDriverHasEnable(MotorDriverType t) {
    return t == MotorDriverType::Tb6612 || t == MotorDriverType::L298n || t == MotorDriverType::L293d;
}
// Driver bam xung truc tiep tren IN1/IN2
inline bool MotorDriverPwmOnIn(MotorDriverType t) {
    return t == MotorDriverType::Drv8833 || t == MotorDriverType::L9110s || t == MotorDriverType::Mx1508 ||
           t == MotorDriverType::Bts7960;
}

struct MotorPins {
    MotorDriverType type = MotorDriverType::Generic4;
    int l1 = -1, l2 = -1, r1 = -1, r2 = -1;  // trai IN1/IN2, phai IN1/IN2 (TB6612: BIN1/BIN2, AIN1/AIN2)
    int pwm_l = -1, pwm_r = -1;              // EN/PWM trai (PWMB/ENB), phai (PWMA/ENA)
    int stby = -1;                           // TB6612 STBY (-1 = noi cung 3.3V)
    bool invert_l = true;                    // dao chieu motor trai (RB5320: true)
    bool invert_r = false;                   // dao chieu motor phai
    bool swap_lr = false;                    // doi banh: lenh trai ra kenh phai va nguoc lai
    bool speed_run = false;                  // true: TB6612 theo sketch speed_run (A=trai, B=phai)
    int speed = 100;                         // toc do % cho cac lenh tien/lui/re
};

class MotorDriver {
public:
    explicit MotorDriver(const MotorPins& p) : p_(p) {
        uint64_t mask = 0;
        for (int pin : {p_.l1, p_.l2, p_.r1, p_.r2, p_.stby}) {
            if (pin >= 0) mask |= 1ULL << pin;
        }
        if (MotorDriverHasEnable(p_.type)) {
            for (int pin : {p_.pwm_l, p_.pwm_r})
                if (pin >= 0) mask |= 1ULL << pin;
        }
        if (mask) {
            gpio_config_t io = {};
            io.mode = GPIO_MODE_OUTPUT;
            io.pin_bit_mask = mask;
            gpio_config(&io);
        }
        if (p_.stby >= 0) gpio_set_level((gpio_num_t)p_.stby, 1);
        for (int pin : {p_.l1, p_.l2, p_.r1, p_.r2}) Level(pin, 0);

        if (MotorDriverPwmOnIn(p_.type)) {
            // 4 kenh PWM tren 4 chan IN
            pwm_ok_ = InitTimer() && AddChannel(p_.l1, 4) && AddChannel(p_.l2, 5) && AddChannel(p_.r1, 6) &&
                      AddChannel(p_.r2, 7);
        } else if (MotorDriverHasEnable(p_.type) && (p_.pwm_l >= 0 || p_.pwm_r >= 0)) {
            pwm_ok_ = InitTimer();
            if (pwm_ok_ && p_.pwm_l >= 0) pwm_ok_ = AddChannel(p_.pwm_l, 4);
            if (pwm_ok_ && p_.pwm_r >= 0) pwm_ok_ = AddChannel(p_.pwm_r, 5);
        }
        Set(0, 0);
        ESP_LOGI(TAG, "Driver %s, PWM %s", MotorDriverName(p_.type), pwm_ok_ ? "co" : "khong");
    }

    bool CanControlSpeed() const { return pwm_ok_; }
    int Speed() const { return p_.speed; }
    void SetDefaultSpeed(int percent) { p_.speed = std::clamp(percent, 0, 100); }
    const MotorPins& Pins() const { return p_; }

    // Mot ham cho moi lenh motor. left/right: -100..100 (%), duong = tien.
    // TB6612: banh trai = Motor A (AIN1/AIN2/PWMA), banh phai = Motor B (BIN1/BIN2/PWMB).
    //   trai > 0: AIN1=1, AIN2=0. phai > 0: BIN1=0, BIN2=1.
    //   bang 0: chi tat PWM (short brake), giu chan IN. STBY da noi VCC.
    void SpeedRun(int left, int right) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (p_.swap_lr) {
            int tmp = left;
            left = right;
            right = tmp;
        }
        if (p_.invert_l) left = -left;
        if (p_.invert_r) right = -right;
        if (p_.speed_run || p_.type == MotorDriverType::Tb6612) {
            ApplyChannel(p_.l1, p_.l2, p_.pwm_l, 4, left, true);
            ApplyChannel(p_.r1, p_.r2, p_.pwm_r, 5, right, false);
            return;
        }
        Drive(false, left);
        Drive(true, right);
    }

    // Ban sao speedRun() trong backup/line_tb6612_6sensor.ino.
    // left/right: -255..255 (duty 8-bit), duong = tien. 255 = 100% PWM.
    // Lenh phai ra Motor A (AIN/PWMA), lenh trai ra Motor B (BIN/PWMB).
    // Ca hai kenh, toc do duong: IN1=0, IN2=1.
    void SpeedRun255(int left, int right) {
        std::lock_guard<std::mutex> lock(mutex_);
        SketchChannel(p_.l1, p_.l2, p_.pwm_l, 4, std::clamp(right, -255, 255));
        SketchChannel(p_.r1, p_.r2, p_.pwm_r, 5, std::clamp(left, -255, 255));
    }

    void Set(int left, int right) { SpeedRun(left, right); }

    // Cac lenh co ban o toc do mac dinh
    void Stop() { SpeedRun(0, 0); }
    void Forward() { SpeedRun(p_.speed, p_.speed); }
    void Backward() { SpeedRun(-p_.speed, -p_.speed); }
    void TurnLeft() { SpeedRun(-p_.speed, p_.speed); }   // quay tai cho sang trai
    void TurnRight() { SpeedRun(p_.speed, -p_.speed); }

private:
    static constexpr const char* TAG = "MotorDriver";
    static constexpr ledc_timer_t kTimer = LEDC_TIMER_2;
    static constexpr int kMaxDuty = 1023;  // 10 bit
    MotorPins p_;
    bool pwm_ok_ = false;
    std::mutex mutex_;

    bool InitTimer() {
        ledc_timer_config_t t = {};
        t.speed_mode = LEDC_LOW_SPEED_MODE;
        t.duty_resolution = LEDC_TIMER_10_BIT;
        t.timer_num = kTimer;
        t.freq_hz = 20000;
        t.clk_cfg = LEDC_AUTO_CLK;
        if (ledc_timer_config(&t) != ESP_OK) {
            ESP_LOGE(TAG, "Khong khoi tao duoc PWM motor");
            return false;
        }
        return true;
    }

    bool AddChannel(int pin, int ch) {
        if (pin < 0) return true;
        ledc_channel_config_t c = {};
        c.gpio_num = pin;
        c.speed_mode = LEDC_LOW_SPEED_MODE;
        c.channel = (ledc_channel_t)ch;
        c.timer_sel = kTimer;
        c.duty = 0;
        c.hpoint = 0;
        return ledc_channel_config(&c) == ESP_OK;
    }

    static void Duty(int ch, int percent) {
        uint32_t d = (uint32_t)std::clamp(percent, 0, 100) * kMaxDuty / 100;
        ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, d);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch);
    }

    // duty 0..255 tren timer 10-bit: 255 = kMaxDuty, cung ti le voi sketch 8-bit.
    void Duty255(int ch, int duty8) {
        uint32_t d = (uint32_t)std::clamp(duty8, 0, 255) * kMaxDuty / 255;
        ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, d);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch);
    }

    void SketchChannel(int in1, int in2, int pwm, int ch, int speed) {
        if (speed >= 0) {
            Level(in1, 0);
            Level(in2, 1);
        } else {
            Level(in1, 1);
            Level(in2, 0);
            speed = -speed;
        }
        if (pwm < 0) return;
        if (pwm_ok_) Duty255(ch, speed);
        else Level(pwm, speed > 0 ? 1 : 0);
    }

    static void Level(int pin, int v) {
        if (pin >= 0) gpio_set_level((gpio_num_t)pin, v);
    }

    // PWM = 0, khong doi IN1/IN2 (TB6612: short brake neu dang o mot chieu).
    void PwmOff(int pin, int ch) {
        if (pin < 0) return;
        if (pwm_ok_) Duty(ch, 0);
        else Level(pin, 0);
    }

    // forward_in1_high: chieu tien la IN1=1, IN2=0. Nguoc lai thi IN1=0, IN2=1.
    void ApplyChannel(int in1, int in2, int pwm, int ch, int value, bool forward_in1_high) {
        value = std::clamp(value, -100, 100);
        if (value == 0) {
            PwmOff(pwm, ch);
            return;
        }
        const bool in1_high = forward_in1_high ? (value > 0) : (value < 0);
        Level(in1, in1_high ? 1 : 0);
        Level(in2, in1_high ? 0 : 1);
        const int mag = std::abs(value);
        if (pwm < 0) return;
        if (pwm_ok_) Duty(ch, mag);
        else Level(pwm, 1);
    }

    // value > 0: IN1 = 1, IN2 = 0 ; value < 0: nguoc lai ; 0: dung (tha troi)
    void Drive(bool right, int value) {
        value = std::clamp(value, -100, 100);
        const int in1 = right ? p_.r1 : p_.l1;
        const int in2 = right ? p_.r2 : p_.l2;
        const int mag = std::abs(value);

        if (MotorDriverPwmOnIn(p_.type) && pwm_ok_) {
            const int ch1 = right ? 6 : 4, ch2 = right ? 7 : 5;
            Duty(ch1, value > 0 ? mag : 0);
            Duty(ch2, value < 0 ? mag : 0);
            return;
        }
        Level(in1, value > 0 ? 1 : 0);
        Level(in2, value < 0 ? 1 : 0);
        if (MotorDriverHasEnable(p_.type)) {
            const int en = right ? p_.pwm_r : p_.pwm_l;
            if (en < 0) return;  // EN noi cung (jumper) -> toc do toi da
            if (pwm_ok_) Duty(right ? 5 : 4, mag);
            else Level(en, mag > 0 ? 1 : 0);
        }
    }
};

#endif  // _MULTI_MOTOR_DRIVER_H_

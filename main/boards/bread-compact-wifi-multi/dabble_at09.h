// Dieu khien robot bang app Dabble (GamePad) qua module Bluetooth BLE AT-09 / HM-10 (UART 9600).
// Noi day: ESP TX -> RXD cua AT-09, ESP RX <- TXD cua AT-09, VCC 3.3-5V, GND chung.
//
//  START  : bat che do tu dong (board tranh vat can: tranh vat can; board do line: do line)
//  SELECT : tat che do tu dong, ve lai tay
//  Mui ten / joystick: lai (dang tu dong ma bam mui ten -> dung tu dong, chuyen lai tay)
//  X      : dung
//  VUONG  : bat / tat den
//  TRON   : board do line: hieu chinh cam bien ; board khac: lac lu
//  TAM GIAC: board co ca 2 chuc nang: do line ; board khac: ca giut
#ifndef _MULTI_DABBLE_AT09_H_
#define _MULTI_DABBLE_AT09_H_

#include "dabble_proto.h"
#include "line_follower.h"
#include "rb_robot.h"

#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

class At09Gamepad {
public:
    // start_is_line: START bat do line (true) hay tranh vat can (false)
    At09Gamepad(int tx, int rx, int baud, RbRobot* robot, LineFollower* line, bool start_is_line,
                uart_port_t port = UART_NUM_1)
        : robot_(robot), line_(line), start_is_line_(start_is_line), port_(port) {
        uart_config_t cfg = {};
        cfg.baud_rate = baud > 0 ? baud : 9600;
        cfg.data_bits = UART_DATA_8_BITS;
        cfg.parity = UART_PARITY_DISABLE;
        cfg.stop_bits = UART_STOP_BITS_1;
        cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        cfg.source_clk = UART_SCLK_DEFAULT;
        if (uart_driver_install(port_, 512, 0, 0, nullptr, 0) != ESP_OK || uart_param_config(port_, &cfg) != ESP_OK ||
            uart_set_pin(port_, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
            ESP_LOGE(TAG, "Khong mo duoc UART cho AT-09 (TX %d, RX %d)", tx, rx);
            return;
        }
        ok_ = true;
        xTaskCreate([](void* a) { static_cast<At09Gamepad*>(a)->Loop(); }, "at09", 4096, this, 4, nullptr);
        ESP_LOGI(TAG, "AT-09 san sang: ESP TX %d -> RXD, ESP RX %d <- TXD, %d baud", tx, rx, cfg.baud_rate);
    }

    bool Ok() const { return ok_; }

private:
    static constexpr const char* TAG = "At09";
    RbRobot* robot_;
    LineFollower* line_;
    bool start_is_line_;
    uart_port_t port_;
    bool ok_ = false;
    dabble::Parser parser_;
    dabble::Gamepad prev_;
    bool driving_ = false;
    char text_[8] = {};  // nhan "OK+LOST" / "OK+CONN" cua module

    void Write(const void* data, size_t len) { uart_write_bytes(port_, (const char*)data, len); }

    void Loop() {
        // Bat thong bao ket noi / mat ket noi (chi co tac dung khi chua ket noi app)
        vTaskDelay(pdMS_TO_TICKS(400));
        Write("AT+NOTI1", 8);
        uint8_t buf[64];
        while (true) {
            int n = uart_read_bytes(port_, buf, sizeof(buf), pdMS_TO_TICKS(50));
            for (int i = 0; i < n; i++) {
                uint8_t b = buf[i];
                // chu "OK+LOST" -> app ngat ket noi -> dung xe
                memmove(text_, text_ + 1, sizeof(text_) - 2);
                text_[sizeof(text_) - 2] = (char)b;
                if (strstr(text_, "OK+LOST")) {
                    ESP_LOGW(TAG, "App ngat ket noi -> dung lai tay");
                    if (driving_) robot_->ManualDrive(0, 0);
                    driving_ = false;
                    prev_ = dabble::Gamepad();
                    text_[0] = 0;
                }
                bool board_req = false;
                dabble::Gamepad g = prev_;
                if (parser_.Feed(b, g, board_req)) {
                    Handle(g);
                    prev_ = g;
                }
                if (board_req) Write(dabble::kBoardIdReply, sizeof(dabble::kBoardIdReply));
            }
        }
    }

    bool AutoRunning() const { return robot_->IsAvoiding() || (line_ && line_->IsRunning()); }

    void Handle(const dabble::Gamepad& g) {
        auto pressed = [&](bool now, bool before) { return now && !before; };

        if (pressed(g.start, prev_.start)) {
            if (start_is_line_ && line_) {
                robot_->ManualDrive(0, 0);
                line_->Start();
                ESP_LOGI(TAG, "START -> do line");
            } else if (robot_->StartAvoidMode()) {
                ESP_LOGI(TAG, "START -> tranh vat can");
            } else if (line_) {
                line_->Start();
            }
        }
        if (pressed(g.select, prev_.select) || pressed(g.cross, prev_.cross)) {
            robot_->Stop();  // tat tranh vat can + do line + dung motor
            driving_ = false;
            ESP_LOGI(TAG, "%s -> lai tay", g.select ? "SELECT" : "X");
        }
        if (pressed(g.square, prev_.square)) robot_->ToggleLeds();
        if (pressed(g.circle, prev_.circle)) {
            if (line_) line_->CalibrateNow();
            else robot_->Wiggle();
        }
        if (pressed(g.triangle, prev_.triangle)) {
            if (line_ && !start_is_line_) line_->Start();
            else robot_->Jerk();
        }

        int l = 0, r = 0;
        dabble::Drive(g, robot_->Motor()->Speed(), l, r);
        if (l != 0 || r != 0) {
            if (AutoRunning()) robot_->Stop();  // bam mui ten khi dang tu dong -> ve lai tay
            robot_->ManualDrive(l, r);
            driving_ = true;
        } else if (driving_) {
            robot_->ManualDrive(0, 0);
            driving_ = false;
        }
    }
};

#endif  // _MULTI_DABBLE_AT09_H_

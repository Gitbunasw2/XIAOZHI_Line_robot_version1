// Giao thuc app Dabble (STEMpedia) qua Bluetooth BLE serial (AT-09 / HM-10).
// Khung: 0xFF, module, function, so_tham_so, [do_dai, du_lieu...]..., 0x00
// GamePad = module 0x01: function 0x01 (nut), 0x02 (joystick), 0x03 (nghieng may)
//   byte 0: START bit0, SELECT bit1, TAM GIAC bit2, TRON bit3, X bit4, VUONG bit5
//   byte 1: che do nut: LEN bit0, XUONG bit1, TRAI bit2, PHAI bit3
//           che do joystick: goc = (byte >> 3) * 15 do, ban kinh = byte & 7 (0..7)
// Khong phu thuoc phan cung -> test duoc tren may tinh.
#ifndef _MULTI_DABBLE_PROTO_H_
#define _MULTI_DABBLE_PROTO_H_

#include <stdint.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace dabble {

struct Gamepad {
    bool start = false, select = false, triangle = false, circle = false, cross = false, square = false;
    bool up = false, down = false, left = false, right = false;
    bool joystick = false;  // true = che do joystick / nghieng
    int angle = 0;          // do, 0 = sang phai, 90 = len
    int radius = 0;         // 0..7

    bool AnyDirection() const { return up || down || left || right || (joystick && radius > 0); }
    bool operator==(const Gamepad& o) const {
        return start == o.start && select == o.select && triangle == o.triangle && circle == o.circle &&
               cross == o.cross && square == o.square && up == o.up && down == o.down && left == o.left &&
               right == o.right && joystick == o.joystick && angle == o.angle && radius == o.radius;
    }
};

// Doc tung byte; tra ve true khi vua co 1 khung GamePad hoan chinh (out duoc cap nhat).
// board_id_request = true khi app hoi loai board (can tra loi kBoardIdReply).
class Parser {
public:
    bool Feed(uint8_t b, Gamepad& out, bool& board_id_request) {
        board_id_request = false;
        switch (state_) {
            case 0:
                if (b == 0xFF) {
                    state_ = 1;
                    argc_ = argi_ = len_ = pos_ = 0;
                    std::memset(arg0_, 0, sizeof(arg0_));
                }
                return false;
            case 1:
                module_ = b;
                state_ = module_ < 18 ? 2 : 0;
                return false;
            case 2:
                func_ = b;
                state_ = 3;
                return false;
            case 3:
                argc_ = b;
                if (argc_ > 8) {
                    state_ = 0;
                    return false;
                }
                state_ = argc_ == 0 ? 6 : 4;
                return false;
            case 4:
                len_ = b;
                pos_ = 0;
                if (len_ == 0) {  // tham so rong
                    if (++argi_ >= argc_) state_ = 6;
                    return false;
                }
                state_ = 5;
                return false;
            case 5:
                if (argi_ == 0 && pos_ < (int)sizeof(arg0_)) arg0_[pos_] = b;
                if (++pos_ >= len_) {
                    if (argi_ == 0) arg0_len_ = len_;
                    state_ = (++argi_ >= argc_) ? 6 : 4;
                }
                return false;
            case 6:
                state_ = 0;
                if (b != 0x00) return false;  // khung hong
                if (module_ == 0x00 && func_ == 0x03) {
                    board_id_request = true;
                    return false;
                }
                if (module_ != 0x01 || argc_ < 1 || arg0_len_ < 2) return false;
                Decode(out);
                return true;
        }
        state_ = 0;
        return false;
    }

private:
    int state_ = 0, module_ = 0, func_ = 0, argc_ = 0, argi_ = 0, len_ = 0, pos_ = 0, arg0_len_ = 0;
    uint8_t arg0_[8] = {};

    void Decode(Gamepad& g) {
        uint8_t v0 = arg0_[0], v1 = arg0_[1];
        g.start = v0 & 0x01;
        g.select = v0 & 0x02;
        g.triangle = v0 & 0x04;
        g.circle = v0 & 0x08;
        g.cross = v0 & 0x10;
        g.square = v0 & 0x20;
        g.joystick = func_ != 0x01;
        if (!g.joystick) {
            g.up = v1 & 0x01;
            g.down = v1 & 0x02;
            g.left = v1 & 0x04;
            g.right = v1 & 0x08;
            g.angle = 0;
            g.radius = 0;
        } else {
            g.up = g.down = g.left = g.right = false;
            g.angle = (v1 >> 3) * 15;
            g.radius = v1 & 0x07;
        }
    }
};

// Tra loi "board khac" cho app (giong thu vien Dabble cho board khong phai Uno/Mega/Nano)
inline constexpr uint8_t kBoardIdReply[] = {0xFF, 0x00, 0x03, 0x01, 0x04, 0x05, 0x01, 0x05, 0x02, 0x00};

// Tay cam -> toc do 2 banh (-speed..speed). Nut: len/xuong/trai/phai, cheo = vua chay vua re.
// Joystick: ban kinh = do manh, goc = huong.
inline void Drive(const Gamepad& g, int speed, int& left, int& right) {
    left = right = 0;
    if (g.joystick) {
        if (g.radius <= 0) return;
        const double a = g.angle * M_PI / 180.0;
        const double mag = g.radius / 7.0;
        double y = std::sin(a) * mag, x = std::cos(a) * mag;
        double l = y + x, r = y - x;
        double m = std::max(1.0, std::max(std::fabs(l), std::fabs(r)));
        left = (int)std::lround(l / m * speed);
        right = (int)std::lround(r / m * speed);
        return;
    }
    int fwd = (g.up ? 1 : 0) - (g.down ? 1 : 0);
    int turn = (g.right ? 1 : 0) - (g.left ? 1 : 0);
    if (fwd == 0 && turn == 0) return;
    if (fwd == 0) {  // quay tai cho
        left = turn * speed;
        right = -turn * speed;
        return;
    }
    // tien / lui, co the vua re: banh phia trong cham lai 40 %
    left = right = fwd * speed;
    if (turn > 0) right = fwd * speed * 4 / 10;
    if (turn < 0) left = fwd * speed * 4 / 10;
}

}  // namespace dabble

#endif  // _MULTI_DABBLE_PROTO_H_

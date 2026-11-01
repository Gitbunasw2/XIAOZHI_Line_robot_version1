// Thuat toan do line PID (khong phu thuoc phan cung -> test duoc tren may tinh).
//  - Chuan hoa gia tri cam bien theo hieu chinh min/max -> 0..1000 (1000 = dang tren vach)
//  - Vi tri vach = trung binh co trong so: cam bien 0 (trai nhat) = 0, cam bien n-1 = (n-1)*1000
//  - 6 mat: giua = 2500. error > 0 = vach ben phai
//  - Mat vach: quay tai cho theo error lan truoc (khong chay PID)
//  - Con thay vach: PID, giam toc khi |error| > 1000
//  - Motor: trai = toc do + hieu chinh, phai = toc do - hieu chinh (don vi %)
#ifndef _MULTI_LINE_PID_H_
#define _MULTI_LINE_PID_H_

#include <algorithm>
#include <cmath>

namespace line {

constexpr int kMaxSensors = 6;

struct Calib {
    int min[kMaxSensors];
    int max[kMaxSensors];
    void Reset(int lo, int hi) {
        for (int i = 0; i < kMaxSensors; i++) {
            min[i] = lo;
            max[i] = hi;
        }
    }
};

struct Params {
    float kp = 0.04f;      // ti le
    float ki = 0.0f;       // tich phan (moi chu ky 10 ms), thuong de 0
    float kd = 0.1f;       // vi phan (moi chu ky 10 ms)
    int base_speed = 50;   // toc do co ban %
    int max_speed = 100;   // gioi han %
    int run_duty = 140;    // toc do thang, duty 0..255 (sketch maxSpeed)
    int recovery_duty = 80;  // quay khi mat vach, duty 0..255
    bool mix_reverse = true;  // true: banh lech duoc lui. false: chi chay toi, kep 0..255
    bool white_line = false;  // false = vach DEN tren nen trang, true = vach TRANG tren nen den
    int threshold = 200;   // 0..1000: duoi muc nay coi nhu khong thay vach
    float alpha_d = 0.2f;  // loc thong thap cho thanh phan D (1 = khong loc)
    int slew = 4;          // thay doi toc do toi da moi chu ky (%), chong giat
    bool corners = true;   // nhan goc vuong (3 mat 1 ben thay vach, 2 mat ben kia khong)
};

// Goc vuong: -1 = re trai, 1 = re phai, 0 = khong (giong code Arduino QTR mau)
inline int Corner(const int* v, int n) {
    if (n != 6) return 0;
    if (v[0] > 800 && v[1] > 800 && v[2] > 800 && v[4] < 400 && v[5] < 400) return -1;
    if (v[5] > 800 && v[4] > 800 && v[3] > 800 && v[0] < 400 && v[1] < 400) return 1;
    return 0;
}

// Gioi han muc thay doi moi chu ky
inline int Slew(int target, int prev, int step) {
    if (step <= 0) return target;
    if (target > prev + step) return prev + step;
    if (target < prev - step) return prev - step;
    return target;
}

// raw -> 0..1000, 1000 = tren vach
inline void Normalize(const int* raw, int n, const Calib& c, bool white_line, int* out) {
    for (int i = 0; i < n; i++) {
        int lo = c.min[i], hi = c.max[i];
        int v;
        if (hi <= lo) {
            v = 0;
        } else {
            v = (int)((long)(std::clamp(raw[i], lo, hi) - lo) * 1000 / (hi - lo));
        }
        // cam bien hong ngoai thuong cho gia tri CAO tren vach den
        out[i] = white_line ? 1000 - v : v;
    }
}

// Vi tri vach 0..(n-1)*1000. last: vi tri lan truoc (cap nhat). on_line: co thay vach khong.
inline int Position(const int* v, int n, int threshold, int& last, bool& on_line) {
    long sum = 0, weighted = 0;
    on_line = false;
    for (int i = 0; i < n; i++) {
        int x = v[i];
        if (x > threshold) on_line = true;
        if (x > 50) {  // bo nhieu nho
            weighted += (long)x * i * 1000;
            sum += x;
        }
    }
    const int max_pos = (n - 1) * 1000;
    if (!on_line || sum == 0) {
        // mat vach: day ve phia nhin thay cuoi cung
        last = last < max_pos / 2 ? 0 : max_pos;
        return last;
    }
    last = (int)(weighted / sum);
    return last;
}

class Pid {
public:
    void Reset() {
        integral_ = 0;
        prev_ = 0;
        d_filt_ = 0;
        first_ = true;
    }
    // tra ve luong hieu chinh (don vi % toc do)
    float Update(const Params& p, float error) {
        integral_ = std::clamp(integral_ + error, -50000.0f, 50000.0f);
        float d = first_ ? 0 : error - prev_;
        d_filt_ = p.alpha_d * d + (1 - p.alpha_d) * d_filt_;
        first_ = false;
        prev_ = error;
        return p.kp * error + p.ki * integral_ + p.kd * d_filt_;
    }

private:
    float integral_ = 0, prev_ = 0, d_filt_ = 0;
    bool first_ = true;
};

// Tron toc do 2 banh, gioi han [-max, max]
inline void Mix(const Params& p, float correction, int& left, int& right) {
    left = (int)std::lround(p.base_speed + correction);
    right = (int)std::lround(p.base_speed - correction);
    left = std::clamp(left, -p.max_speed, p.max_speed);
    right = std::clamp(right, -p.max_speed, p.max_speed);
}

// Mot chu ky do line, 6 mat (hoac n mat). Tra ve % cho SpeedRun(left, right).
// Mat vach: quay tai cho. last_error > 0 (vach nam phai) -> quay phai.
// Con thay: PID, |error| > 1000 thi giam toc do co ban xuong mot nua o mep thang do.
inline void Follow(const int* v, int n, const Params& p, Pid& pid, int& last_error, int& left, int& right) {
    long weighted = 0;
    long sum = 0;
    bool on_line = false;
    for (int i = 0; i < n; i++) {
        int x = v[i];
        if (x > p.threshold) on_line = true;
        weighted += (long)x * i * 1000;
        sum += x;
    }

    const int set_point = (n - 1) * 500;
    if (!on_line || sum == 0) {
        pid.Reset();
        int rec = std::clamp(p.base_speed * 160 / 200, 0, p.max_speed);
        if (last_error > 0) {
            left = rec;
            right = -rec;
        } else {
            left = -rec;
            right = rec;
        }
        return;
    }

    int error = (int)(weighted / sum) - set_point;
    float corr = pid.Update(p, (float)error);
    int base = p.base_speed;
    if (std::abs(error) > 1000 && set_point > 1000) {
        int turn = std::clamp(p.base_speed / 2, 0, p.max_speed);
        int ae = std::min(std::abs(error), set_point);
        base = p.base_speed + (turn - p.base_speed) * (ae - 1000) / (set_point - 1000);
    }
    left = std::clamp((int)std::lround(base + corr), -p.max_speed, p.max_speed);
    right = std::clamp((int)std::lround(base - corr), -p.max_speed, p.max_speed);
    last_error = error;
}

}  // namespace line

#endif  // _MULTI_LINE_PID_H_

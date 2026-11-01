// Co gian logo / hinh nen cho moi kich thuoc man, uu tien do NET.
// Khong phu thuoc phan cung de test duoc tren may tinh.
//
//  - Logo: ban goc 1-bit do phan giai cao (rb_logo_1bpp.c), luon THU NHO bang trung binh
//    dien tich (box filter) -> vien muot, khong rang cua, khong mo.
//  - Hinh nen (goc 24-bit): tach 2 chieu; chieu nao THU NHO dung trung binh dien tich, chieu nao
//    PHONG TO dung bicubic Catmull-Rom, sau do lam net (unsharp) va dither khi ve RGB565.
#ifndef _MULTI_LOGO_SCALE_H_
#define _MULTI_LOGO_SCALE_H_

#include <stdint.h>
#include <stdlib.h>
#include <algorithm>
#include <vector>

namespace multi {

struct LogoFit {
    int width = 0;
    int height = 0;
};

// Kich thuoc logo lon nhat vua khung man (chua le margin_percent % moi canh), giu ti le src.
inline LogoFit FitLogo(int screen_w, int screen_h, int src_w, int src_h, int margin_percent = 6) {
    LogoFit f;
    if (screen_w <= 0 || screen_h <= 0 || src_w <= 0 || src_h <= 0) return f;
    int box_w = std::max(1, screen_w - 2 * (screen_w * margin_percent / 100));
    int box_h = std::max(1, screen_h - 2 * (screen_h * margin_percent / 100));
    if ((long)box_w * src_h <= (long)box_h * src_w) {
        f.width = box_w;
        f.height = std::max(1, (int)((long)box_w * src_h / src_w));
    } else {
        f.height = box_h;
        f.width = std::max(1, (int)((long)box_h * src_w / src_h));
    }
    return f;
}

inline uint16_t Gray565(int v) {
    return (uint16_t)(((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3));
}

// Bang trong so 1 chieu: moi diem dich -> danh sach (chi so nguon, trong so /4096)
struct AxisTaps {
    std::vector<int> start;    // vi tri bat dau trong idx/w cho moi diem dich
    std::vector<int> count;
    std::vector<int> idx;
    std::vector<int> w;        // tong trong so moi diem = 4096
};

// Thu nho: trung binh dien tich. Phong to: Catmull-Rom.
inline AxisTaps MakeAxisTaps(int src_n, int dst_n) {
    AxisTaps t;
    t.start.resize(dst_n);
    t.count.resize(dst_n);
    const double scale = (double)src_n / dst_n;
    for (int i = 0; i < dst_n; i++) {
        std::vector<std::pair<int, double>> taps;
        if (scale > 1.0) {
            // vung nguon [a, b) phu boi diem dich i
            double a = i * scale, b = (i + 1) * scale;
            for (int s = (int)a; s < (int)b + 1 && s < src_n; s++) {
                double ov = std::min(b, (double)s + 1) - std::max(a, (double)s);
                if (ov > 0) taps.push_back({s, ov});
            }
        } else {
            double c = (i + 0.5) * scale - 0.5;  // tam diem dich trong toa do nguon
            int base = (int)(c >= 0 ? c : c - 1);  // floor
            double f = c - base;
            for (int k = -1; k <= 2; k++) {
                double x = k - f, ax = x < 0 ? -x : x, wt;
                if (ax < 1) wt = 1.5 * ax * ax * ax - 2.5 * ax * ax + 1;
                else if (ax < 2) wt = -0.5 * ax * ax * ax + 2.5 * ax * ax - 4 * ax + 2;
                else wt = 0;
                int s = std::clamp(base + k, 0, src_n - 1);
                if (wt != 0) taps.push_back({s, wt});
            }
        }
        double sum = 0;
        for (auto& p : taps) sum += p.second;
        t.start[i] = (int)t.idx.size();
        t.count[i] = (int)taps.size();
        int acc = 0;
        for (size_t k = 0; k < taps.size(); k++) {
            int wq = (k + 1 == taps.size()) ? 4096 - acc : (int)(taps[k].second / sum * 4096 + (taps[k].second >= 0 ? 0.5 : -0.5));
            acc += wq;
            t.idx.push_back(taps[k].first);
            t.w.push_back(wq);
        }
    }
    return t;
}

// Logo 1-bit (stride byte/hang, bit cao = pixel trai) -> RGB565 xam (dw x dh), luon thu nho.
inline void ScaleLogo1bppToRgb565(const uint8_t* src, int sw, int sh, int stride, uint16_t* dst, int dw,
                                  int dh) {
    AxisTaps tx = MakeAxisTaps(sw, dw);
    AxisTaps ty = MakeAxisTaps(sh, dh);
    std::vector<int> row(dw);  // gia tri 1 hang nguon da gom theo x (0..4096*255)
    std::vector<int64_t> acc(dw);
    for (int y = 0; y < dh; y++) {
        std::fill(acc.begin(), acc.end(), 0);
        for (int ky = 0; ky < ty.count[y]; ky++) {
            int sy = ty.idx[ty.start[y] + ky];
            int wy = ty.w[ty.start[y] + ky];
            const uint8_t* line = src + (size_t)sy * stride;
            for (int x = 0; x < dw; x++) {
                int v = 0;
                for (int kx = 0; kx < tx.count[x]; kx++) {
                    int sx = tx.idx[tx.start[x] + kx];
                    if (line[sx >> 3] & (0x80 >> (sx & 7))) v += tx.w[tx.start[x] + kx];
                }
                acc[x] += (int64_t)v * wy;
            }
        }
        for (int x = 0; x < dw; x++) {
            int v = (int)((acc[x] * 255 + (1LL << 23)) >> 24);  // /4096/4096
            dst[y * dw + x] = Gray565(std::clamp(v, 0, 255));
        }
    }
}

// Hinh nen RGB888 (sw x sh) -> RGB565 (dw x dh): gian vua khit, giu tron anh, khong xoay, khong cat.
//  1) co gian 2 luot (ngang roi doc) o do sau 8-bit/kenh
//  2) neu phong to: lam net (unsharp mask 3x3) cho duong net ro hon
//  3) chuyen RGB565 co dither (Bayer 4x4) de khong bi soc mang mau
// sharpen_percent: muc lam net khi phong to (0 = tat). Tra ve false neu thieu RAM.
inline bool ScaleWallpaperRgb888(const uint8_t* src, int sw, int sh, uint16_t* dst, int dw, int dh,
                                 int sharpen_percent = 60) {
    AxisTaps tx = MakeAxisTaps(sw, dw);
    AxisTaps ty = MakeAxisTaps(sh, dh);
    uint8_t* mid = (uint8_t*)malloc((size_t)dw * sh * 3);
    uint8_t* out = (uint8_t*)malloc((size_t)dw * dh * 3);
    if (mid == nullptr || out == nullptr) {
        free(mid);
        free(out);
        return false;
    }
    for (int y = 0; y < sh; y++) {
        const uint8_t* line = src + (size_t)y * sw * 3;
        for (int x = 0; x < dw; x++) {
            int acc[3] = {0, 0, 0};
            for (int k = 0; k < tx.count[x]; k++) {
                const uint8_t* p = line + (size_t)tx.idx[tx.start[x] + k] * 3;
                int wq = tx.w[tx.start[x] + k];
                acc[0] += p[0] * wq;
                acc[1] += p[1] * wq;
                acc[2] += p[2] * wq;
            }
            uint8_t* m = mid + ((size_t)y * dw + x) * 3;
            for (int c = 0; c < 3; c++) m[c] = (uint8_t)std::clamp((acc[c] + 2048) >> 12, 0, 255);
        }
    }
    for (int y = 0; y < dh; y++) {
        for (int x = 0; x < dw; x++) {
            int acc[3] = {0, 0, 0};
            for (int k = 0; k < ty.count[y]; k++) {
                const uint8_t* m = mid + ((size_t)ty.idx[ty.start[y] + k] * dw + x) * 3;
                int wq = ty.w[ty.start[y] + k];
                acc[0] += m[0] * wq;
                acc[1] += m[1] * wq;
                acc[2] += m[2] * wq;
            }
            uint8_t* o = out + ((size_t)y * dw + x) * 3;
            for (int c = 0; c < 3; c++) o[c] = (uint8_t)std::clamp((acc[c] + 2048) >> 12, 0, 255);
        }
    }
    free(mid);

    // muc lam net: chi khi phong to (theo chieu phong to nhieu nhat)
    int amount = 0;
    if (sharpen_percent > 0 && (dw > sw || dh > sh)) {
        int up = std::max(dw * 100 / sw, dh * 100 / sh);  // 125 = phong to 1.25 lan
        amount = std::min(sharpen_percent, sharpen_percent * (up - 100) / 50);
    }
    static const int kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    for (int y = 0; y < dh; y++) {
        for (int x = 0; x < dw; x++) {
            const uint8_t* o = out + ((size_t)y * dw + x) * 3;
            int v[3] = {o[0], o[1], o[2]};
            if (amount > 0) {
                for (int c = 0; c < 3; c++) {
                    int sum = 0, wsum = 0;
                    for (int ky = -1; ky <= 1; ky++) {
                        int yy = std::clamp(y + ky, 0, dh - 1);
                        for (int kx = -1; kx <= 1; kx++) {
                            int xx = std::clamp(x + kx, 0, dw - 1);
                            int w = (ky == 0 ? 2 : 1) * (kx == 0 ? 2 : 1);
                            sum += out[((size_t)yy * dw + xx) * 3 + c] * w;
                            wsum += w;
                        }
                    }
                    int blur = sum / wsum;
                    v[c] = std::clamp(v[c] + (v[c] - blur) * amount / 100, 0, 255);
                }
            }
            // dither: nguong theo Bayer, buoc 8 (R,B 5-bit) va 4 (G 6-bit)
            int t = kBayer[y & 3][x & 3];
            int r = std::min(255, v[0] + (t * 8) / 16);
            int g = std::min(255, v[1] + (t * 4) / 16);
            int b = std::min(255, v[2] + (t * 8) / 16);
            dst[(size_t)y * dw + x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
    free(out);
    return true;
}

}  // namespace multi

#endif  // _MULTI_LOGO_SCALE_H_

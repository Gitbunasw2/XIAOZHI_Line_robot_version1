// Ve len man OLED don sac 128x64 (SSD1306/SH1106) KHONG qua LVGL.
// Bo dem dang "trang" cua SSD1306: buf[(y/8) * W + x], bit (y % 8).
// Khong phu thuoc phan cung -> chay thu duoc tren may tinh.
//  - Font 5x7 ASCII, chu tieng Viet duoc bo dau (Utf8ToAscii)
//  - Mat robot theo cam xuc (DrawEyes)
#ifndef _MULTI_OLED_CANVAS_H_
#define _MULTI_OLED_CANVAS_H_

#include <stdint.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

namespace oled {

// Font 5x7 ky tu 0x20..0x7E, moi ky tu 5 cot, bit 0 = hang tren cung
inline constexpr uint8_t kFont5x7[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00}, {0x00, 0x07, 0x00, 0x07, 0x00},
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, {0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00}, {0x00, 0x1C, 0x22, 0x41, 0x00},
    {0x00, 0x41, 0x22, 0x1C, 0x00}, {0x14, 0x08, 0x3E, 0x08, 0x14}, {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08}, {0x00, 0x60, 0x60, 0x00, 0x00},
    {0x20, 0x10, 0x08, 0x04, 0x02}, {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31}, {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}, {0x00, 0x36, 0x36, 0x00, 0x00},
    {0x00, 0x56, 0x36, 0x00, 0x00}, {0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x51, 0x09, 0x06}, {0x32, 0x49, 0x79, 0x41, 0x3E},
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}, {0x00, 0x7F, 0x41, 0x41, 0x00},
    {0x02, 0x04, 0x08, 0x10, 0x20}, {0x00, 0x41, 0x41, 0x7F, 0x00}, {0x04, 0x02, 0x01, 0x02, 0x04},
    {0x40, 0x40, 0x40, 0x40, 0x40}, {0x00, 0x01, 0x02, 0x04, 0x00}, {0x20, 0x54, 0x54, 0x54, 0x78},
    {0x7F, 0x48, 0x44, 0x44, 0x38}, {0x38, 0x44, 0x44, 0x44, 0x20}, {0x38, 0x44, 0x44, 0x48, 0x7F},
    {0x38, 0x54, 0x54, 0x54, 0x18}, {0x08, 0x7E, 0x09, 0x01, 0x02}, {0x0C, 0x52, 0x52, 0x52, 0x3E},
    {0x7F, 0x08, 0x04, 0x04, 0x78}, {0x00, 0x44, 0x7D, 0x40, 0x00}, {0x20, 0x40, 0x44, 0x3D, 0x00},
    {0x7F, 0x10, 0x28, 0x44, 0x00}, {0x00, 0x41, 0x7F, 0x40, 0x00}, {0x7C, 0x04, 0x18, 0x04, 0x78},
    {0x7C, 0x08, 0x04, 0x04, 0x78}, {0x38, 0x44, 0x44, 0x44, 0x38}, {0x7C, 0x14, 0x14, 0x14, 0x08},
    {0x08, 0x14, 0x14, 0x18, 0x7C}, {0x7C, 0x08, 0x04, 0x04, 0x08}, {0x48, 0x54, 0x54, 0x54, 0x20},
    {0x04, 0x3F, 0x44, 0x40, 0x20}, {0x3C, 0x40, 0x40, 0x20, 0x7C}, {0x1C, 0x20, 0x40, 0x20, 0x1C},
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, {0x44, 0x28, 0x10, 0x28, 0x44}, {0x0C, 0x50, 0x50, 0x50, 0x3C},
    {0x44, 0x64, 0x54, 0x4C, 0x44}, {0x00, 0x08, 0x36, 0x41, 0x00}, {0x00, 0x00, 0x7F, 0x00, 0x00},
    {0x00, 0x41, 0x36, 0x08, 0x00}, {0x02, 0x01, 0x02, 0x04, 0x02},
};

// UTF-8 -> ASCII: chu tieng Viet / Latin co dau -> chu khong dau, ky tu khac bo qua.
inline std::string Utf8ToAscii(const char* s) {
    // U+00C0..U+017F
    static const char kLatin[] =
        "AAAAAA?CEEEEIIII?NOOOOO??UUUUY??aaaaaa?ceeeeiiii?nooooo??uuuuy?yAaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHh??"
        "IiIiIiIiI???JjKk?LlLlLl????NnNnNn???OoOoOo??RrRrRrSsSsSsSsTtTt??UuUuUuUuUuUuWwYyYZzZzZz?";
    // U+1EA0..U+1EF9
    static const char kViet[] =
        "AaAaAaAaAaAaAaAaAaAaAaAaEeEeEeEeEeEeEeEeIiIiOoOoOoOoOoOoOoOoOoOoOoOoUuUuUuUuUuUuUuYyYyYyYy";
    std::string out;
    if (s == nullptr) return out;
    const uint8_t* p = (const uint8_t*)s;
    while (*p) {
        uint32_t cp;
        int n;
        if (*p < 0x80) { cp = *p; n = 1; }
        else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; n = 2; }
        else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; n = 3; }
        else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; n = 4; }
        else { p++; continue; }
        int k = 1;
        for (; k < n; k++) {
            if ((p[k] & 0xC0) != 0x80) break;
            cp = (cp << 6) | (p[k] & 0x3F);
        }
        if (k < n) { p += k; continue; }  // chuoi UTF-8 hong
        p += n;

        char c = 0;
        if (cp == '\n' || cp == '\r' || cp == '\t') c = ' ';
        else if (cp >= 0x20 && cp < 0x7F) c = (char)cp;
        else if (cp >= 0xC0 && cp <= 0x17F) c = kLatin[cp - 0xC0];
        else if (cp == 0x1A0) c = 'O';
        else if (cp == 0x1A1) c = 'o';
        else if (cp == 0x1AF) c = 'U';
        else if (cp == 0x1B0) c = 'u';
        else if (cp >= 0x1EA0 && cp <= 0x1EF9) c = kViet[cp - 0x1EA0];
        else if (cp == 0x2018 || cp == 0x2019) c = '\'';
        else if (cp == 0x201C || cp == 0x201D) c = '"';
        else if (cp == 0x2013 || cp == 0x2014) c = '-';
        else if (cp == 0x2026) { out += "..."; continue; }
        if (c == '?' || c == 0) continue;  // dau ghep, emoji, chu Han... -> bo
        if (c == ' ' && (out.empty() || out.back() == ' ')) continue;
        out += c;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// Ngat dong theo tu, moi dong toi da max_chars ky tu
inline std::vector<std::string> WrapText(const std::string& text, size_t max_chars) {
    std::vector<std::string> lines;
    std::string line;
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find(' ', i);
        if (j == std::string::npos) j = text.size();
        std::string word = text.substr(i, j - i);
        i = j + 1;
        while (word.size() > max_chars) {  // tu qua dai -> cat
            if (!line.empty()) { lines.push_back(line); line.clear(); }
            lines.push_back(word.substr(0, max_chars));
            word = word.substr(max_chars);
        }
        if (word.empty()) continue;
        if (line.empty()) line = word;
        else if (line.size() + 1 + word.size() <= max_chars) line += " " + word;
        else { lines.push_back(line); line = word; }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

class Canvas {
public:
    static constexpr int W = 128;
    Canvas(int height = 64) : h_(height) { buf_.assign(W * h_ / 8, 0); }

    int Height() const { return h_; }
    const uint8_t* Data() const { return buf_.data(); }
    size_t Size() const { return buf_.size(); }
    void Clear() { std::fill(buf_.begin(), buf_.end(), 0); }

    bool Get(int x, int y) const {
        if (x < 0 || x >= W || y < 0 || y >= h_) return false;
        return buf_[(y >> 3) * W + x] & (1 << (y & 7));
    }
    void Pixel(int x, int y, bool on = true) {
        if (x < 0 || x >= W || y < 0 || y >= h_) return;
        uint8_t& b = buf_[(y >> 3) * W + x];
        if (on) b |= (1 << (y & 7));
        else b &= ~(1 << (y & 7));
    }
    void HLine(int x0, int x1, int y, bool on = true) {
        if (x0 > x1) std::swap(x0, x1);
        for (int x = x0; x <= x1; x++) Pixel(x, y, on);
    }
    void FillRect(int x, int y, int w, int h, bool on = true) {
        for (int j = y; j < y + h; j++) HLine(x, x + w - 1, j, on);
    }
    void Rect(int x, int y, int w, int h, bool on = true) {
        HLine(x, x + w - 1, y, on);
        HLine(x, x + w - 1, y + h - 1, on);
        for (int j = y; j < y + h; j++) { Pixel(x, j, on); Pixel(x + w - 1, j, on); }
    }
    void Line(int x0, int y0, int x1, int y1, bool on = true) {
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        while (true) {
            Pixel(x0, y0, on);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
    void FillCircle(int cx, int cy, int r, bool on = true) {
        for (int y = -r; y <= r; y++) {
            int w = 0;
            while ((w + 1) * (w + 1) + y * y <= r * r + r) w++;
            HLine(cx - w, cx + w, cy + y, on);
        }
    }
    // Hinh chu nhat bo goc tron (ban kinh r)
    void FillRoundRect(int x, int y, int w, int h, int r, bool on = true) {
        r = std::min(r, std::min(w, h) / 2);
        for (int j = 0; j < h; j++) {
            int inset = 0;
            int dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
            if (dy > 0) {
                int dx = 0;
                while ((dx + 1) * (dx + 1) + dy * dy <= r * r + r) dx++;
                inset = r - dx;
            }
            HLine(x + inset, x + w - 1 - inset, y + j, on);
        }
    }
    void FillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, bool on = true) {
        int miny = std::min({y0, y1, y2}), maxy = std::max({y0, y1, y2});
        for (int y = miny; y <= maxy; y++) {
            int xs[3], n = 0;
            const int px[3] = {x0, x1, x2}, py[3] = {y0, y1, y2};
            for (int e = 0; e < 3; e++) {
                int ax = px[e], ay = py[e], bx = px[(e + 1) % 3], by = py[(e + 1) % 3];
                if (ay == by) continue;
                if ((y >= std::min(ay, by)) && (y <= std::max(ay, by))) {
                    if (n < 3) xs[n++] = ax + (bx - ax) * (y - ay) / (by - ay);
                }
            }
            if (n == 0) continue;
            int lo = *std::min_element(xs, xs + n), hi = *std::max_element(xs, xs + n);
            HLine(lo, hi, y, on);
        }
    }

    // Chu 5x7 (scale 1 = 6 px moi ky tu). Tra ve x sau chuoi.
    int Text(int x, int y, const std::string& s, int scale = 1, bool on = true) {
        for (char ch : s) {
            uint8_t c = (uint8_t)ch;
            if (c < 0x20 || c > 0x7E) c = '?';
            const uint8_t* g = kFont5x7[c - 0x20];
            for (int col = 0; col < 5; col++)
                for (int row = 0; row < 7; row++)
                    if (g[col] & (1 << row)) FillRect(x + col * scale, y + row * scale, scale, scale, on);
            x += 6 * scale;
        }
        return x;
    }
    static int TextWidth(const std::string& s, int scale = 1) { return (int)s.size() * 6 * scale - scale; }

private:
    int h_;
    std::vector<uint8_t> buf_;
};

// ---------------- Mat robot ----------------
enum class Eyes { Normal, Happy, Sad, Cry, Angry, Surprised, Love, Sleepy, Wink, Thinking, Cool, Confused };

inline Eyes EyesFromEmotion(const std::string& e) {
    if (e == "happy" || e == "laughing" || e == "funny" || e == "delicious" || e == "silly") return Eyes::Happy;
    if (e == "sad") return Eyes::Sad;
    if (e == "crying") return Eyes::Cry;
    if (e == "angry") return Eyes::Angry;
    if (e == "surprised" || e == "shocked") return Eyes::Surprised;
    if (e == "loving" || e == "kissy") return Eyes::Love;
    if (e == "sleepy") return Eyes::Sleepy;
    if (e == "winking") return Eyes::Wink;
    if (e == "thinking") return Eyes::Thinking;
    if (e == "cool") return Eyes::Cool;
    if (e == "confused" || e == "embarrassed") return Eyes::Confused;
    return Eyes::Normal;
}

// Ve 2 mat trong vung [top, top+h). blink 0..1 (1 = nham han), look_x/look_y: lech nhin (px).
inline void DrawEyes(Canvas& c, Eyes style, int top, int h, float blink = 0, int look_x = 0, int look_y = 0) {
    const int ew = 30, eh = std::min(26, h - 2);
    const int cy = top + h / 2 + look_y;
    const int cxs[2] = {36 + look_x, 92 + look_x};

    auto open_eye = [&](int cx, int height) {
        int hh = std::max(2, (int)(height * (1.0f - blink)));
        c.FillRoundRect(cx - ew / 2, cy - hh / 2, ew, hh, 7);
    };
    auto arc_eye = [&](int cx) {  // mat cuoi ^
        c.FillCircle(cx, cy + 6, 15);
        c.FillCircle(cx, cy + 12, 14, false);
        c.FillRect(cx - 16, cy + 8, 33, 16, false);
    };
    auto heart = [&](int cx) {
        c.FillCircle(cx - 7, cy - 4, 8);
        c.FillCircle(cx + 7, cy - 4, 8);
        c.FillTriangle(cx - 15, cy - 1, cx + 15, cy - 1, cx, cy + 13);
    };

    switch (style) {
        case Eyes::Happy:
            for (int cx : cxs) arc_eye(cx);
            break;
        case Eyes::Sad:
        case Eyes::Cry:
            for (int i = 0; i < 2; i++) {
                int cx = cxs[i], x0 = cx - ew / 2, y0 = cy - eh / 2;
                open_eye(cx, eh);
                // ha goc ngoai xuong (mat buon)
                if (i == 0) c.FillTriangle(x0 - 1, y0 - 1, x0 + ew, y0 - 1, x0 - 1, y0 + eh / 2, false);
                else c.FillTriangle(x0 - 1, y0 - 1, x0 + ew, y0 - 1, x0 + ew, y0 + eh / 2, false);
            }
            if (style == Eyes::Cry) {  // giot nuoc mat
                for (int i = 0; i < 2; i++) {
                    int tx = i == 0 ? cxs[0] - ew / 2 - 5 : cxs[1] + ew / 2 + 5, ty = cy;
                    c.FillCircle(tx, ty + 3, 2);
                    c.Pixel(tx, ty);
                    c.HLine(tx - 1, tx + 1, ty + 1);
                }
            }
            break;
        case Eyes::Angry:
            for (int i = 0; i < 2; i++) {
                int cx = cxs[i], x0 = cx - ew / 2, y0 = cy - eh / 2;
                open_eye(cx, eh);
                // ha goc trong xuong (cau may)
                if (i == 0) c.FillTriangle(x0 - 1, y0 - 1, x0 + ew, y0 - 1, x0 + ew, y0 + eh / 2, false);
                else c.FillTriangle(x0 - 1, y0 - 1, x0 + ew, y0 - 1, x0 - 1, y0 + eh / 2, false);
            }
            break;
        case Eyes::Surprised:
            for (int cx : cxs) {
                int r = std::min(13, eh / 2);
                c.FillCircle(cx, cy, r);
                c.FillCircle(cx, cy, r - 4, false);
                c.FillCircle(cx, cy, std::max(2, r - 9));
            }
            break;
        case Eyes::Love:
            for (int cx : cxs) heart(cx);
            break;
        case Eyes::Sleepy:
            for (int cx : cxs) c.FillRoundRect(cx - ew / 2, cy + 2, ew, 4, 2);
            c.Text(112, top, "z");
            c.Text(118, top - 3 < 0 ? top : top - 3, "Z");
            break;
        case Eyes::Wink:
            open_eye(cxs[0], eh);
            arc_eye(cxs[1]);
            break;
        case Eyes::Thinking:  // nhin len goc tren ben phai
            for (int i = 0; i < 2; i++) {
                int hh = std::max(2, (int)((eh * 6 / 10) * (1.0f - blink)));
                c.FillRoundRect(cxs[i] - ew / 2 + 6, cy - 5 - hh / 2, ew - 6, hh, 6);
            }
            c.Text(110, top, "...");
            break;
        case Eyes::Cool: {
            int y0 = cy - 9;
            c.FillRect(cxs[0] - ew / 2 - 4, y0, cxs[1] - cxs[0] + ew + 8, 3);
            for (int cx : cxs) {
                c.FillRoundRect(cx - ew / 2 - 1, y0, ew + 2, 15, 5);
                c.Line(cx - 9, y0 + 4, cx - 4, y0 + 4, false);  // anh phan chieu
                c.Line(cx - 9, y0 + 5, cx - 6, y0 + 5, false);
            }
            break;
        }
        case Eyes::Confused:
            open_eye(cxs[0], eh);
            open_eye(cxs[1], eh * 6 / 10);
            break;
        default:
            for (int cx : cxs) open_eye(cx, eh);
            break;
    }
}

// Cot song Wi-Fi (4 vach), rssi_level 0..4, -1 = mat ket noi (ve dau x)
inline void DrawWifi(Canvas& c, int x, int y, int level) {
    if (level < 0) {
        c.Line(x, y, x + 5, y + 5);
        c.Line(x, y + 5, x + 5, y);
        return;
    }
    for (int i = 0; i < 4; i++) {
        int bh = 2 + i * 2;
        if (i < level) c.FillRect(x + i * 3, y + 7 - bh, 2, bh);
        else c.Pixel(x + i * 3, y + 6);
    }
}

// Bieu tuong pin 14x7, level 0..100, charging -> ve tia set
inline void DrawBattery(Canvas& c, int x, int y, int level, bool charging) {
    c.Rect(x, y, 12, 7);
    c.FillRect(x + 12, y + 2, 2, 3);
    int fill = std::clamp(level, 0, 100) * 10 / 100;
    c.FillRect(x + 1, y + 1, fill, 5);
    if (charging) {
        c.Line(x + 7, y + 1, x + 4, y + 3, fill < 7);
        c.Line(x + 4, y + 3, x + 7, y + 3, fill < 7);
        c.Line(x + 7, y + 3, x + 4, y + 5, fill < 7);
    }
}

// Noi dung 1 khung hinh OLED phu
struct Frame {
    std::string time;          // "12:34" (rong = chua co gio)
    std::string state;         // "Dang nghe", "Dang noi"...
    int wifi = -1;             // 0..4, -1 = mat Wi-Fi
    int battery = -1;          // 0..100, -1 = khong co pin
    bool charging = false;
    int distance_cm = -2;      // -2 = khong co cam bien, -1 = loi doc
    Eyes eyes = Eyes::Normal;
    float blink = 0;
    int look_x = 0, look_y = 0;
    std::string line1, line2;  // 2 dong chu (AI noi / thong bao)
};

// Bo cuc 128x64:
//   y 0..6   : gio | trang thai | Wi-Fi | pin
//   y 10..37 : 2 mat robot
//   y 40..46 : khoang cach HC-SR04
//   y 48..62 : 2 dong chu AI dang noi
// Man 128x32: chi gio/Wi-Fi/pin + 1 dong mat nho + 1 dong chu
inline void RenderFrame(Canvas& c, const Frame& f) {
    c.Clear();
    const bool tall = c.Height() >= 64;
    // thanh tren
    c.Text(0, 0, f.time.empty() ? "--:--" : f.time);
    int right = 127;
    if (f.battery >= 0) {
        DrawBattery(c, right - 13, 0, f.battery, f.charging);
        right -= 17;
    }
    DrawWifi(c, right - 10, 0, f.wifi);
    right -= 14;
    if (!f.state.empty()) {
        // trang thai nam giua khoang trong
        std::string st = f.state.substr(0, std::max(0, (right - 34) / 6));
        int w = Canvas::TextWidth(st);
        int x = 32 + ((right - 32) - w) / 2;
        c.Text(x, 0, st);
    }
    if (!tall) {
        DrawEyes(c, f.eyes, 9, 14, f.blink, f.look_x / 2, 0);
        c.Text(0, 25, f.line1.substr(0, 21));
        return;
    }
    DrawEyes(c, f.eyes, 10, 28, f.blink, f.look_x, f.look_y);
    if (f.distance_cm != -2) {
        std::string d = f.distance_cm < 0 ? "Vat can: --" : "Vat can: " + std::to_string(f.distance_cm) + " cm";
        c.Text((128 - Canvas::TextWidth(d)) / 2, 40, d);
    }
    c.Text(0, 48, f.line1.substr(0, 21));
    c.Text(0, 56, f.line2.substr(0, 21));
}

}  // namespace oled

#endif  // _MULTI_OLED_CANVAS_H_

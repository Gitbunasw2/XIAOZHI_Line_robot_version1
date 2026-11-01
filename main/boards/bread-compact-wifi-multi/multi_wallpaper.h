// Hinh nen sang / toi RB5320 cho MOI kich thuoc man LCD.
// Anh goc 240x320 (rb_wallpaper.c) duoc GIAN VUA KHIT man, giu tron anh, KHONG cat mep
// (co the meo), KHONG xoay (anh luon dung doc). Phong to bicubic + lam net, thu nho trung
// binh dien tich, dither khi ve RGB565. Anh da co gian nam trong PSRAM.
// Ten: rb_bg_light_888 = anh mach den (giao dien SANG), rb_bg_dark_888 = anh mach vang (TOI).
#ifndef _MULTI_WALLPAPER_H_
#define _MULTI_WALLPAPER_H_

#include "logo_scale.h"
#include "lvgl_image.h"
#include "lvgl_theme.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <lvgl.h>
#include <memory>

extern "C" {
extern const int rb_bg_width;
extern const int rb_bg_height;
extern const uint8_t rb_bg_light_888[];
extern const uint8_t rb_bg_dark_888[];
}

// W x H: kich thuoc LVGL that cua man (da tinh Hoan XY). Tra ve false neu khong du PSRAM.
inline bool ApplyRbWallpapers(int W, int H) {
    const size_t bytes = (size_t)W * H * sizeof(uint16_t);
    // can 2 anh + chua lai it nhat 1 MB PSRAM cho he thong
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < bytes * 2 + 1024 * 1024) {
        ESP_LOGW("Wallpaper", "Khong du PSRAM cho hinh nen %dx%d, bo qua", W, H);
        return false;
    }
    auto& mgr = LvglThemeManager::GetInstance();
    struct Item {
        const char* theme;
        const uint8_t* src;
    } items[] = {{"light", rb_bg_light_888}, {"dark", rb_bg_dark_888}};

    for (auto& it : items) {
        auto theme = mgr.GetTheme(it.theme);
        if (theme == nullptr) {
            continue;
        }
        auto* buf = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (buf == nullptr) {
            ESP_LOGW("Wallpaper", "Cap phat that bai cho %s", it.theme);
            return false;
        }
        if (!multi::ScaleWallpaperRgb888(it.src, rb_bg_width, rb_bg_height, buf, W, H)) {
            heap_caps_free(buf);
            ESP_LOGW("Wallpaper", "Khong du RAM de co gian %s", it.theme);
            return false;
        }
        // LvglAllocatedImage giu va tu giai phong bo dem
        auto img = std::make_shared<LvglAllocatedImage>(buf, bytes, W, H, W * 2, LV_COLOR_FORMAT_RGB565);
        theme->set_background_image(img);
    }
    ESP_LOGI("Wallpaper", "Hinh nen sang/toi %dx%d", W, H);
    return true;
}

#endif  // _MULTI_WALLPAPER_H_

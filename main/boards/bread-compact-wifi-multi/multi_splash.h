// Man hinh logo RB5320XZ78AI luc khoi dong cho MOI kich thuoc man.
// Logo goc do phan giai cao (rb_logo_1bpp.c) duoc THU NHO giu dung ti le theo kich thuoc LVGL that,
// can giua tren nen den. Tu an khi ket noi Wi-Fi hoac vao che do cau hinh.
#ifndef _MULTI_SPLASH_H_
#define _MULTI_SPLASH_H_

#include "display.h"
#include "logo_scale.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <lvgl.h>
#include <src/misc/cache/instance/lv_image_cache.h>  // lv_image_cache_drop

extern "C" {
extern const int rb_logo_width;
extern const int rb_logo_height;
extern const int rb_logo_stride;
extern const uint8_t rb_logo_1bpp[];
}

class SplashLogo {
public:
    // margin_percent: le moi canh (man tron dung le lon hon de logo nam trong vong tron)
    void Show(lv_obj_t* screen, int margin_percent) {
        if (obj_ != nullptr) {
            return;
        }
        const int W = lv_display_get_horizontal_resolution(lv_display_get_default());
        const int H = lv_display_get_vertical_resolution(lv_display_get_default());

        obj_ = lv_obj_create(screen);
        lv_obj_remove_style_all(obj_);
        lv_obj_set_size(obj_, W, H);
        lv_obj_set_pos(obj_, 0, 0);
        lv_obj_set_style_bg_color(obj_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(obj_, LV_OPA_COVER, 0);
        lv_obj_clear_flag(obj_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_move_foreground(obj_);

        auto fit = multi::FitLogo(W, H, rb_logo_width, rb_logo_height, margin_percent);
        size_t bytes = (size_t)fit.width * fit.height * sizeof(uint16_t);
        pixels_ = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (pixels_ == nullptr) {
            pixels_ = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
        }
        if (pixels_ == nullptr) {
            ESP_LOGW("Splash", "Khong du RAM cho logo %dx%d", fit.width, fit.height);
            return;  // van giu nen den
        }
        multi::ScaleLogo1bppToRgb565(rb_logo_1bpp, rb_logo_width, rb_logo_height, rb_logo_stride, pixels_,
                                     fit.width, fit.height);

        dsc_ = {};
        dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        dsc_.header.w = fit.width;
        dsc_.header.h = fit.height;
        dsc_.header.stride = fit.width * 2;
        dsc_.data_size = bytes;
        dsc_.data = (const uint8_t*)pixels_;

        lv_obj_t* img = lv_image_create(obj_);
        lv_image_set_src(img, &dsc_);
        lv_obj_center(img);
        ESP_LOGI("Splash", "Logo %dx%d tren man %dx%d", fit.width, fit.height, W, H);
    }

    void Hide() {
        if (obj_ != nullptr) {
            lv_obj_delete(obj_);
            obj_ = nullptr;
        }
        if (pixels_ != nullptr) {
            lv_image_cache_drop(&dsc_);
            heap_caps_free(pixels_);
            pixels_ = nullptr;
        }
    }

    bool visible() const { return obj_ != nullptr; }

private:
    lv_obj_t* obj_ = nullptr;
    uint16_t* pixels_ = nullptr;
    lv_image_dsc_t dsc_ = {};
};

// Gan logo vao bat ky lop man hinh LVGL nao (SpiLcdDisplay, OledDisplay...)
template <class Base>
class WithSplash : public Base {
public:
    template <class... Args>
    WithSplash(int margin_percent, Args&&... args)
        : Base(std::forward<Args>(args)...), margin_percent_(margin_percent) {}

    void SetupUI() override {
        Base::SetupUI();
        DisplayLockGuard lock(this);
        splash_.Show(lv_screen_active(), margin_percent_);
    }

    void HideSplash() {
        DisplayLockGuard lock(this);
        splash_.Hide();
    }

private:
    int margin_percent_;
    SplashLogo splash_;
};

#endif  // _MULTI_SPLASH_H_

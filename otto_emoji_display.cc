#include "otto_emoji_display.h"

#include <esp_log.h>
#include <cstring>
#include <vector>

#include "assets.h"
#include "assets/lang_config.h"
#include "display/lvgl_display/emoji_collection.h"
#include "display/lvgl_display/lvgl_image.h"
#include "display/lvgl_display/lvgl_theme.h"

#define TAG "OttoEmojiDisplay"

OttoEmojiDisplay::OttoEmojiDisplay(esp_lcd_panel_io_handle_t panel_io,
                                   esp_lcd_panel_handle_t panel,
                                   int width,
                                   int height,
                                   int offset_x,
                                   int offset_y,
                                   bool mirror_x,
                                   bool mirror_y,
                                   bool swap_xy)
    : SpiLcdDisplay(
          panel_io,
          panel,
          160,   // landscape width
          128,   // landscape height
          offset_x,
          offset_y,
          true,   // mirror_x (fixed orientation)
          false,  // mirror_y
          true    // rotate
      ) {
}

void OttoEmojiDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }

    SpiLcdDisplay::SetupUI();

    // 🔥 DO NOT FORCE SIZE → prevents zoom issue
    {
        DisplayLockGuard lock(this);
        lv_obj_center(preview_image_);
    }

    SetEmotion("staticstate");
}

void OttoEmojiDisplay::SetupPreviewImage() {
    DisplayLockGuard lock(this);

    if (preview_image_ == nullptr) {
        ESP_LOGW(TAG, "Preview image not initialized");
        return;
    }

    // 🔥 No forced size
    lv_obj_center(preview_image_);
}

void OttoEmojiDisplay::InitializeOttoEmojis() {
    ESP_LOGI(TAG, "Otto emojis handled by assets system");
}

LV_FONT_DECLARE(OTTO_ICON_FONT);

void OttoEmojiDisplay::SetStatus(const char* status) {
    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();

    DisplayLockGuard lock(this);

    if (!status) {
        ESP_LOGE(TAG, "SetStatus: status is nullptr");
        return;
    }

    if (strcmp(status, Lang::Strings::LISTENING) == 0) {
        lv_obj_set_style_text_font(status_label_, &OTTO_ICON_FONT, 0);
        lv_label_set_text(status_label_, "\xEF\x84\xB0");
        lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(network_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (strcmp(status, Lang::Strings::SPEAKING) == 0) {
        lv_obj_set_style_text_font(status_label_, &OTTO_ICON_FONT, 0);
        lv_label_set_text(status_label_, "\xEF\x80\xA8");
        lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(network_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (strcmp(status, Lang::Strings::CONNECTING) == 0) {
        lv_obj_set_style_text_font(status_label_, &OTTO_ICON_FONT, 0);
        lv_label_set_text(status_label_, "\xEF\x83\x81");
        lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (strcmp(status, Lang::Strings::STANDBY) == 0) {
        lv_obj_set_style_text_font(status_label_, text_font, 0);
        lv_label_set_text(status_label_, "");
        lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(network_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_set_style_text_font(status_label_, text_font, 0);
    lv_label_set_text(status_label_, status);
}

void OttoEmojiDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    DisplayLockGuard lock(this);

    if (preview_image_ == nullptr) {
        ESP_LOGE(TAG, "Preview image is not initialized");
        return;
    }

    // If no image → return to emoji animation
    if (image == nullptr) {
        esp_timer_stop(preview_timer_);
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        preview_image_cached_.reset();

        if (gif_controller_) {
            gif_controller_->Start();
        }
        return;
    }

    preview_image_cached_ = std::move(image);
    auto img_dsc = preview_image_cached_->image_dsc();

    // Set image
    lv_image_set_src(preview_image_, img_dsc);
    lv_image_set_rotation(preview_image_, 0);

    // 🔥 PERFECT FIT (NO ZOOM / NO CROP)
    if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {

        int img_w = img_dsc->header.w;
        int img_h = img_dsc->header.h;

        int scale_x = (256 * width_) / img_w;
        int scale_y = (256 * height_) / img_h;

        int scale = (scale_x < scale_y) ? scale_x : scale_y;

        // Small margin to avoid edge clipping
        scale = scale - 10;

        if (scale < 32) scale = 32;
        if (scale > 256) scale = 256;

        lv_image_set_scale(preview_image_, scale);

        // Center image
        lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);
    }

    // Hide emoji animation
    if (gif_controller_) {
        gif_controller_->Stop();
    }

    lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    esp_timer_stop(preview_timer_);
    ESP_ERROR_CHECK(
        esp_timer_start_once(preview_timer_, PREVIEW_IMAGE_DURATION_MS * 1000)
    );
}
#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "boards/common/adc_battery_monitor.h"
#include "boards/common/power_save_timer.h"
#include "lvgl_theme.h"
#include "board.h"
#include "assets/lang_config.h"

#include <font_awesome.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <ctime>

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include <esp_sleep.h>

#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C,
                0x04, 0x12, 0x14, 0x1f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D,
                0x0C, 0x1A, 0x14, 0x1E},
    14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif

#define TAG "CompactWifiBoardLCD"

// Idle background: animated starfield
static constexpr int kStarCount = 20;
static constexpr int kStarMaxSpeed = 2;

struct Star {
    int16_t x, y;
    int8_t speed;
    uint8_t brightness;
};

class CompactWifiBoardLCD;

class TftDisplay : public SpiLcdDisplay {
private:
    PowerSaveTimer* power_save_timer_ = nullptr;
    lv_obj_t* battery_pct_label_ = nullptr;
    lv_obj_t* bg_canvas_ = nullptr;
    lv_timer_t* anim_timer_ = nullptr;
    Star stars_[kStarCount];
    lv_color16_t* canvas_buf_ = nullptr;
    CompactWifiBoardLCD* board_ = nullptr;

    void InitStars() {
        srand((unsigned)time(NULL));
        for (int i = 0; i < kStarCount; i++) {
            stars_[i].x = rand() % DISPLAY_WIDTH;
            stars_[i].y = rand() % DISPLAY_HEIGHT;
            stars_[i].speed = 1 + rand() % kStarMaxSpeed;
            stars_[i].brightness = 80 + rand() % 176;
        }
    }

    void DrawStars() {
        if (canvas_buf_ == nullptr) return;
        // Clear with dark gradient
        for (int y = 0; y < DISPLAY_HEIGHT; y++) {
            uint8_t ratio = (uint8_t)(y * 200 / DISPLAY_HEIGHT);
            uint8_t r = 6 + ratio / 16;
            uint8_t g = 6 + ratio / 20;
            uint8_t b = 18 + ratio / 8;
            lv_color16_t c = lv_color_make(r, g, b);
            for (int x = 0; x < DISPLAY_WIDTH; x++) {
                canvas_buf_[y * DISPLAY_WIDTH + x] = c;
            }
        }
        // Draw stars
        for (int i = 0; i < kStarCount; i++) {
            Star& s = stars_[i];
            uint8_t bri = s.brightness;
            lv_color16_t star_color = lv_color_make(bri, bri, bri);
            if (s.x >= 0 && s.x < DISPLAY_WIDTH && s.y >= 0 && s.y < DISPLAY_HEIGHT) {
                canvas_buf_[s.y * DISPLAY_WIDTH + s.x] = star_color;
            }
        }
    }

    void AnimateStars() {
        for (int i = 0; i < kStarCount; i++) {
            stars_[i].y += stars_[i].speed;
            if (stars_[i].y >= DISPLAY_HEIGHT) {
                stars_[i].y = 0;
                stars_[i].x = rand() % DISPLAY_WIDTH;
                stars_[i].speed = 1 + rand() % kStarMaxSpeed;
                stars_[i].brightness = 80 + rand() % 176;
            }
        }
        DrawStars();
        if (bg_canvas_ != nullptr) {
            lv_obj_invalidate(bg_canvas_);
        }
    }

    static void AnimTimerCb(lv_timer_t* timer) {
        TftDisplay* self = (TftDisplay*)timer->user_data;
        self->AnimateStars();
    }

public:
    TftDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
               int width, int height, int offset_x, int offset_y,
               bool mirror_x, bool mirror_y, bool swap_xy)
        : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy) {}

    void SetPowerSaveTimer(PowerSaveTimer* timer) {
        power_save_timer_ = timer;
    }

    void SetBoard(CompactWifiBoardLCD* board) {
        board_ = board;
    }

    virtual void SetupUI() override {
        if (setup_ui_called_) return;
        Display::SetupUI();
        DisplayLockGuard lock(this);

        LvglTheme* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
        auto text_font = lvgl_theme->text_font()->font();
        auto icon_font = lvgl_theme->icon_font()->font();

        auto screen = lv_screen_active();
        lv_obj_set_style_text_font(screen, text_font, 0);
        lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);

        // Animated starfield background
        canvas_buf_ = (lv_color16_t*)heap_caps_malloc(
            DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(lv_color16_t), MALLOC_CAP_SPIRAM);
        if (canvas_buf_ != nullptr) {
            InitStars();
            DrawStars();

            lv_draw_buf_t draw_buf;
            lv_draw_buf_init(&draw_buf, DISPLAY_WIDTH, DISPLAY_HEIGHT, LV_COLOR_FORMAT_RGB565,
                             LV_STRIDE_AUTO, canvas_buf_, DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(lv_color16_t));

            bg_canvas_ = lv_canvas_create(screen);
            lv_canvas_set_draw_buf(bg_canvas_, &draw_buf);
            lv_obj_set_size(bg_canvas_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
            lv_obj_align(bg_canvas_, LV_ALIGN_TOP_LEFT, 0, 0);
            lv_obj_move_background(bg_canvas_);

            anim_timer_ = lv_timer_create(AnimTimerCb, 80, this);
        }

        // ----- Top bar: [WiFi] [status scrolling] [%] -----
        top_bar_ = lv_obj_create(screen);
        lv_obj_set_size(top_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(top_bar_, 0, 0);
        lv_obj_set_style_bg_color(top_bar_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(top_bar_, LV_OPA_50, 0);
        lv_obj_set_style_border_width(top_bar_, 0, 0);
        lv_obj_set_style_pad_all(top_bar_, 0, 0);
        lv_obj_set_style_pad_top(top_bar_, 1, 0);
        lv_obj_set_style_pad_bottom(top_bar_, 1, 0);
        lv_obj_set_style_pad_left(top_bar_, 2, 0);
        lv_obj_set_style_pad_right(top_bar_, 2, 0);
        lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollbar_mode(top_bar_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_align(top_bar_, LV_ALIGN_TOP_MID, 0, 0);

        // Left: WiFi icon
        network_label_ = lv_label_create(top_bar_);
        lv_label_set_text(network_label_, "");
        lv_obj_set_style_text_font(network_label_, icon_font, 0);
        lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);

        // Center: status + time scrolling (no battery icon overlap)
        status_bar_ = lv_obj_create(screen);
        lv_obj_set_size(status_bar_, LV_HOR_RES - 40, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(status_bar_, 0, 0);
        lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(status_bar_, 0, 0);
        lv_obj_set_style_pad_all(status_bar_, 0, 0);
        lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);
        lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);

        notification_label_ = lv_label_create(status_bar_);
        lv_obj_set_width(notification_label_, LV_HOR_RES - 40);
        lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);
        lv_label_set_text(notification_label_, "");
        lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

        status_label_ = lv_label_create(status_bar_);
        lv_obj_set_width(status_label_, LV_HOR_RES - 40);
        lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);
        lv_label_set_text(status_label_, "--:--");
        lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

        // Right: battery percentage only (no icon)
        battery_pct_label_ = lv_label_create(top_bar_);
        lv_label_set_text(battery_pct_label_, "--%");
        lv_obj_set_style_text_font(battery_pct_label_, text_font, 0);
        lv_obj_set_style_text_color(battery_pct_label_, lvgl_theme->text_color(), 0);

        // ----- Content area: chat messages -----
        lv_obj_update_layout(top_bar_);
        lv_coord_t bar_h = lv_obj_get_height(top_bar_);

        bottom_bar_ = lv_obj_create(screen);
        lv_obj_set_size(bottom_bar_, LV_HOR_RES, LV_VER_RES - bar_h);
        lv_obj_set_style_radius(bottom_bar_, 0, 0);
        lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(bottom_bar_, lvgl_theme->text_color(), 0);
        lv_obj_set_style_pad_all(bottom_bar_, 4, 0);
        lv_obj_set_style_border_width(bottom_bar_, 0, 0);
        lv_obj_set_scrollbar_mode(bottom_bar_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_align(bottom_bar_, LV_ALIGN_TOP_MID, 0, bar_h);

        chat_message_label_ = lv_label_create(bottom_bar_);
        lv_label_set_text(chat_message_label_, "");
        lv_obj_set_width(chat_message_label_, LV_HOR_RES - 8);
        lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(chat_message_label_, lv_color_white(), 0);
        lv_obj_align(chat_message_label_, LV_ALIGN_TOP_MID, 0, 0);

        // Low battery popup
        low_battery_popup_ = lv_obj_create(screen);
        lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 9 / 10, text_font->line_height * 2);
        lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -4);
        lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);
        lv_obj_set_style_radius(low_battery_popup_, 4, 0);
        low_battery_label_ = lv_label_create(low_battery_popup_);
        lv_label_set_text(low_battery_label_, "Low Battery");
        lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
        lv_obj_center(low_battery_label_);
        lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
    }

    virtual void UpdateStatusBar(bool update_all = false) override {
        auto& app = Application::GetInstance();

        // When idle: show "待命 HH:MM" combined as scrolling text
        if (app.GetDeviceState() == kDeviceStateIdle) {
            time_t now = time(NULL);
            struct tm* tm = localtime(&now);
            if (tm->tm_year >= 2025 - 1900) {
                char time_str[32];
                strftime(time_str, sizeof(time_str), "%H:%M", tm);
                std::string status = std::string(Lang::Strings::STANDBY) + " " + time_str;
                DisplayLockGuard lock(this);
                if (status_label_ != nullptr) {
                    lv_label_set_text(status_label_, status.c_str());
                }
            }
        }

        // Update battery percentage
        if (battery_pct_label_ != nullptr) {
            auto& board_ref = Board::GetInstance();
            int battery_level;
            bool charging, discharging;
            if (board_ref.GetBatteryLevel(battery_level, charging, discharging)) {
                DisplayLockGuard lock(this);
                char pct_str[8];
                if (charging) {
                    snprintf(pct_str, sizeof(pct_str), "+%d%%", battery_level);
                } else {
                    snprintf(pct_str, sizeof(pct_str), "%d%%", battery_level);
                }
                lv_label_set_text(battery_pct_label_, pct_str);
            }
        }

        SpiLcdDisplay::UpdateStatusBar(update_all);
    }

    virtual void SetStatus(const char* status) override {
        if (power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }
        // When listening/speaking, set status directly (overridden by UpdateStatusBar when idle)
        SpiLcdDisplay::SetStatus(status);
    }

    virtual void ShowNotification(const char* notification, int duration_ms = 3000) override {
        if (power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }
        SpiLcdDisplay::ShowNotification(notification, duration_ms);
    }

    virtual void ShowNotification(const std::string& notification, int duration_ms = 3000) override {
        if (power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }
        SpiLcdDisplay::ShowNotification(notification, duration_ms);
    }

    virtual void SetEmotion(const char* emotion) override {
        if (power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }
    }

    virtual void SetTheme(Theme* theme) override {
        if (power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }
        SpiLcdDisplay::SetTheme(theme);
        // Redraw starfield with new theme
        DrawStars();
        if (bg_canvas_ != nullptr) {
            lv_obj_invalidate(bg_canvas_);
        }
    }

    virtual void SetChatMessage(const char* role, const char* content) override {
        if (power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }
        if (chat_message_label_ != nullptr) {
            DisplayLockGuard lock(this);
            lv_label_set_text(chat_message_label_, content);
            lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    virtual void ClearChatMessages() override {
        if (power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }
        if (chat_message_label_ != nullptr) {
            DisplayLockGuard lock(this);
            lv_label_set_text(chat_message_label_, "");
        }
    }

    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override {
        if (power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }
        SpiLcdDisplay::SetPreviewImage(std::move(image));
    }

    virtual void SetPowerSaveMode(bool on) override {
        SpiLcdDisplay::SetPowerSaveMode(on);
    }
};

class CompactWifiBoardLCD : public WifiBoard {
private:
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    TftDisplay* display_ = nullptr;
    AdcBatteryMonitor* adc_battery_monitor_ = nullptr;
    PowerSaveTimer* power_save_timer_ = nullptr;

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 180, 360);
        power_save_timer_->OnEnterSleepMode([this]() {
            ESP_LOGI(TAG, "Entering sleep mode");
            auto display = GetDisplay();
            display->SetPowerSaveMode(true);
            auto backlight = GetBacklight();
            if (backlight != nullptr) {
                backlight->SetBrightness(5);
            }
        });
        power_save_timer_->OnExitSleepMode([this]() {
            ESP_LOGI(TAG, "Exiting sleep mode");
            auto display = GetDisplay();
            display->SetPowerSaveMode(false);
            auto backlight = GetBacklight();
            if (backlight != nullptr) {
                backlight->RestoreBrightness();
            }
        });
        power_save_timer_->OnShutdownRequest([this]() {
            ESP_LOGI(TAG, "Shutdown timeout reached. Entering deep sleep...");
            gpio_wakeup_enable(BOOT_BUTTON_GPIO, GPIO_INTR_LOW_LEVEL);
            esp_sleep_enable_gpio_wakeup();
            esp_deep_sleep_start();
        });
        power_save_timer_->SetEnabled(false);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#ifdef LCD_TYPE_GC9A01_SERIAL
        panel_config.vendor_config = &gc9107_vendor_config;
#endif

        display_ = new TftDisplay(panel_io, panel,
                                  DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                  DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        display_->SetPowerSaveTimer(power_save_timer_);
        display_->SetBoard(this);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            if (power_save_timer_ != nullptr) {
                power_save_timer_->WakeUp();
            }
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        volume_up_button_.OnClick([this]() {
            if (power_save_timer_ != nullptr) {
                power_save_timer_->WakeUp();
            }
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) volume = 100;
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            if (power_save_timer_ != nullptr) {
                power_save_timer_->WakeUp();
            }
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            if (power_save_timer_ != nullptr) {
                power_save_timer_->WakeUp();
            }
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) volume = 0;
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            if (power_save_timer_ != nullptr) {
                power_save_timer_->WakeUp();
            }
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
    }

public:
    CompactWifiBoardLCD() :
        boot_button_(BOOT_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializeSpi();
        InitializePowerSaveTimer();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializeTools();

        adc_battery_monitor_ = new AdcBatteryMonitor(ADC_UNIT_1, ADC_CHANNEL_9, 100000.0f, 100000.0f, GPIO_NUM_NC);

        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        if (adc_battery_monitor_ == nullptr) {
            return false;
        }

        static bool last_discharging = false;
        charging = adc_battery_monitor_->IsCharging();
        discharging = adc_battery_monitor_->IsDischarging();

        if (discharging != last_discharging) {
            if (power_save_timer_ != nullptr) {
                power_save_timer_->SetEnabled(discharging);
            }
            last_discharging = discharging;
        }

        level = adc_battery_monitor_->GetBatteryLevel();
        return true;
    }
};

DECLARE_BOARD(CompactWifiBoardLCD);

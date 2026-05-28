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
#include "display/emote_display.h"
#include "boards/common/adc_battery_monitor.h"
#include "boards/common/power_save_timer.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include <esp_sleep.h>

#if defined(CONFIG_OLED_SH1106_128X64)
#define SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
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

class DualDisplay : public SpiLcdDisplay {
private:
    emote::EmoteDisplay* oled_;
    esp_lcd_panel_handle_t oled_panel_;
    PowerSaveTimer* power_save_timer_ = nullptr;
public:
    DualDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                int width, int height, int offset_x, int offset_y,
                bool mirror_x, bool mirror_y, bool swap_xy,
                emote::EmoteDisplay* oled, esp_lcd_panel_handle_t oled_panel)
        : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy),
          oled_(oled), oled_panel_(oled_panel) {}

    void SetPowerSaveTimer(PowerSaveTimer* timer) {
        power_save_timer_ = timer;
    }

    virtual void SetStatus(const char* status) override {
        if (power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }
        SpiLcdDisplay::SetStatus(status);
        if (oled_ != nullptr) {
            oled_->SetStatus(status);
        }
    }

    virtual void ShowNotification(const char* notification, int duration_ms = 3000) override {
        if (power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }
        SpiLcdDisplay::ShowNotification(notification, duration_ms);
        if (oled_ != nullptr) {
            oled_->ShowNotification(notification, duration_ms);
        }
    }

    virtual void ShowNotification(const std::string &notification, int duration_ms = 3000) override {
        if (power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }
        SpiLcdDisplay::ShowNotification(notification, duration_ms);
        if (oled_ != nullptr) {
            oled_->ShowNotification(notification, duration_ms);
        }
    }

    virtual void SetEmotion(const char* emotion) override {
        if (power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }
        SpiLcdDisplay::SetEmotion(emotion);
        if (oled_ != nullptr) {
            oled_->SetEmotion(emotion);
        }
    }

    virtual void SetTheme(Theme* theme) override {
        if (power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }
        SpiLcdDisplay::SetTheme(theme);
        if (oled_ != nullptr) {
            oled_->SetTheme(theme);
        }
    }

    virtual void UpdateStatusBar(bool update_all = false) override {
        SpiLcdDisplay::UpdateStatusBar(update_all);
        if (oled_ != nullptr) {
            oled_->UpdateStatusBar(update_all);
        }
    }

    virtual void SetChatMessage(const char* role, const char* content) override {
        if (power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }
        SpiLcdDisplay::SetChatMessage(role, content);
        if (oled_ != nullptr) {
            oled_->SetChatMessage(role, content);
        }
    }

    virtual void ClearChatMessages() override {
        if (power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }
        SpiLcdDisplay::ClearChatMessages();
        if (oled_ != nullptr) {
            oled_->ClearChatMessages();
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
        if (oled_ != nullptr) {
            oled_->SetPowerSaveMode(on);
        }
        if (oled_panel_ != nullptr) {
            esp_lcd_panel_disp_on_off(oled_panel_, !on);
        }
    }
};

class CompactWifiBoardLCD : public WifiBoard {
private:
    Button boot_button_;
    LcdDisplay* display_;
    
    i2c_master_bus_handle_t oled_i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t oled_panel_io_ = nullptr;
    esp_lcd_panel_handle_t oled_panel_ = nullptr;
    emote::EmoteDisplay* oled_display_ = nullptr;
    AdcBatteryMonitor* adc_battery_monitor_ = nullptr;
    PowerSaveTimer* power_save_timer_ = nullptr;

    void InitializeOledI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = OLED_I2C_SDA_PIN,
            .scl_io_num = OLED_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &oled_i2c_bus_));
    }

    void InitializeOledDisplay() {
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(oled_i2c_bus_, &io_config, &oled_panel_io_));

        ESP_LOGI(TAG, "Install OLED driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

#if defined(CONFIG_OLED_SH1106_128X64)
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(oled_panel_io_, &panel_config, &oled_panel_));
#else
        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = 64,
        };
        panel_config.vendor_config = &ssd1306_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(oled_panel_io_, &panel_config, &oled_panel_));
#endif
        ESP_LOGI(TAG, "OLED driver installed");

        ESP_ERROR_CHECK(esp_lcd_panel_reset(oled_panel_));
        if (esp_lcd_panel_init(oled_panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize OLED display");
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(oled_panel_, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(oled_panel_, true));

        oled_display_ = new emote::EmoteDisplay(oled_panel_, oled_panel_io_, 128, 64);
    }

    void InitializePowerSaveTimer() {
        // -1 CPU frequency, 3 minutes (180s) to dim, 6 minutes (360s) to shutdown
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
            esp_deep_sleep_enable_gpio_wakeup(1ULL << BOOT_BUTTON_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
            esp_deep_sleep_start();
        });
        power_save_timer_->SetEnabled(false); // Enable dynamically on battery in GetBatteryLevel
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
#ifdef  LCD_TYPE_GC9A01_SERIAL
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        
        auto dual_display = new DualDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY, oled_display_, oled_panel_);
        dual_display->SetPowerSaveTimer(power_save_timer_);
        display_ = dual_display;
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
    }

    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
    }

public:
    CompactWifiBoardLCD() :
        boot_button_(BOOT_BUTTON_GPIO) {
        InitializeSpi();
        InitializeOledI2c();
        InitializeOledDisplay();
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

    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
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

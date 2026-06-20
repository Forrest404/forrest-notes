#ifndef CONFIG_H
#define CONFIG_H

#define EPD_SPI_NUM        SPI2_HOST
#define ESP32_I2C_DEV_NUM  I2C_NUM_0

#define EPD_WIDTH  200
#define EPD_HEIGHT 200
#define LVGL_SPIRAM_BUFF_LEN (EPD_WIDTH * EPD_HEIGHT * 2)

/* EPD SPI pins */
#define EPD_DC_PIN    GPIO_NUM_10
#define EPD_CS_PIN    GPIO_NUM_11
#define EPD_SCK_PIN   GPIO_NUM_12
#define EPD_MOSI_PIN  GPIO_NUM_13
#define EPD_RST_PIN   GPIO_NUM_9
#define EPD_BUSY_PIN  GPIO_NUM_8


/* Power control pins */
#define EPD_PWR_PIN     GPIO_NUM_6
#define Audio_PWR_PIN   GPIO_NUM_42
#define VBAT_PWR_PIN    GPIO_NUM_17
#define BAT_ADC_PIN     4   // ADC1_CHANNEL_3 on ESP32-S3

#define BOOT_BUTTON_PIN GPIO_NUM_0
#define PWR_BUTTON_PIN  GPIO_NUM_18

/* Deep-sleep wake-up pin */
#define ext_wakeup_pin_1 GPIO_NUM_0

/* I2C bus */
#define ESP32_I2C_SDA_PIN GPIO_NUM_47
#define ESP32_I2C_SCL_PIN GPIO_NUM_48

/* LVGL tick timing */
#define EXAMPLE_LVGL_TICK_PERIOD_MS    5
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 100

/* I2C peripheral addresses */
#define I2C_RTC_DEV_Address        0x51
#define I2C_SHTC3_DEV_Address      0x70

/* Button GPIO aliases */
#define BTN_REC      0
#define BTN_PWR      18
#define PWR_HOLD_PIN 17

/* SD-MMC pins */
#define SD_CLK  39
#define SD_CMD  41
#define SD_D0   40

/* Audio */
#define SAMPLE_RATE  16000
#define REC_BUF      (8 * 1024)
#define MAX_REC_MS   (5UL * 60UL * 1000UL)   // hard cap on a single recording (5 min)
#define REC_RING_LEN (96 * 1024)             // PSRAM ring to absorb SD write stalls
#define MIC_GAIN_DB  45.0f                   // ES7210 input gain
#define SPK_VOL_MAX  100.0f                  // ES8311 output volume ceiling

/* Storage paths */
#define NOTES_DIR  "/notes"
#define INDEX_FILE "/notes/index.csv"
#define TAG_FILE   "/notes/tags.txt"
#define MAX_TAGS   20

/* UI timing */
#define REC_HOLD_MS         350
#define BTN_LONG_MS         450     // hold threshold for "back"/secondary (lower = snappier)
#define BTN_DEBOUNCE_MS     12      // press must persist this long to count
#define LOOP_DELAY_MS       4       // main-loop poll period (was 15; lower = more responsive)
#define ULTRA_SLEEP_MS      120000UL
#define TICKER_INTERVAL_MS  950

/* Battery warning */
#define BAT_CHECK_INTERVAL_MS  30000
#define BAT_LOW_THRESHOLD      15
#define BAT_RECOVER_THRESHOLD  20

/* Time & firmware */
#define LOCAL_TIME_OFFSET_MIN  120   // UTC+2 (Germany summer). Set to your offset.
#define FIRMWARE_VERSION       "v1.0"
#define FW_VERSION             "v1.0"

#endif // CONFIG_H
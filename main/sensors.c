// sensors.c — BME280 (temp only) + BH1750 + PIR
#include "sensors.h"

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdint.h>

#define TAG "SENSORS"

// ===== I2C config =====
#define I2C_PORT        I2C_NUM_0
#define I2C_SDA         GPIO_NUM_21
#define I2C_SCL         GPIO_NUM_22
#define I2C_FREQ_HZ     100000

// ===== BH1750 =====
#define BH1750_ADDR     0x23  // change to 0x5C if ADDR tied to VCC
#define BH1750_PWR_ON   0x01
#define BH1750_RESET    0x07
#define BH1750_CONT_HI  0x10  // continuous high-res (1 lx / 1.2)

// ===== BME280 (temp) =====
#define BME280_ADDR     0x76  // change to 0x77 if SDO high

#define BME280_REG_CALIB00  0x88
#define BME280_REG_CALIB26  0xA1
#define BME280_REG_ID       0xD0
#define BME280_REG_RESET    0xE0
#define BME280_REG_CTRL_HUM 0xF2
#define BME280_REG_STATUS   0xF3
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_CONFIG   0xF5
#define BME280_REG_TEMP_MSB 0xFA

// ctrl_meas bits
#define BME280_OSRS_T_x1    (1<<5)
#define BME280_MODE_FORCED  0x01

// ===== PIR =====
#ifndef PIR_GPIO
#define PIR_GPIO GPIO_NUM_27
#endif

static volatile bool s_motion_instant = false;
static volatile bool s_motion_latched = false;

// ===== latest values =====
static float s_latest_temp_c = 0.0f;
static float s_latest_lux    = 0.0f;

// ===== BME280 calibration for temperature =====
static uint16_t dig_T1;
static int16_t  dig_T2, dig_T3;
static int32_t  t_fine;

// --------- helpers ---------
static esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
        .clk_flags = 0
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
    return i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
}

static esp_err_t i2c_write_cmd(uint8_t addr, uint8_t cmd) {
    i2c_cmd_handle_t c = i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c, (addr<<1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(c, cmd, true);
    i2c_master_stop(c);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, c, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(c);
    return err;
}

static esp_err_t i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
    i2c_cmd_handle_t c = i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c, (addr<<1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(c, reg, true);
    i2c_master_write_byte(c, val, true);
    i2c_master_stop(c);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, c, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(c);
    return err;
}

static esp_err_t i2c_read_bytes(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len) {
    i2c_cmd_handle_t c = i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c, (addr<<1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(c, reg, true);
    i2c_master_start(c);
    i2c_master_write_byte(c, (addr<<1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(c, buf, len-1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(c, buf+len-1, I2C_MASTER_NACK);
    i2c_master_stop(c);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, c, pdMS_TO_TICKS(200));
    i2c_cmd_link_delete(c);
    return err;
}

// ---- BH1750 ----
static esp_err_t bh1750_init(void) {
    esp_err_t err = i2c_write_cmd(BH1750_ADDR, BH1750_PWR_ON);
    if (err != ESP_OK) return err;
    err = i2c_write_cmd(BH1750_ADDR, BH1750_RESET);
    if (err != ESP_OK) return err;
    err = i2c_write_cmd(BH1750_ADDR, BH1750_CONT_HI);
    vTaskDelay(pdMS_TO_TICKS(200));
    return err;
}

static esp_err_t bh1750_read(float *lux) {
    uint8_t data[2] = {0};
    // In continuous mode, just read 2 bytes
    i2c_cmd_handle_t c = i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c, (BH1750_ADDR<<1) | I2C_MASTER_READ, true);
    i2c_master_read(c, data, 1, I2C_MASTER_ACK);
    i2c_master_read_byte(c, data+1, I2C_MASTER_NACK);
    i2c_master_stop(c);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, c, pdMS_TO_TICKS(200));
    i2c_cmd_link_delete(c);
    if (err != ESP_OK) return err;
    uint16_t raw = ((uint16_t)data[0] << 8) | data[1];
    float val = raw / 1.2f;
    if (lux) *lux = val;
    return ESP_OK;
}

// ---- BME280 (temperature) ----
static esp_err_t bme280_read_calib(void) {
    uint8_t buf[6];
    esp_err_t err = i2c_read_bytes(BME280_ADDR, BME280_REG_CALIB00, buf, 6);
    if (err != ESP_OK) return err;
    dig_T1 = (uint16_t)(buf[1]<<8 | buf[0]);
    dig_T2 = (int16_t)(buf[3]<<8 | buf[2]);
    dig_T3 = (int16_t)(buf[5]<<8 | buf[4]);
    return ESP_OK;
}

static esp_err_t bme280_init(void) {
    uint8_t id = 0;
    i2c_read_bytes(BME280_ADDR, BME280_REG_ID, &id, 1);
    ESP_LOGI(TAG, "BME280 ID=0x%02X", id);
    ESP_ERROR_CHECK(bme280_read_calib());
    // humidity oversampling register must be written before ctrl_meas if used; we ignore humidity.
    // set standby/filter minimal in CONFIG
    i2c_write_reg(BME280_ADDR, BME280_REG_CONFIG, 0x00);
    return ESP_OK;
}

// Returns degC in *t_c
static esp_err_t bme280_read_temp(float *t_c) {
    // set forced mode with temp oversampling x1
    ESP_ERROR_CHECK(i2c_write_reg(BME280_ADDR, BME280_REG_CTRL_MEAS, BME280_OSRS_T_x1 | BME280_MODE_FORCED));
    // wait for measurement, typically < 10ms
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t buf[3];
    esp_err_t err = i2c_read_bytes(BME280_ADDR, BME280_REG_TEMP_MSB, buf, 3);
    if (err != ESP_OK) return err;

    int32_t adc_T = ((int32_t)buf[0] << 12) | ((int32_t)buf[1] << 4) | ((buf[2] >> 4) & 0x0F);

    // compensation from datasheet
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) *
                    ((int32_t)dig_T3)) >> 14;
    t_fine = var1 + var2;
    int32_t T = (t_fine * 5 + 128) >> 8;
    if (t_c) *t_c = T / 100.0f;
    return ESP_OK;
}

// ---- PIR ----
static void IRAM_ATTR pir_isr(void *arg) {
    int level = gpio_get_level(PIR_GPIO);
    bool motion = (level != 0);
    s_motion_instant = motion;
    if (motion) {
        s_motion_latched = true;
    }
}

static void pir_init(void) {
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIR_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    int lvl = gpio_get_level(PIR_GPIO);
    s_motion_instant = (lvl != 0);
    s_motion_latched = s_motion_latched || s_motion_instant;

    static bool isr_installed = false;
    if (!isr_installed) {
        ESP_ERROR_CHECK(gpio_install_isr_service(0));
        isr_installed = true;
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIR_GPIO, pir_isr, NULL));
}

// ===== Public API =====
void sensors_init(void) {
    ESP_ERROR_CHECK(i2c_master_init());
    // try BH1750, don't fail hard
    if (bh1750_init() != ESP_OK) {
        ESP_LOGW(TAG, "BH1750 init failed");
    }
    if (bme280_init() != ESP_OK) {
        ESP_LOGW(TAG, "BME280 init failed");
    }
    pir_init();
}

void sensors_sample_tick(void) {
    // --- BH1750 ---
    float lux;
    if (bh1750_read(&lux) == ESP_OK) {
        s_latest_lux = lux;
    }

    // --- BME280 temp ---
    float t;
    if (bme280_read_temp(&t) == ESP_OK) {
        s_latest_temp_c = t;
    }

    // --- PIR poll fallback ---
    int lvl = gpio_get_level(PIR_GPIO);
    bool now_motion = (lvl != 0);
    static bool prev = false;
    if (now_motion != prev) {
        prev = now_motion;
        s_motion_instant = now_motion;
        if (now_motion) s_motion_latched = true;
        ESP_LOGI(TAG, "PIR %s", now_motion ? "HIGH (motion)" : "LOW (no motion)");
    }
}

void sensors_get_latest(float *t_c, float *lux, bool *motion_instant) {
    if (t_c) *t_c = s_latest_temp_c;
    if (lux) *lux = s_latest_lux;
    if (motion_instant) *motion_instant = s_motion_instant;
}

bool sensors_get_motion_latched(void) {
    return s_motion_latched;
}

void sensors_clear_motion_latch(void) {
    s_motion_latched = false;
}

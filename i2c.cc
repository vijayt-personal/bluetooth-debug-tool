#include "i2c.h"

#include <cstring> // For memcpy

// ESP-IDF specific includes - keep these only in the .cpp file
#include "driver/i2c_master.h"
#include "esp_check.h" // Can use ESP_RETURN_ON_ERROR for cleaner checks if desired
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h" // Required for pdMS_TO_TICKS

// Define a tag for logging, can be customized
static const char* TAG = "I2C_WRAPPER";

namespace io {

namespace {
// Helper function to map ESP-IDF error codes to the internal Result enum
I2c::Result EspErrorToResult(esp_err_t err) {
  switch (err) {
  case ESP_OK:
    return I2c::Result::kSuccess;
  case ESP_ERR_INVALID_ARG:
    return I2c::Result::kInvalidArgs;
  case ESP_ERR_TIMEOUT:
    return I2c::Result::kTimeOut;
  case ESP_ERR_NOT_FOUND: // Common for probe NACK, map to kNackAddr
  case ESP_FAIL: // General failure, often NACK or bus issue. Map to kNackAddr as a common cause.
    return I2c::Result::kNackAddr;
  case ESP_ERR_INVALID_STATE:
    return I2c::Result::kBusy; // Or kBusError if state implies hardware issue
  default:
    // Log unexpected errors if debugging is needed
    // ESP_LOGE(TAG, "Unhandled esp_err_t: %d (%s)", err, esp_err_to_name(err));
    return I2c::Result::kBusError; // Default to a general bus error
  }
}
} // namespace

I2c::I2c() : config_{}, bus_handle_(nullptr), initialized_(false) {}

I2c::~I2c() {
  if (initialized_) {
    Deinitialize(); // Ignore result in destructor
  }
}

I2c::Result I2c::Initialize(const Config& config) {
  if (initialized_) {
    ESP_LOGW(TAG, "I2C port %d already initialized.", config.port);
    return Result::kBusy;
  }

  if (config.port >= I2C_NUM_MAX) {
      ESP_LOGE(TAG, "Invalid I2C port number: %d", config.port);
      return Result::kInvalidArgs;
  }
   if (config.frequency == 0) {
      ESP_LOGE(TAG, "Invalid I2C frequency: 0 Hz");
      return Result::kInvalidArgs;
  }

  config_ = config;

  i2c_master_bus_config_t bus_conf = {};
  bus_conf.i2c_port = static_cast<i2c_port_t>(config_.port);
  bus_conf.sda_io_num = static_cast<gpio_num_t>(config_.sda_pin);
  bus_conf.scl_io_num = static_cast<gpio_num_t>(config_.scl_pin);
  bus_conf.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_conf.glitch_filter_ns = 0;
  bus_conf.flags.enable_internal_pullup = config_.pull_up;

  i2c_master_bus_handle_t obtained_handle = nullptr;
  esp_err_t err = i2c_new_master_bus(&bus_conf, &obtained_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create I2C master bus (port %d): %s", config_.port, esp_err_to_name(err));
    bus_handle_ = nullptr;
    return EspErrorToResult(err);
  }

  bus_handle_ = static_cast<void*>(obtained_handle); // Store as void*
  initialized_ = true;
  ESP_LOGI(TAG, "I2C master bus (port %d) initialized successfully.", config_.port);
  return Result::kSuccess;
}

I2c::Result I2c::Deinitialize() {
  if (!initialized_ || bus_handle_ == nullptr) {
    initialized_ = false;
    bus_handle_ = nullptr;
    return Result::kSuccess;
  }

  i2c_master_bus_handle_t actual_bus_handle = static_cast<i2c_master_bus_handle_t>(bus_handle_);
  esp_err_t err = i2c_del_master_bus(actual_bus_handle);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to delete I2C master bus (port %d): %s", config_.port, esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "I2C master bus (port %d) deinitialized.", config_.port);
  }

  initialized_ = false;
  bus_handle_ = nullptr;
  return EspErrorToResult(err); // Return result even if logging occurred
}


I2c::Result I2c::Write(uint8_t device_addr, const uint8_t* data, size_t length, uint32_t timeout_ms) {
  if (!initialized_ || bus_handle_ == nullptr) return Result::kBusy;
  if (data == nullptr && length > 0) return Result::kInvalidArgs;
  if (device_addr > 0x7F) return Result::kInvalidArgs;

  i2c_master_bus_handle_t actual_bus_handle = static_cast<i2c_master_bus_handle_t>(bus_handle_);
  i2c_master_dev_handle_t dev_handle = nullptr;
  esp_err_t err = ESP_FAIL; // Default to error
  esp_err_t rm_err = ESP_FAIL;

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = device_addr;
  dev_cfg.scl_speed_hz = config_.frequency;

  err = i2c_master_bus_add_device(actual_bus_handle, &dev_cfg, &dev_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Write: Failed to add device 0x%02X: %s", device_addr, esp_err_to_name(err));
    return EspErrorToResult(err);
  }

  err = i2c_master_transmit(dev_handle, data, length, pdMS_TO_TICKS(timeout_ms));
  if (err != ESP_OK) {
     ESP_LOGD(TAG, "Write to 0x%02X failed: %s", device_addr, esp_err_to_name(err));
  }

  rm_err = i2c_master_bus_rm_device(dev_handle);
  if (rm_err != ESP_OK) {
    ESP_LOGE(TAG, "Write: Failed to remove device 0x%02X handle: %s", device_addr, esp_err_to_name(rm_err));
    if (err == ESP_OK) err = rm_err; // Report remove error if transmit was OK
  }

  return EspErrorToResult(err); // Report the primary error (transmit or remove)
}


I2c::Result I2c::Read(uint8_t device_addr, uint8_t* data, size_t length, uint32_t timeout_ms) {
  if (!initialized_ || bus_handle_ == nullptr) return Result::kBusy;
  if (data == nullptr || length == 0) return Result::kInvalidArgs;
  if (device_addr > 0x7F) return Result::kInvalidArgs;

  i2c_master_bus_handle_t actual_bus_handle = static_cast<i2c_master_bus_handle_t>(bus_handle_);
  i2c_master_dev_handle_t dev_handle = nullptr;
  esp_err_t err = ESP_FAIL;
  esp_err_t rm_err = ESP_FAIL;

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = device_addr;
  dev_cfg.scl_speed_hz = config_.frequency;

  err = i2c_master_bus_add_device(actual_bus_handle, &dev_cfg, &dev_handle);
  if (err != ESP_OK) {
     ESP_LOGE(TAG, "Read: Failed to add device 0x%02X: %s", device_addr, esp_err_to_name(err));
    return EspErrorToResult(err);
  }

  err = i2c_master_receive(dev_handle, data, length, pdMS_TO_TICKS(timeout_ms));
    if (err != ESP_OK) {
     ESP_LOGD(TAG, "Read from 0x%02X failed: %s", device_addr, esp_err_to_name(err));
  }

  rm_err = i2c_master_bus_rm_device(dev_handle);
  if (rm_err != ESP_OK) {
    ESP_LOGE(TAG, "Read: Failed to remove device 0x%02X handle: %s", device_addr, esp_err_to_name(rm_err));
    if (err == ESP_OK) err = rm_err;
  }

  return EspErrorToResult(err);
}


I2c::Result I2c::WriteReg(uint8_t device_addr, uint8_t reg_addr, const uint8_t* data, size_t length, uint32_t timeout_ms) {
  if (!initialized_ || bus_handle_ == nullptr) return Result::kBusy;
  if (data == nullptr && length > 0) return Result::kInvalidArgs;
  if (device_addr > 0x7F) return Result::kInvalidArgs;

  if (length + 1 > kMaxWriteRegBufferSize) {
      ESP_LOGE(TAG, "WriteReg length (%zu) exceeds static buffer limit (%zu)", length, kMaxWriteRegBufferSize - 1);
      return Result::kInvalidArgs;
  }

  i2c_master_bus_handle_t actual_bus_handle = static_cast<i2c_master_bus_handle_t>(bus_handle_);
  i2c_master_dev_handle_t dev_handle = nullptr;
  esp_err_t err = ESP_FAIL;
  esp_err_t rm_err = ESP_FAIL;

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = device_addr;
  dev_cfg.scl_speed_hz = config_.frequency;

  err = i2c_master_bus_add_device(actual_bus_handle, &dev_cfg, &dev_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "WriteReg: Failed to add device 0x%02X: %s", device_addr, esp_err_to_name(err));
    return EspErrorToResult(err);
  }

  uint8_t temp_buffer[kMaxWriteRegBufferSize];
  temp_buffer[0] = reg_addr;
  if (length > 0) { // data checked for nullptr earlier
      memcpy(temp_buffer + 1, data, length);
  }

  err = i2c_master_transmit(dev_handle, temp_buffer, length + 1, pdMS_TO_TICKS(timeout_ms));
   if (err != ESP_OK) {
     ESP_LOGD(TAG, "WriteReg to 0x%02X, reg 0x%02X failed: %s", device_addr, reg_addr, esp_err_to_name(err));
  }

  rm_err = i2c_master_bus_rm_device(dev_handle);
  if (rm_err != ESP_OK) {
    ESP_LOGE(TAG, "WriteReg: Failed to remove device 0x%02X handle: %s", device_addr, esp_err_to_name(rm_err));
    if (err == ESP_OK) err = rm_err;
  }

  return EspErrorToResult(err);
}


I2c::Result I2c::ReadReg(uint8_t device_addr, uint8_t reg_addr, uint8_t* data, size_t length, uint32_t timeout_ms) {
  if (!initialized_ || bus_handle_ == nullptr) return Result::kBusy;
  if (data == nullptr || length == 0) return Result::kInvalidArgs;
  if (device_addr > 0x7F) return Result::kInvalidArgs;

  i2c_master_bus_handle_t actual_bus_handle = static_cast<i2c_master_bus_handle_t>(bus_handle_);
  i2c_master_dev_handle_t dev_handle = nullptr;
  esp_err_t err = ESP_FAIL;
  esp_err_t rm_err = ESP_FAIL;

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = device_addr;
  dev_cfg.scl_speed_hz = config_.frequency;

  err = i2c_master_bus_add_device(actual_bus_handle, &dev_cfg, &dev_handle);
   if (err != ESP_OK) {
     ESP_LOGE(TAG, "ReadReg: Failed to add device 0x%02X: %s", device_addr, esp_err_to_name(err));
    return EspErrorToResult(err);
  }

  err = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, data, length, pdMS_TO_TICKS(timeout_ms));
  if (err != ESP_OK) {
     ESP_LOGD(TAG, "ReadReg from 0x%02X, reg 0x%02X failed: %s", device_addr, reg_addr, esp_err_to_name(err));
  }

  rm_err = i2c_master_bus_rm_device(dev_handle);
    if (rm_err != ESP_OK) {
    ESP_LOGE(TAG, "ReadReg: Failed to remove device 0x%02X handle: %s", device_addr, esp_err_to_name(rm_err));
    if (err == ESP_OK) err = rm_err;
  }

  return EspErrorToResult(err);
}


I2c::Result I2c::ScanDevices(uint8_t* found_devices, size_t max_devices, size_t* found_count) {
  if (!initialized_ || bus_handle_ == nullptr) return Result::kBusy;
  if (found_devices == nullptr || found_count == nullptr || max_devices == 0) return Result::kInvalidArgs;

  *found_count = 0;
  i2c_master_bus_handle_t actual_bus_handle = static_cast<i2c_master_bus_handle_t>(bus_handle_);

  ESP_LOGI(TAG, "Scanning I2C bus (port %d)...", config_.port);

  for (uint8_t i = 0x08; i <= 0x77; ++i) {
    esp_err_t err = i2c_master_probe(actual_bus_handle, i, pdMS_TO_TICKS(50)); // Short timeout for probe

    if (err == ESP_OK) {
      if (*found_count < max_devices) {
        found_devices[*found_count] = i;
        (*found_count)++;
        ESP_LOGD(TAG, "  Found device at address 0x%02X", i); // Use Debug level for found devices during scan
      } else {
        ESP_LOGW(TAG, "  Found device at 0x%02X, but buffer is full (max %zu)", i, max_devices);
        // Optional: break here if buffer full is considered scan completion
      }
    } else if (err != ESP_ERR_TIMEOUT && err != ESP_ERR_NOT_FOUND && err != ESP_FAIL) {
        // Log only unexpected errors, not NACK (ESP_ERR_NOT_FOUND/ESP_FAIL) or typical timeouts
        ESP_LOGE(TAG, "  Error probing address 0x%02X: %s", i, esp_err_to_name(err));
        // Consider returning an error if a severe bus error occurs during scan
        // return EspErrorToResult(err); // Uncomment to make scan fail on bus errors
    }
  }

  ESP_LOGI(TAG, "Scan finished. Found %zu device(s).", *found_count);
  return Result::kSuccess;
}

} // namespace io

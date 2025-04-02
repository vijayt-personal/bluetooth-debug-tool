#include "i2c.h" // Your header file

#include <cstring> // For memcpy

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h" // For pdMS_TO_TICKS

// Define a tag for logging, can be customized
static const char* TAG = "I2C_WRAPPER";

// Define a maximum size for the temporary buffer used in WriteReg.
// Adjust this based on the maximum expected register write transaction size.
// This avoids Variable Length Arrays (VLAs) which are not standard C++.
constexpr size_t kMaxWriteRegBufferSize = 128;

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
    case ESP_ERR_NOT_FOUND: // Often indicates NACK on address probe
      return I2c::Result::kNackAddr;
    case ESP_FAIL: // General failure, often NACK or bus contention
                   // Mapping ESP_FAIL is tricky. Let's map it generally
                   // to kBusError, though it could be kNackAddr/kNackData too.
                   // Specific functions might give better context.
      return I2c::Result::kBusError;
    case ESP_ERR_INVALID_STATE: // E.g., driver not installed
      return I2c::Result::kBusError; // Or map to a specific error if needed
    // Add other specific ESP_ERR mappings if needed
    default:
      // Log unexpected errors if desired
      // ESP_LOGE(TAG, "Unhandled esp_err_t: %d (%s)", err, esp_err_to_name(err));
      return I2c::Result::kBusError; // Default to a general bus error
  }
}
} // namespace

I2c::I2c() : config_{}, bus_handle_(nullptr), initialized_(false) {
  // Constructor initializes members to safe default values
}

I2c::~I2c() {
  // Ensure resources are released if the object is destroyed
  // Ignore return value in destructor as exceptions are generally avoided
  if (initialized_) {
    Deinitialize();
  }
}

I2c::Result I2c::Initialize(const Config& config) {
  if (initialized_) {
    ESP_LOGW(TAG, "I2C port %d already initialized.", config.port);
    // Decide if re-initialization should be an error or idempotent
    // Returning kSuccess makes it idempotent if config is the same,
    // but could hide issues if config differs. kBusy indicates state conflict.
    return Result::kBusy; // Or return kSuccess if idempotent is preferred
  }

  // Check for valid arguments (basic checks)
  if (config.port >= I2C_NUM_MAX) {
      ESP_LOGE(TAG, "Invalid I2C port number: %d", config.port);
      return Result::kInvalidArgs;
  }
   if (config.frequency == 0) {
      ESP_LOGE(TAG, "Invalid I2C frequency: 0 Hz");
      return Result::kInvalidArgs;
  }

  config_ = config; // Store configuration

  i2c_master_bus_config_t bus_conf = {}; // Use designated initializer
  bus_conf.i2c_port = static_cast<i2c_port_t>(config_.port);
  bus_conf.sda_io_num = static_cast<gpio_num_t>(config_.sda_pin);
  bus_conf.scl_io_num = static_cast<gpio_num_t>(config_.scl_pin);
  bus_conf.clk_source = I2C_CLK_SRC_DEFAULT; // Or choose another clock source
  bus_conf.glitch_filter_ns = 0; // No filter by default, adjust if needed
  bus_conf.flags.enable_internal_pullup = config_.pull_up;

  // Frequency is set per-device later, but can set a bus default if needed
  // bus_conf.trans_queue_depth = 0; // 0 means driver selects default

  esp_err_t err = i2c_new_master_bus(&bus_conf, &bus_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create I2C master bus (port %d): %s", config_.port,
             esp_err_to_name(err));
    bus_handle_ = nullptr;
    return EspErrorToResult(err);
  }

  initialized_ = true;
  ESP_LOGI(TAG, "I2C master bus (port %d) initialized successfully.", config_.port);
  return Result::kSuccess;
}

I2c::Result I2c::Deinitialize() {
  if (!initialized_ || bus_handle_ == nullptr) {
    // Not initialized or already deinitialized
    initialized_ = false; // Ensure state consistency
    bus_handle_ = nullptr;
    return Result::kSuccess; // Deinitializing an uninitialized bus is okay
  }

  esp_err_t err = i2c_del_master_bus(bus_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to delete I2C master bus (port %d): %s", config_.port,
             esp_err_to_name(err));
    // Even on error, mark as uninitialized to prevent further use
    initialized_ = false;
    bus_handle_ = nullptr;
    return EspErrorToResult(err);
  }

  ESP_LOGI(TAG, "I2C master bus (port %d) deinitialized.", config_.port);
  initialized_ = false;
  bus_handle_ = nullptr;
  // Reset config if desired, though not strictly necessary
  // config_ = {};
  return Result::kSuccess;
}

// **************************************************************************
// Corrected Signature: const uint8_t data -> const uint8_t* data
// **************************************************************************
I2c::Result I2c::Write(uint8_t device_addr, const uint8_t* data, size_t length,
                       uint32_t timeout_ms) {
  if (!initialized_ || bus_handle_ == nullptr) {
    return Result::kBusy; // Or another suitable error like kBusError
  }
  if (data == nullptr && length > 0) {
    return Result::kInvalidArgs;
  }
  if (device_addr > 0x7F) { // Validate 7-bit address
      return Result::kInvalidArgs;
  }

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = device_addr;
  dev_cfg.scl_speed_hz = config_.frequency; // Use configured frequency

  i2c_master_dev_handle_t dev_handle = nullptr;
  esp_err_t err = i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add device 0x%02X: %s", device_addr, esp_err_to_name(err));
    return EspErrorToResult(err);
  }

  // Perform the write transaction
  err = i2c_master_transmit(dev_handle, data, length, pdMS_TO_TICKS(timeout_ms));

  // Remove the device handle - MUST be done regardless of transmit result
  esp_err_t rm_err = i2c_master_bus_rm_device(dev_handle);
  if (rm_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to remove device 0x%02X handle: %s", device_addr, esp_err_to_name(rm_err));
    // If transmit succeeded but remove failed, report the remove error
    // If transmit failed, report the transmit error primarily
    if (err == ESP_OK) {
      err = rm_err; // Report the removal error
    }
  }

  // Log specific NACK errors if possible based on err
  if (err == ESP_FAIL) {
      ESP_LOGD(TAG, "Write to 0x%02X failed (NACK/BusError?)", device_addr);
      // Cannot easily distinguish Addr NACK from Data NACK here with ESP_FAIL
      // We could return kNackAddr as a likely cause for ESP_FAIL in simple writes
      return Result::kNackAddr; // Or stick to kBusError from mapping
  }

  return EspErrorToResult(err);
}

// **************************************************************************
// Corrected Signature: uint8_t data -> uint8_t* data
// **************************************************************************
I2c::Result I2c::Read(uint8_t device_addr, uint8_t* data, size_t length,
                      uint32_t timeout_ms) {
  if (!initialized_ || bus_handle_ == nullptr) {
    return Result::kBusy;
  }
  if (data == nullptr || length == 0) {
    return Result::kInvalidArgs;
  }
   if (device_addr > 0x7F) {
      return Result::kInvalidArgs;
  }

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = device_addr;
  dev_cfg.scl_speed_hz = config_.frequency;

  i2c_master_dev_handle_t dev_handle = nullptr;
  esp_err_t err = i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle);
  if (err != ESP_OK) {
     ESP_LOGE(TAG, "Failed to add device 0x%02X: %s", device_addr, esp_err_to_name(err));
    return EspErrorToResult(err);
  }

  // Perform the read transaction
  err = i2c_master_receive(dev_handle, data, length, pdMS_TO_TICKS(timeout_ms));

  // Remove the device handle
  esp_err_t rm_err = i2c_master_bus_rm_device(dev_handle);
   if (rm_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to remove device 0x%02X handle: %s", device_addr, esp_err_to_name(rm_err));
    if (err == ESP_OK) err = rm_err;
  }

  if (err == ESP_FAIL) {
      ESP_LOGD(TAG, "Read from 0x%02X failed (NACK/BusError?)", device_addr);
      return Result::kNackAddr; // Address NACK is likely cause for read failure start
  }

  return EspErrorToResult(err);
}

// **************************************************************************
// Corrected Signature: const uint8_t data -> const uint8_t* data
// **************************************************************************
I2c::Result I2c::WriteReg(uint8_t device_addr, uint8_t reg_addr,
                          const uint8_t* data, size_t length,
                          uint32_t timeout_ms) {
  if (!initialized_ || bus_handle_ == nullptr) {
    return Result::kBusy;
  }
  if (data == nullptr && length > 0) {
      return Result::kInvalidArgs;
  }
  if (device_addr > 0x7F) {
      return Result::kInvalidArgs;
  }
  // Check if the combined buffer exceeds our static allocation limit
  if (length + 1 > kMaxWriteRegBufferSize) {
      ESP_LOGE(TAG, "WriteReg length (%zu) exceeds static buffer limit (%zu)",
               length, kMaxWriteRegBufferSize -1);
      return Result::kInvalidArgs;
  }


  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = device_addr;
  dev_cfg.scl_speed_hz = config_.frequency;

  i2c_master_dev_handle_t dev_handle = nullptr;
  esp_err_t err = i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add device 0x%02X: %s", device_addr, esp_err_to_name(err));
    return EspErrorToResult(err);
  }

  // Create a temporary buffer on the stack: [reg_addr, data...]
  // NO DYNAMIC ALLOCATION
  uint8_t temp_buffer[kMaxWriteRegBufferSize]; // Use fixed-size stack buffer
  temp_buffer[0] = reg_addr;
  if (length > 0) {
      memcpy(temp_buffer + 1, data, length);
  }

  // Perform the write transaction (register address + data)
  err = i2c_master_transmit(dev_handle, temp_buffer, length + 1,
                             pdMS_TO_TICKS(timeout_ms));

  // Remove the device handle
  esp_err_t rm_err = i2c_master_bus_rm_device(dev_handle);
  if (rm_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to remove device 0x%02X handle: %s", device_addr, esp_err_to_name(rm_err));
    if (err == ESP_OK) err = rm_err;
  }

  if (err == ESP_FAIL) {
      ESP_LOGD(TAG, "WriteReg to 0x%02X, reg 0x%02X failed (NACK/BusError?)", device_addr, reg_addr);
      // Could be Addr NACK or Data NACK
      return Result::kNackAddr; // Default guess
  }

  return EspErrorToResult(err);
}

// **************************************************************************
// Corrected Signature: uint8_t data -> uint8_t* data
// **************************************************************************
I2c::Result I2c::ReadReg(uint8_t device_addr, uint8_t reg_addr, uint8_t* data,
                         size_t length, uint32_t timeout_ms) {
  if (!initialized_ || bus_handle_ == nullptr) {
    return Result::kBusy;
  }
   if (data == nullptr || length == 0) {
    return Result::kInvalidArgs;
  }
   if (device_addr > 0x7F) {
      return Result::kInvalidArgs;
  }

  // Use i2c_master_transmit_receive for atomic register read operation
  // (Write register address, then Read data)

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = device_addr;
  dev_cfg.scl_speed_hz = config_.frequency;

  i2c_master_dev_handle_t dev_handle = nullptr;
  esp_err_t err = i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle);
   if (err != ESP_OK) {
     ESP_LOGE(TAG, "Failed to add device 0x%02X: %s", device_addr, esp_err_to_name(err));
    return EspErrorToResult(err);
  }

  // Perform the combined write (register address) and read (data) transaction
  err = i2c_master_transmit_receive(dev_handle,
                                     &reg_addr, // Pointer to register address to write
                                     1,         // Write length (1 byte for register addr)
                                     data,      // Buffer to store read data
                                     length,    // Read length
                                     pdMS_TO_TICKS(timeout_ms));

  // Remove the device handle
  esp_err_t rm_err = i2c_master_bus_rm_device(dev_handle);
    if (rm_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to remove device 0x%02X handle: %s", device_addr, esp_err_to_name(rm_err));
    if (err == ESP_OK) err = rm_err;
  }

   if (err == ESP_FAIL) {
      ESP_LOGD(TAG, "ReadReg from 0x%02X, reg 0x%02X failed (NACK/BusError?)", device_addr, reg_addr);
      return Result::kNackAddr; // Address NACK is likely cause
  }

  return EspErrorToResult(err);
}

// **************************************************************************
// Corrected Signature: uint8_t found_devices -> uint8_t* found_devices
// **************************************************************************
I2c::Result I2c::ScanDevices(uint8_t* found_devices, size_t max_devices,
                             size_t* found_count) {
  if (!initialized_ || bus_handle_ == nullptr) {
    return Result::kBusy;
  }
  if (found_devices == nullptr || found_count == nullptr || max_devices == 0) {
    return Result::kInvalidArgs;
  }

  *found_count = 0;
  esp_err_t err = ESP_OK;

  ESP_LOGI(TAG, "Scanning I2C bus (port %d)...", config_.port);

  // Iterate through valid 7-bit addresses (excluding reserved ones if desired)
  // Standard range is 0x08 to 0x77
  for (uint8_t i = 0x08; i <= 0x77; ++i) {
    // Use i2c_master_probe to check if a device ACKs the address
    // Use a short timeout for probing
    err = i2c_master_probe(bus_handle_, i, pdMS_TO_TICKS(50)); // 50ms timeout

    if (err == ESP_OK) { // Device responded (ACKed)
      if (*found_count < max_devices) {
        found_devices[*found_count] = i;
        (*found_count)++;
        ESP_LOGI(TAG, "  Found device at address 0x%02X", i);
      } else {
        ESP_LOGW(TAG, "  Found device at 0x%02X, but buffer is full (max %zu)", i, max_devices);
        // Stop scanning if buffer is full? Or just report success with partial results?
        // Current behaviour: continue scanning but don't store more.
      }
    } else if (err == ESP_ERR_TIMEOUT) {
        // This might happen if SCL is held low. Log it.
        ESP_LOGW(TAG, "  Timeout probing address 0x%02X (SCL held low?)", i);
        // Depending on the desired behavior, you might want to stop scanning
        // or return a specific error here. For now, continue scanning.
    } else if (err != ESP_ERR_NOT_FOUND && err != ESP_FAIL) {
        // ESP_ERR_NOT_FOUND or ESP_FAIL means NACK (no device) - this is expected.
        // Log other unexpected errors during probe.
        ESP_LOGE(TAG, "  Error probing address 0x%02X: %s", i, esp_err_to_name(err));
        // Potentially return EspErrorToResult(err) here if a bus error occurs
    }
    // Reset err for the next iteration's check if needed, though probe sets it each time.
    err = ESP_OK;
  } // end for loop

  ESP_LOGI(TAG, "Scan finished. Found %zu device(s).", *found_count);
  return Result::kSuccess; // Scan itself completed successfully
}

} // namespace io

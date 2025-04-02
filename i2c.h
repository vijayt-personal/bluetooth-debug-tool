/**
 * @file i2c.h
 * @brief I2C communication library for embedded systems
 * @details Provides a C++ wrapper for I2C communication protocols
 * following Google C++ style guidelines. Designed for use with
 * ESP-IDF i2c_master driver without dynamic allocation.
 */
#ifndef I2C_H // Standard include guard convention (use _H suffix)
#define I2C_H

#include <cstddef> // For size_t
#include <cstdint> // For standard integer types

// Forward declaration for the internal handle type if needed,
// or just use void* if implementation details are hidden.
// Using void* here to keep the header independent of specific driver includes.
// struct i2c_master_bus_handle_t; // Example if using specific type

namespace io {

/**
 * @brief I2C communication class
 * @details Provides methods for communicating with I2C devices in master mode.
 * Manages the underlying I2C bus resource.
 */
class I2c {
public:
  /**
   * @brief Result codes for I2C operations
   */
  enum class Result {
    kSuccess = 0,      ///< Operation completed successfully
    kBusError,         ///< Bus error occurred (hardware, SDA/SCL line issues)
    kArbitrationLost,  ///< Arbitration lost (not typically reported by ESP-IDF master) - Kept for API compatibility if needed
    kNackAddr,         ///< Address was not acknowledged by the slave device
    kNackData,         ///< Data byte was not acknowledged by the slave device
    kTimeOut,          ///< Operation timed out waiting for ACK, clock stretching, or bus access
    kBusy,             ///< Driver/Bus is busy or already initialized with this instance
    kInvalidArgs,      ///< Invalid arguments provided to a method
  };

  /**
   * @brief Configuration for I2C bus initialization
   */
  struct Config {
    uint8_t port;       ///< I2C port number (e.g., 0 or 1 for ESP32)
    uint32_t frequency; ///< Clock frequency in Hz (e.g., 100000 for 100kHz, 400000 for 400kHz)
    uint8_t sda_pin;    ///< GPIO pin number for SDA
    uint8_t scl_pin;    ///< GPIO pin number for SCL
    bool pull_up;       ///< Enable internal pull-up resistors on SDA and SCL lines
  };

  /**
   * @brief Default constructor. Initializes object state but not the hardware.
   */
  I2c();

  /**
   * @brief Destructor. Ensures Deinitialize() is called to release resources.
   */
  ~I2c();

  // Prevent copying and assignment to manage resource ownership correctly
  I2c(const I2c&) = delete;
  I2c& operator=(const I2c&) = delete;
  // Allow moving if desired (though implementation needs care)
  I2c(I2c&&) = default; // Or implement custom move if needed
  I2c& operator=(I2c&&) = default; // Or implement custom move if needed


  /**
   * @brief Initialize the I2C driver and hardware bus.
   * @param config Configuration parameters for the I2C bus.
   * @return Result::kSuccess on success, error code otherwise.
   */
  Result Initialize(const Config& config);

  /**
   * @brief Deinitialize the I2C driver and release hardware resources.
   * @return Result::kSuccess on success, error code otherwise.
   */
  Result Deinitialize();

  /**
   * @brief Write data to an I2C device.
   * @param device_addr 7-bit I2C slave device address.
   * @param data Pointer to the buffer containing data to write. Must not be NULL if length > 0.
   * @param length Number of bytes to write from the data buffer.
   * @param timeout_ms Timeout for the transaction in milliseconds (default 1000ms).
   * @return Result::kSuccess on success, error code otherwise (e.g., kNackAddr, kTimeOut).
   */
  Result Write(uint8_t device_addr, const uint8_t* data, size_t length,
               uint32_t timeout_ms = 1000);

  /**
   * @brief Read data from an I2C device.
   * @param device_addr 7-bit I2C slave device address.
   * @param data Pointer to the buffer where read data will be stored. Must not be NULL.
   * @param length Number of bytes to read into the data buffer. Must be > 0.
   * @param timeout_ms Timeout for the transaction in milliseconds (default 1000ms).
   * @return Result::kSuccess on success, error code otherwise (e.g., kNackAddr, kTimeOut).
   */
  Result Read(uint8_t device_addr, uint8_t* data, size_t length,
              uint32_t timeout_ms = 1000);

  /**
   * @brief Write data to a specific register within an I2C device.
   * Performs: START-ADDR(W)-REG_ADDR-DATA...-STOP
   * @param device_addr 7-bit I2C slave device address.
   * @param reg_addr The address of the register within the slave device to write to.
   * @param data Pointer to the buffer containing data to write. Must not be NULL if length > 0.
   * @param length Number of bytes to write from the data buffer (following the register address).
   * @param timeout_ms Timeout for the transaction in milliseconds (default 1000ms).
   * @return Result::kSuccess on success, error code otherwise.
   */
  Result WriteReg(uint8_t device_addr, uint8_t reg_addr, const uint8_t* data,
                  size_t length, uint32_t timeout_ms = 1000);

  /**
   * @brief Read data from a specific register within an I2C device.
   * Performs: START-ADDR(W)-REG_ADDR-REPEATED_START-ADDR(R)-DATA...-STOP
   * @param device_addr 7-bit I2C slave device address.
   * @param reg_addr The address of the register within the slave device to read from.
   * @param data Pointer to the buffer where read data will be stored. Must not be NULL.
   * @param length Number of bytes to read from the register into the data buffer. Must be > 0.
   * @param timeout_ms Timeout for the transaction in milliseconds (default 1000ms).
   * @return Result::kSuccess on success, error code otherwise.
   */
  Result ReadReg(uint8_t device_addr, uint8_t reg_addr, uint8_t* data,
                 size_t length, uint32_t timeout_ms = 1000);

  /**
   * @brief Scan the I2C bus for responding devices.
   * Probes all valid 7-bit addresses (0x08 to 0x77).
   * @param[out] found_devices Pointer to an array (allocated by caller) to store found device addresses. Must not be NULL.
   * @param[in] max_devices The maximum number of device addresses the found_devices array can hold. Must be > 0.
   * @param[out] found_count Pointer to a size_t variable (provided by caller) where the actual count of found devices will be stored. Must not be NULL.
   * @return Result::kSuccess if the scan completed (even if no devices found), kInvalidArgs if parameters are invalid, or kBusy if not initialized. Bus errors during probe are logged but may not cause a failure return.
   */
  Result ScanDevices(uint8_t* found_devices, size_t max_devices,
                     size_t* found_count);

private:
  Config config_;        ///< Stores the configuration used during initialization.
  void* bus_handle_;     ///< Opaque handle to the underlying driver's bus instance. (e.g., i2c_master_bus_handle_t)
  bool initialized_;     ///< Flag indicating if Initialize() has been successfully called.
};

} // namespace io

#endif // I2C_H

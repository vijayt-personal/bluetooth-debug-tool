////////////////////////////////////////////////////////////////////////////////
// aws_iot_interface.hpp
// Copyright (c) 2025 Your Company Name
// SPDX-License-Identifier: Your-License-Identifier
// Description: Abstract C++ interface for AWS IoT MQTT client (Google Style).
//              Manages connectivity and yield internally via a background task.
////////////////////////////////////////////////////////////////////////////////

#ifndef COMPONENTS_AWS_IOT_AWS_IOT_INTERFACE_H_
#define COMPONENTS_AWS_IOT_AWS_IOT_INTERFACE_H_

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <cstdint>
#include <cstddef> // For size_t

/**
 * @brief Configuration settings for the AWS IoT client (excluding certs/key).
 */
struct AwsIotConfig {
  /// @brief AWS IoT Core endpoint URL. REQUIRED.
  std::string endpoint;
  /// @brief Unique identifier for this MQTT client. REQUIRED.
  std::string client_id;
  /// @brief The "Thing Name" registered in AWS IoT Core. REQUIRED.
  std::string thing_name;
  /// @brief Port number for the MQTT connection. Default: 8883.
  uint16_t port = 8883;
  /// @brief MQTT Keep-Alive interval in seconds. Default: 60.
  uint16_t keep_alive_sec = 60;
  /// @brief Enable automatic reconnection by the SDK. Default: true.
  bool auto_reconnect = true;
  /// @brief Timeout for MQTT operations (ms). Default: 20000.
  int command_timeout_ms = 20000;
  /// @brief Timeout for the TLS handshake (ms). Default: 5000.
  int tls_handshake_timeout_ms = 5000;
};

/**
 * @brief Abstract interface for interacting with AWS IoT Core via MQTT.
 *
 * Manages connection state and MQTT keep-alives/message processing
 * automatically via an internal background task.
 * Use the factory function create_aws_iot_client() to get an instance.
 */
class AwsIotInterface {
 public:
  // --- Type Definitions ---
  using MessageHandler =
      std::function<void(std::string_view topic, std::string_view payload)>;
  using ConnectionCallback = std::function<void()>;
  using ShadowUpdateHandler =
      std::function<void(std::string_view type, std::string_view payload)>;
  using JobNotificationHandler =
      std::function<void(std::string_view topic_suffix,
                         std::string_view payload)>;

  /// @brief Max size for cert/key buffers (including required null terminator).
  static constexpr size_t kMaxCertBufferSize = 2048;

  // --- Lifecycle ---
  virtual ~AwsIotInterface() = default;

  /**
   * @brief Loads client configuration (endpoint, client ID, timeouts etc.).
   * @param config Configuration settings (excluding certs).
   * @return True if config loaded and validated, false otherwise. Fails if connected.
   */
  virtual bool load_configuration(const AwsIotConfig& config) = 0;

  /**
   * @brief Sets the Root CA certificate. Call before connect().
   * @param pem_data Pointer to PEM data buffer. Must not be null.
   * @param length Length of data in buffer (excluding null terminator).
   * @return True if valid and stored (checks size), false otherwise. Fails if connected.
   */
  virtual bool set_root_ca(const char* pem_data, size_t length) = 0;

  /**
   * @brief Sets the Client certificate. Call before connect().
   * @param pem_data Pointer to PEM data buffer. Must not be null.
   * @param length Length of data in buffer (excluding null terminator).
   * @return True if valid and stored (checks size), false otherwise. Fails if connected.
   */
  virtual bool set_client_cert(const char* pem_data, size_t length) = 0;

  /**
   * @brief Sets the Client private key. Call before connect().
   * @param pem_data Pointer to PEM data buffer. Must not be null.
   * @param length Length of data in buffer (excluding null terminator).
   * @return True if valid and stored (checks size), false otherwise. Fails if connected.
   */
  virtual bool set_client_key(const char* pem_data, size_t length) = 0;

  /**
   * @brief Connects to AWS IoT using loaded config and certificates.
   * Starts internal background task for MQTT processing.
   * Requires load_configuration() and set_*() cert methods called successfully first.
   * @return True if connection initiation succeeded, false otherwise.
   */
  virtual bool connect() = 0;

  /**
   * @brief Disconnects from AWS IoT, stops background task, cleans up resources.
   * @return True if disconnect sequence completed successfully, false otherwise.
   */
  virtual bool disconnect() = 0;

  /**
   * @brief Checks if the client is currently considered connected.
   * @return True if connected, false otherwise.
   */
  virtual bool is_connected() const = 0;

  // --- MQTT Operations ---
  virtual bool publish(std::string_view topic, std::string_view payload,
                       int qos = 0) = 0;
  virtual bool subscribe(std::string_view topic_filter, int qos,
                         MessageHandler handler) = 0;
  virtual bool unsubscribe(std::string_view topic_filter) = 0;

  // --- Device Shadow ---
  virtual bool subscribe_to_shadow_updates(ShadowUpdateHandler handler) = 0;
  virtual bool unsubscribe_from_shadow_updates() = 0;
  virtual bool publish_shadow_update(std::string_view shadow_payload,
                                     int qos = 1) = 0;
  virtual bool publish_shadow_get_request() = 0;

  // --- IoT Jobs ---
  virtual bool subscribe_to_job_notifications(
      JobNotificationHandler handler) = 0;
  virtual bool unsubscribe_from_job_notifications() = 0;
  virtual bool publish_job_execution_update(
      std::string_view job_id, std::string_view status,
      std::string_view status_details_json = "{}") = 0;

  // --- Callbacks ---
  virtual void set_on_connected_callback(ConnectionCallback handler) = 0;
  virtual void set_on_disconnected_callback(ConnectionCallback handler) = 0;

  // Note: yield() method is removed. It's handled internally.

};  // class AwsIotInterface

/**
 * @brief Factory function to create an AWS IoT client implementation instance.
 * @return A std::unique_ptr managing the AwsIotInterface instance. Check if
 * valid (not nullptr) before use.
 */
std::unique_ptr<AwsIotInterface> create_aws_iot_client();

#endif  // COMPONENTS_AWS_IOT_AWS_IOT_INTERFACE_H_

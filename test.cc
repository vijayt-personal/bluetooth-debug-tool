////////////////////////////////////////////////////////////////////////////////
// aws_iot_client.cpp
// Copyright (c) 2025 Your Company Name
// SPDX-License-Identifier: Your-License-Identifier
// Description: Implementation of AWS IoT C++ interface (Google Style).
//              Manages internal yield task and uses std::mutex.
////////////////////////////////////////////////////////////////////////////////

#include "aws_iot_interface.hpp"  // Corresponding header first

// C++ Standard Library
#include <atomic>
#include <chrono>
#include <cstring> // For memcpy, memset
#include <map>
#include <memory>
#include <mutex>   // Use std::mutex
#include <string>
#include <thread>

// ESP-IDF / FreeRTOS
#include "freertos/FreeRTOS.h" // Still needed for tasks, delays
#include "freertos/task.h"    // For TaskHandle_t, xTaskCreate, vTaskDelete
#include "esp_log.h"
// #include "sdkconfig.h" // No longer needed for MT config check

// AWS IoT C SDK (Submodule includes)
#include "aws_iot_config.h"
#include "aws_iot_error.h"
#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_client.h"
#include "aws_iot_mqtt_client_interface.h"

// --- Constants ---
static constexpr const char* kTag = "AwsIotClientImpl";
static constexpr int kYieldTaskStackSize = 4096;
static constexpr UBaseType_t kYieldTaskPriority = 5;
static constexpr uint32_t kDefaultYieldTimeoutMs = 200;
static constexpr int kShadowJobsQos = 1;
static constexpr size_t kMaxCertBufferSize = AwsIotInterface::kMaxCertBufferSize;

// --- Implementation Class (Hidden via Pimpl) ---
class AwsIotClientImpl {
  friend std::unique_ptr<AwsIotInterface> create_aws_iot_client();

 public:
  // --- Destructor ---
  ~AwsIotClientImpl() {
    ESP_LOGI(kTag, "Destroying AwsIotClientImpl (Client ID: %s)...",
             config_.client_id.c_str());
    disconnect_internal(); // Ensure task stopped, client freed, etc.
    ESP_LOGI(kTag, "AwsIotClientImpl destroyed.");
  }

  // --- Delete copy/move operations ---
  AwsIotClientImpl(const AwsIotClientImpl&) = delete;
  AwsIotClientImpl& operator=(const AwsIotClientImpl&) = delete;
  AwsIotClientImpl(AwsIotClientImpl&&) = delete;
  AwsIotClientImpl& operator=(AwsIotClientImpl&&) = delete;

  // --- Public Interface Implementation Methods ---

  bool load_configuration(const AwsIotConfig& config) {
    if (config.endpoint.empty() || config.client_id.empty() ||
        config.thing_name.empty()) {
      ESP_LOGE(kTag, "Config validation failed: endpoint, client_id, "
                     "thing_name required.");
      return false;
    }
     if (config.port == 0) {
       ESP_LOGE(kTag, "Config validation failed: Port cannot be 0.");
       return false;
     }

    std::lock_guard<std::mutex> lock(mutex_);
    if (is_connected_.load() || client_initialized_) {
      ESP_LOGE(
          kTag,
          "Cannot load config while connected/initialized. Disconnect first.");
      return false;
    }
    config_ = config;
    config_loaded_ = true;
    ESP_LOGI(kTag, "Config loaded (Client: %s, Thing: %s)",
             config_.client_id.c_str(), config_.thing_name.c_str());
    return true;
  }

  // --- Certificate Setting Functions ---
  bool set_root_ca(const char* pem_data, size_t length) {
      return set_certificate_internal(pem_data, length, root_ca_buffer_,
                                    &root_ca_len_, &root_ca_set_, "Root CA");
  }
  bool set_client_cert(const char* pem_data, size_t length) {
      return set_certificate_internal(pem_data, length, client_cert_buffer_,
                                    &client_cert_len_, &client_cert_set_, "Client Cert");
  }
  bool set_client_key(const char* pem_data, size_t length) {
       return set_certificate_internal(pem_data, length, client_key_buffer_,
                                     &client_key_len_, &client_key_set_, "Client Key");
  }

  bool connect() {
    if (!config_loaded_) {
      ESP_LOGE(kTag, "Config not loaded. Call load_configuration() first.");
      return false;
    }
    if (!root_ca_set_ || !client_cert_set_ || !client_key_set_) {
        ESP_LOGE(kTag, "Certs/Key not set. Call set_*() methods first.");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (is_connected_.load()) {
      ESP_LOGW(kTag, "connect: Already connected.");
      return true;
    }
    if (client_initialized_) {
      ESP_LOGE(kTag, "connect: Client initialized but not connected. "
                     "Disconnect fully before reconnecting.");
      return false;
    }

    ESP_LOGI(kTag, "Connecting to AWS IoT: Endpoint=%s, ClientID=%s",
             config_.endpoint.c_str(), config_.client_id.c_str());

    // --- 1. Initialize client ---
    IoT_Error_t rc = SUCCESS;
    IoT_Client_Init_Params mqtt_init_params = iotClientInitParamsDefault;
    mqtt_init_params.pHostURL = config_.endpoint.c_str();
    mqtt_init_params.port = config_.port;
    mqtt_init_params.pRootCALocation = root_ca_buffer_; // Use internal buffers
    mqtt_init_params.pDeviceCertLocation = client_cert_buffer_;
    mqtt_init_params.pDevicePrivateKeyLocation = client_key_buffer_;
    mqtt_init_params.mqttCommandTimeout_ms = config_.command_timeout_ms;
    mqtt_init_params.tlsHandshakeTimeout_ms = config_.tls_handshake_timeout_ms;
    mqtt_init_params.isSSLHostnameVerify = true;
    mqtt_init_params.enableAutoReconnect = config_.auto_reconnect;
    mqtt_init_params.disconnectHandler = aws_disconnect_callback;
    mqtt_init_params.disconnectHandlerData = this;

    rc = aws_iot_mqtt_init(&client_, &mqtt_init_params);
    if (rc != SUCCESS) {
      ESP_LOGE(kTag, "aws_iot_mqtt_init failed: error %d", rc);
      client_initialized_ = false;
      return false;
    }
    client_initialized_ = true;
    ESP_LOGD(kTag, "aws_iot_mqtt_init successful.");

    // --- 2. Connect client ---
    IoT_Client_Connect_Params connect_params = iotClientConnectParamsDefault;
    connect_params.keepAliveIntervalInSec = config_.keep_alive_sec;
    connect_params.isCleanSession = true;
    connect_params.MQTTVersion = MQTT_3_1_1;
    connect_params.pClientID = config_.client_id.c_str();
    connect_params.clientIDLen = (uint16_t)config_.client_id.length();
    connect_params.isWillMsgPresent = false;

    rc = aws_iot_mqtt_connect(&client_, &connect_params);
    if (rc != SUCCESS) {
      ESP_LOGE(kTag, "aws_iot_mqtt_connect failed: error %d", rc);
      if (!config_.auto_reconnect) {
        aws_iot_mqtt_free(&client_);
        client_initialized_ = false;
      }
      return false;
    }

    // --- 3. Start Background Task ---
    is_connected_.store(true);
    ESP_LOGI(kTag, "aws_iot_mqtt_connect request sent.");

    bool task_started_ok = true;
    // Task is now mandatory, start if not already running
    if (yield_task_handle_ == nullptr) {
        should_yield_task_run_.store(true);
        BaseType_t task_created = xTaskCreate(
            yield_task_runner, "aws_yield", kYieldTaskStackSize, this,
            kYieldTaskPriority, &yield_task_handle_);

        if (task_created != pdPASS) {
            ESP_LOGE(kTag, "CRITICAL: Failed to create yield task!");
            yield_task_handle_ = nullptr;
            task_started_ok = false;
            // Attempt cleanup
            is_connected_.store(false);
            if (client_initialized_) {
                aws_iot_mqtt_disconnect(&client_);
                aws_iot_mqtt_free(&client_);
                client_initialized_ = false;
            }
        } else {
            ESP_LOGI(kTag, "Internal yield task started.");
        }
    }

    // --- 4. Trigger Connect Callback ---
    if (task_started_ok) {
        trigger_connected_callback();
    }

    return task_started_ok;
  }

  bool disconnect() { return disconnect_internal(); }

  bool is_connected() const { return is_connected_.load(); }

  bool publish(std::string_view topic, std::string_view payload, int qos) {
    if (!is_connected()) {
      ESP_LOGE(kTag, "publish: Not connected.");
      return false;
    }
    if (topic.empty()) {
      ESP_LOGE(kTag, "publish: Topic cannot be empty.");
      return false;
    }
    if (qos < 0 || qos > 1) {
      ESP_LOGE(kTag, "publish: Invalid QoS %d.", qos);
      return false;
    }

    char* topic_ptr = const_cast<char*>(topic.data());
    char* payload_ptr = const_cast<char*>(payload.data());
    IoT_Publish_Message_Params params_qos;
    params_qos.qos = (qos == 1) ? QOS1 : QOS0;
    params_qos.payload = static_cast<void*>(payload_ptr);
    params_qos.payloadLen = payload.length();
    params_qos.isRetained = 0;

    ESP_LOGD(kTag, "Publishing to '%.*s' (QoS%d, Len:%d)", (int)topic.length(),
             topic.data(), qos, params_qos.payloadLen);

    IoT_Error_t rc = aws_iot_mqtt_publish(&client_, topic_ptr, topic.length(),
                                        &params_qos);

    if (rc != SUCCESS) {
      ESP_LOGE(kTag, "publish: aws_iot_mqtt_publish failed for '%.*s': %d",
               (int)topic.length(), topic.data(), rc);
      handle_potential_disconnect_error(rc, "publish");
      return false;
    }
    return true;
  }

  bool subscribe(std::string_view topic_filter_sv, int qos,
                 AwsIotInterface::MessageHandler handler) {
    if (!is_connected()) {
      ESP_LOGE(kTag, "subscribe: Not connected.");
      return false;
    }
    if (topic_filter_sv.empty()) {
      ESP_LOGE(kTag, "subscribe: Topic filter cannot be empty.");
      return false;
    }
    if (qos < 0 || qos > 1) { ESP_LOGE(kTag, "subscribe: Invalid QoS %d.", qos); return false; }
    if (!handler) { ESP_LOGE(kTag, "subscribe: Handler cannot be null."); return false; }

    std::string topic_filter(topic_filter_sv);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        message_handlers_[topic_filter] = std::move(handler);
        ESP_LOGD(kTag, "subscribe: Handler stored for '%s'", topic_filter.c_str());
    }

    ESP_LOGI(kTag, "Subscribing to '%s' (QoS%d)", topic_filter.c_str(), qos);
    IoT_Error_t rc = aws_iot_mqtt_subscribe(
        &client_, topic_filter.c_str(), topic_filter.length(),
        (qos == 1) ? QOS1 : QOS0, aws_subscribe_callback, this);

    if (rc != SUCCESS) {
      ESP_LOGE(kTag, "subscribe: aws_iot_mqtt_subscribe failed for '%s': %d",
               topic_filter.c_str(), rc);
      {
          std::lock_guard<std::mutex> lock(mutex_);
          message_handlers_.erase(topic_filter); // Clean up handler map
      }
      handle_potential_disconnect_error(rc, "subscribe");
      return false;
    }
    ESP_LOGI(kTag, "subscribe: Subscribe request sent for '%s'", topic_filter.c_str());
    return true;
  }

  bool unsubscribe(std::string_view topic_filter_sv) {
    if (topic_filter_sv.empty()) {
      ESP_LOGE(kTag, "unsubscribe: Topic filter cannot be empty.");
      return false;
    }
    std::string topic_filter(topic_filter_sv);
    ESP_LOGI(kTag, "Unsubscribing from '%s'", topic_filter.c_str());

    bool handler_existed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (message_handlers_.erase(topic_filter) > 0) {
            handler_existed = true;
            ESP_LOGD(kTag, "unsubscribe: Removed handler for '%s'", topic_filter.c_str());
        }
    }

    bool unsubscribe_sent_ok = true;
    if (is_connected()) {
      IoT_Error_t rc = aws_iot_mqtt_unsubscribe(&client_, topic_filter.c_str(),
                                                topic_filter.length());
      if (rc != SUCCESS) {
        ESP_LOGE(kTag, "unsubscribe: aws_iot_mqtt_unsubscribe failed for '%s': %d",
                 topic_filter.c_str(), rc);
        handle_potential_disconnect_error(rc, "unsubscribe");
        unsubscribe_sent_ok = false;
      } else {
        ESP_LOGI(kTag, "unsubscribe: Unsubscribe request sent for '%s'", topic_filter.c_str());
      }
    } else {
      ESP_LOGW(kTag, "unsubscribe: Not connected, MQTT command skipped for '%s'.", topic_filter.c_str());
    }
    return unsubscribe_sent_ok; // Return based on MQTT command success if attempted
  }

  // --- Shadow Implementation ---
  bool subscribe_to_shadow_updates( AwsIotInterface::ShadowUpdateHandler handler) {
    if (!config_loaded_ || config_.thing_name.empty()) { ESP_LOGE(kTag, "subscribe_shadow: Config/Thing Name missing."); return false; }
    if (!handler) { ESP_LOGE(kTag, "subscribe_shadow: Null handler."); return false; }
    if (!is_connected()) { ESP_LOGE(kTag, "subscribe_shadow: Not connected."); return false; }

    if (shadow_subscribed_) {
      ESP_LOGI(kTag, "subscribe_shadow: Unsubscribing existing shadow topics.");
      unsubscribe_from_shadow_updates_internal();
    }
    ESP_LOGI(kTag, "Subscribing to shadow topics for Thing: %s", config_.thing_name.c_str());
    bool success = true;
    std::string prefix = "$aws/things/" + config_.thing_name + "/shadow/";
    if (!set_shadow_handler_internal(std::move(handler))) { return false; }

    success &= subscribe_internal(prefix + "update/accepted", kShadowJobsQos);
    success &= subscribe_internal(prefix + "update/rejected", kShadowJobsQos);
    success &= subscribe_internal(prefix + "update/delta", kShadowJobsQos);
    success &= subscribe_internal(prefix + "get/accepted", kShadowJobsQos);
    success &= subscribe_internal(prefix + "get/rejected", kShadowJobsQos);

    if (!success) {
      ESP_LOGE(kTag, "subscribe_shadow: Subscription failed. Cleaning up.");
      unsubscribe_from_shadow_updates_internal();
      set_shadow_handler_internal(nullptr);
      shadow_subscribed_ = false;
      return false;
    }
    shadow_subscribed_ = true;
    ESP_LOGI(kTag, "subscribe_shadow: Subscriptions successful.");
    return true;
  }

  bool unsubscribe_from_shadow_updates() {
    return unsubscribe_from_shadow_updates_internal();
  }

  bool publish_shadow_update(std::string_view shadow_payload, int qos) {
    if (!config_loaded_ || config_.thing_name.empty()) { ESP_LOGE(kTag, "publish_shadow_update: Config/Thing Name missing."); return false; }
    if (!is_connected()) { ESP_LOGE(kTag, "publish_shadow_update: Not connected."); return false; }
    std::string topic = "$aws/things/" + config_.thing_name + "/shadow/update";
    return publish(topic, shadow_payload, qos);
  }

  bool publish_shadow_get_request() {
    if (!config_loaded_ || config_.thing_name.empty()) { ESP_LOGE(kTag, "publish_shadow_get: Config/Thing Name missing."); return false; }
    if (!is_connected()) { ESP_LOGE(kTag, "publish_shadow_get: Not connected."); return false; }
    std::string topic = "$aws/things/" + config_.thing_name + "/shadow/get";
    return publish(topic, "", 0);
  }

  // --- Jobs Implementation ---
  bool subscribe_to_job_notifications( AwsIotInterface::JobNotificationHandler handler) {
     if (!config_loaded_ || config_.thing_name.empty()) { ESP_LOGE(kTag, "subscribe_jobs: Config/Thing Name missing."); return false; }
     if (!handler) { ESP_LOGE(kTag, "subscribe_jobs: Null handler."); return false; }
     if (!is_connected()) { ESP_LOGE(kTag, "subscribe_jobs: Not connected."); return false; }

     if (jobs_subscribed_) {
       ESP_LOGI(kTag, "subscribe_jobs: Unsubscribing existing job topics.");
       unsubscribe_from_job_notifications_internal();
     }
     ESP_LOGI(kTag, "Subscribing to job topics for Thing: %s", config_.thing_name.c_str());
     bool success = true;
     std::string prefix = "$aws/things/" + config_.thing_name + "/jobs/";
     if (!set_job_handler_internal(std::move(handler))) { return false; }

     success &= subscribe_internal(prefix + "notify-next", kShadowJobsQos);
     // Add others if needed

     if (!success) {
       ESP_LOGE(kTag, "subscribe_jobs: Subscription failed. Cleaning up.");
       unsubscribe_from_job_notifications_internal();
       set_job_handler_internal(nullptr);
       jobs_subscribed_ = false;
       return false;
     }
     jobs_subscribed_ = true;
     ESP_LOGI(kTag, "subscribe_jobs: Subscriptions successful.");
     return true;
  }

  bool unsubscribe_from_job_notifications() {
    return unsubscribe_from_job_notifications_internal();
  }

  bool publish_job_execution_update(std::string_view job_id_sv,
                                    std::string_view status_sv,
                                    std::string_view status_details_json_sv) {
    if (!config_loaded_ || config_.thing_name.empty()) { ESP_LOGE(kTag, "publish_job_update: Config/Thing Name missing."); return false; }
    if (job_id_sv.empty() || status_sv.empty()) { ESP_LOGE(kTag, "publish_job_update: Job ID and Status required."); return false; }
    if (!is_connected()) { ESP_LOGE(kTag, "publish_job_update: Not connected."); return false; }

    std::string topic = "$aws/things/" + config_.thing_name + "/jobs/";
    topic.append(job_id_sv.data(), job_id_sv.length());
    topic += "/update";
    std::string payload = R"({"status":")";
    payload.append(status_sv.data(), status_sv.length());
    payload += R"(","statusDetails":)";
    payload.append(status_details_json_sv.data(), status_details_json_sv.length());
    payload += "}";
    return publish(topic, payload, kShadowJobsQos);
  }

  // --- Callbacks Setter Implementation ---
  void set_on_connected_callback(AwsIotInterface::ConnectionCallback handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    on_connected_callback_ = std::move(handler);
    ESP_LOGD(kTag, "OnConnected callback %s.", on_connected_callback_ ? "set" : "cleared");
  }
  void set_on_disconnected_callback( AwsIotInterface::ConnectionCallback handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    on_disconnected_callback_ = std::move(handler);
    ESP_LOGD(kTag, "OnDisconnected callback %s.", on_disconnected_callback_ ? "set" : "cleared");
  }

  // --- yield() method removed from public interface ---


 private: //------------------- Private Members & Methods --------------------

  AwsIotClientImpl() // Private constructor
      : client_{}, config_{}, config_loaded_(false), client_initialized_(false),
        is_connected_(false), shadow_subscribed_(false), jobs_subscribed_(false),
        // mutex_ default constructs
        yield_task_handle_(nullptr), should_yield_task_run_(false),
        root_ca_set_(false), client_cert_set_(false), client_key_set_(false),
        root_ca_len_(0), client_cert_len_(0), client_key_len_(0)
        // callbacks default construct
        // map default constructs
        // general_shadow_job_callback_ initialized below
        {
    ESP_LOGD(kTag, "AwsIotClientImpl constructing...");
    memset(root_ca_buffer_, 0, kMaxCertBufferSize); // Zero buffers
    memset(client_cert_buffer_, 0, kMaxCertBufferSize);
    memset(client_key_buffer_, 0, kMaxCertBufferSize);
    general_shadow_job_callback_ = // Bind general dispatcher
        [this](std::string_view topic, std::string_view payload) {
          this->handle_shadow_job_message(topic, payload);
        };
    ESP_LOGD(kTag, "AwsIotClientImpl constructed.");
  }

  // --- Internal State and Handles ---
  AWS_IoT_Client client_;
  AwsIotConfig config_;
  bool config_loaded_;
  bool client_initialized_;
  std::atomic<bool> is_connected_;
  bool shadow_subscribed_;
  bool jobs_subscribed_;

  // --- Threading and Synchronization ---
  std::mutex mutex_; // Always used now
  TaskHandle_t yield_task_handle_;
  std::atomic<bool> should_yield_task_run_;

  // --- Certificate Storage ---
  char root_ca_buffer_[kMaxCertBufferSize];
  char client_cert_buffer_[kMaxCertBufferSize];
  char client_key_buffer_[kMaxCertBufferSize];
  size_t root_ca_len_;
  size_t client_cert_len_;
  size_t client_key_len_;
  bool root_ca_set_;
  bool client_cert_set_;
  bool client_key_set_;

  // --- User Callbacks & Handlers ---
  AwsIotInterface::ConnectionCallback on_connected_callback_;
  AwsIotInterface::ConnectionCallback on_disconnected_callback_;
  AwsIotInterface::ShadowUpdateHandler shadow_update_handler_;
  AwsIotInterface::JobNotificationHandler job_notification_handler_;
  std::map<std::string, AwsIotInterface::MessageHandler> message_handlers_;
  AwsIotInterface::MessageHandler general_shadow_job_callback_;

   // --- Internal Helper Methods ---

   /** @brief Internal helper to copy certificate data. Assumes lock NOT held initially. */
   bool set_certificate_internal(const char* pem_data, size_t length,
                                 char* buffer, size_t* length_storage,
                                 bool* set_flag, const char* name) {
       if (pem_data == nullptr || length == 0) { ESP_LOGE(kTag, "set_%s: Null/zero length data.", name); return false; }
       if (length >= kMaxCertBufferSize) { ESP_LOGE(kTag, "set_%s: Data length (%d) >= buffer size (%d).", name, length, kMaxCertBufferSize); return false; }

       std::lock_guard<std::mutex> lock(mutex_);
       if (is_connected_.load() || client_initialized_) { ESP_LOGE(kTag, "set_%s: Cannot set while connected/initialized.", name); return false; }

       memcpy(buffer, pem_data, length);
       buffer[length] = '\0'; // Ensure null termination
       *length_storage = length;
       *set_flag = true;
       ESP_LOGI(kTag, "%s set successfully (Length: %d).", name, length);
       return true;
   }

   /** @brief Internal disconnect logic. Idempotent. Assumes lock NOT held initially. */
   bool disconnect_internal() {
        ESP_LOGI(kTag, "disconnect_internal: Starting cleanup...");
        bool task_stopped = false;
        bool mqtt_disconnected = false;
        bool client_freed = false;

        // 1. Stop yield task (mandatory now)
        TaskHandle_t task_handle_local = nullptr;
        { // Minimal lock scope.
            std::lock_guard<std::mutex> lock(mutex_);
            task_handle_local = yield_task_handle_;
            yield_task_handle_ = nullptr; // Prevent reuse.
        }
        if (task_handle_local != nullptr) {
            ESP_LOGI(kTag, "disconnect_internal: Stopping yield task...");
            should_yield_task_run_.store(false);
            vTaskDelay(pdMS_TO_TICKS(kDefaultYieldTimeoutMs + 50)); // Allow task to exit yield.
            task_stopped = true; // Assume task deletes itself.
            ESP_LOGI(kTag, "disconnect_internal: Yield task stop signaled.");
        } else {
            task_stopped = true; // Task wasn't running.
        }

        // 2. Disconnect MQTT Client.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (client_initialized_) {
                ESP_LOGD(kTag, "disconnect_internal: Sending MQTT disconnect...");
                IoT_Error_t rc = aws_iot_mqtt_disconnect(&client_);
                mqtt_disconnected = (rc == SUCCESS || rc == MQTT_CLIENT_NOT_CONNECTED_ERROR);
                 if (!mqtt_disconnected) { ESP_LOGE(kTag, "aws_iot_mqtt_disconnect failed: %d", rc); }
            } else { mqtt_disconnected = true; }
            is_connected_.store(false);
            shadow_subscribed_ = false;
            jobs_subscribed_ = false;
        }

        // 3. Free MQTT Client Resources.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (client_initialized_) {
                ESP_LOGD(kTag, "disconnect_internal: Freeing MQTT client...");
                IoT_Error_t rc = aws_iot_mqtt_free(&client_);
                client_freed = (rc == SUCCESS);
                if (!client_freed) { ESP_LOGE(kTag, "aws_iot_mqtt_free failed: %d", rc); }
                client_initialized_ = false;
            } else { client_freed = true; }
        }
        ESP_LOGI(kTag, "disconnect_internal finished (TaskStop:%d, MQTT:%d, Freed:%d)", task_stopped, mqtt_disconnected, client_freed);
        return task_stopped && mqtt_disconnected && client_freed;
   }

   /** @brief Handles errors that might mean disconnection occurred. */
   void handle_potential_disconnect_error(IoT_Error_t rc, const char* operation) {
        bool disconnect_indicated = false;
        switch(rc) { /* ... cases ... */
            case NETWORK_DISCONNECTED_ERROR: case MQTT_CONNECTION_ERROR: case NETWORK_SSL_READ_ERROR:
            case NETWORK_SSL_WRITE_ERROR: case NETWORK_SSL_CONNECT_ERROR: disconnect_indicated = true; break;
            default: break; }

        if (disconnect_indicated) {
            ESP_LOGW(kTag, "Network error (%d) during '%s'. Assuming disconnection.", rc, operation);
             bool was_connected = is_connected_.exchange(false);
             if (was_connected) {
                 ESP_LOGW(kTag, "Marking client disconnected due to error.");
                 trigger_disconnected_callback();
                 // No need to stop task explicitly if auto-reconnect is off,
                 // as disconnect_internal will be called eventually by user or destructor.
                 // The SDK/yield task handles reconnect attempts if auto_reconnect is on.
                 if (!config_.auto_reconnect) { ESP_LOGW(kTag,"Auto-reconnect disabled. Manual action may be needed."); }
                 else { ESP_LOGI(kTag,"Auto-reconnect enabled. Yield task will handle retries."); }
             }
        }
    }

   /** @brief Safely triggers the on_connected_callback_. */
   void trigger_connected_callback() {
        AwsIotInterface::ConnectionCallback cb;
        { std::lock_guard<std::mutex> lock(mutex_); cb = on_connected_callback_; }
        if (cb) { ESP_LOGD(kTag, "Invoking on_connected callback."); try { cb(); } catch (...) { ESP_LOGE(kTag, "Exc in on_connected_cb"); } }
   }
   /** @brief Safely triggers the on_disconnected_callback_. */
   void trigger_disconnected_callback() {
        AwsIotInterface::ConnectionCallback cb;
        { std::lock_guard<std::mutex> lock(mutex_); cb = on_disconnected_callback_; }
        if (cb) { ESP_LOGD(kTag, "Invoking on_disconnected callback."); try { cb(); } catch (...) { ESP_LOGE(kTag, "Exc in on_disconnected_cb"); } }
   }

   /** @brief Routes messages arriving on shadow/job topics to specific handlers. */
   void handle_shadow_job_message(std::string_view topic, std::string_view payload) {
        // ... (Logic remains the same, uses std::lock_guard internally when copying handlers) ...
       ESP_LOGD(kTag, "Routing shadow/job message: '%.*s'", (int)topic.length(), topic.data());
       if (!config_loaded_ || config_.thing_name.empty()) { return; }
       std::string shadow_prefix = "$aws/things/" + config_.thing_name + "/shadow/";
       std::string jobs_prefix = "$aws/things/" + config_.thing_name + "/jobs/";
       AwsIotInterface::ShadowUpdateHandler shadow_cb;
       AwsIotInterface::JobNotificationHandler job_cb;
       std::string_view relevant_suffix; bool is_shadow = false; bool is_job = false;

       if (topic.size() > shadow_prefix.size() && topic.rfind(shadow_prefix, 0) == 0) {
           is_shadow = true; relevant_suffix = topic.substr(shadow_prefix.size());
           std::lock_guard<std::mutex> lock(mutex_); shadow_cb = shadow_update_handler_;
       } else if (topic.size() > jobs_prefix.size() && topic.rfind(jobs_prefix, 0) == 0) {
           is_job = true; relevant_suffix = topic.substr(jobs_prefix.size());
           std::lock_guard<std::mutex> lock(mutex_); job_cb = job_notification_handler_;
       } else { ESP_LOGW(kTag, "Internal handler for non-shadow/job topic: %.*s", (int)topic.length(), topic.data()); return; }

       if (is_shadow && shadow_cb) { /* ... (invoke shadow_cb with parsed type) ... */ try { /* ... */ } catch(...) { /* ... */ } return; }
       if (is_job && job_cb) { /* ... (invoke job_cb with relevant_suffix) ... */ try { /* ... */ } catch(...) { /* ... */ } return; }
       if ((is_shadow && !shadow_cb) || (is_job && !job_cb)) { ESP_LOGW(kTag, "Subscribed shadow/job msg received but no handler: '%.*s'", (int)topic.length(), topic.data()); }
   }

   /** @brief Internal subscribe, used by shadow/jobs. Assumes lock NOT held. */
    bool subscribe_internal(const std::string& topic_filter, int qos) {
        // ... (Checks: is_connected, topic_filter empty) ...
        if (!is_connected()) { return false; }
        if (topic_filter.empty()) { return false; }

        { // Store general handler for this specific topic.
            std::lock_guard<std::mutex> lock(mutex_);
            message_handlers_[topic_filter] = general_shadow_job_callback_;
        }
        ESP_LOGD(kTag, "subscribe_internal: Subscribing to '%s'", topic_filter.c_str());
        IoT_Error_t rc = aws_iot_mqtt_subscribe(&client_, topic_filter.c_str(), topic_filter.length(),
                                               (qos == 1) ? QOS1 : QOS0, aws_subscribe_callback, this);
        if (rc != SUCCESS) {
            // ... (Log error, cleanup handler map entry, handle disconnect error) ...
            { std::lock_guard<std::mutex> lock(mutex_); message_handlers_.erase(topic_filter); }
            handle_potential_disconnect_error(rc, "subscribe_internal");
            return false;
        }
        return true;
    }

    /** @brief Internal unsubscribe, used by shadow/jobs. Assumes lock NOT held. */
    bool unsubscribe_internal(const std::string& topic_filter) {
        // ... (Check: topic_filter empty) ...
        if (topic_filter.empty()) { return false; }
        { // Remove handler from map.
            std::lock_guard<std::mutex> lock(mutex_);
            message_handlers_.erase(topic_filter);
        }
        bool success = true;
        if (is_connected()) {
            // ... (Log, call aws_iot_mqtt_unsubscribe, handle error, set success = false on error) ...
            IoT_Error_t rc = aws_iot_mqtt_unsubscribe(&client_, topic_filter.c_str(), topic_filter.length());
             if (rc != SUCCESS) { /* Log, handle disconnect, set success = false */ success = false; }
        }
        return success;
    }

    /** @brief Internal helper: Unsubscribes all shadow topics. Assumes lock NOT held.*/
    bool unsubscribe_from_shadow_updates_internal() {
        if (!shadow_subscribed_) { return true; }
        if (!config_loaded_ || config_.thing_name.empty()) { return false; }
        ESP_LOGI(kTag, "Unsubscribing from shadow topics...");
        bool success = true;
        std::string prefix = "$aws/things/" + config_.thing_name + "/shadow/";
        success &= unsubscribe_internal(prefix + "update/accepted");
        success &= unsubscribe_internal(prefix + "update/rejected");
        success &= unsubscribe_internal(prefix + "update/delta");
        success &= unsubscribe_internal(prefix + "get/accepted");
        success &= unsubscribe_internal(prefix + "get/rejected");
        set_shadow_handler_internal(nullptr);
        shadow_subscribed_ = false;
        ESP_LOGI(kTag, "Shadow unsubscribe complete (Success: %d).", success);
        return success;
    }
   /** @brief Internal helper: Unsubscribes all job topics. Assumes lock NOT held.*/
     bool unsubscribe_from_job_notifications_internal() {
        if (!jobs_subscribed_) { return true; }
        if (!config_loaded_ || config_.thing_name.empty()) { return false; }
        ESP_LOGI(kTag, "Unsubscribing from job topics...");
        bool success = true;
        std::string prefix = "$aws/things/" + config_.thing_name + "/jobs/";
        success &= unsubscribe_internal(prefix + "notify-next");
        // Add others if subscribed
        set_job_handler_internal(nullptr);
        jobs_subscribed_ = false;
        ESP_LOGI(kTag, "Jobs unsubscribe complete (Success: %d).", success);
        return success;
    }
   /** @brief Internal helper: Set shadow handler with lock. */
    bool set_shadow_handler_internal(AwsIotInterface::ShadowUpdateHandler handler) {
         std::lock_guard<std::mutex> lock(mutex_);
         shadow_update_handler_ = std::move(handler);
         return true; // std::lock_guard throws on error, so always true if we get here
    }
   /** @brief Internal helper: Set job handler with lock. */
    bool set_job_handler_internal(AwsIotInterface::JobNotificationHandler handler) {
         std::lock_guard<std::mutex> lock(mutex_);
         job_notification_handler_ = std::move(handler);
         return true;
    }


  // --- Static C Callback Wrappers ---
  /** @brief Static C callback for ALL MQTT message subscriptions. */
  static void aws_subscribe_callback(AWS_IoT_Client* client, char* topic_name,
                                   uint16_t topic_name_len,
                                   IoT_Publish_Message_Params* params,
                                   void* user_data) {
      // ... (validation) ...
      AwsIotClientImpl* instance = static_cast<AwsIotClientImpl*>(user_data);
      std::string_view topic_sv(topic_name, topic_name_len);
      std::string_view payload_sv(/*...*/);
      ESP_LOGD(kTag, "aws_subscribe_callback: Msg on '%.*s'", /*...*/);
      AwsIotInterface::MessageHandler handler_to_call;
      {
          std::lock_guard<std::mutex> lock(instance->mutex_); // Use std::lock_guard
          std::string topic_str(topic_sv);
          auto it = instance->message_handlers_.find(topic_str);
          if (it != instance->message_handlers_.end()) { handler_to_call = it->second; }
          else { /* Log warning */ }
      }
      if (handler_to_call) { /* Invoke handler with try-catch */ }
      else { /* Log warning */ }
  }

  /** @brief Static C callback for SDK disconnect events. */
  static void aws_disconnect_callback(AWS_IoT_Client* client, void* user_data) {
      // ... (validation) ...
      AwsIotClientImpl* instance = static_cast<AwsIotClientImpl*>(user_data);
      bool was_connected = instance->is_connected_.exchange(false);
      instance->shadow_subscribed_ = false; instance->jobs_subscribed_ = false;
      if (was_connected) {
           ESP_LOGW(kTag, "Client marked disconnected by SDK callback.");
           instance->trigger_disconnected_callback();
           // No need to explicitly stop task if auto-reconnect is off,
           // disconnect_internal or destructor handles task lifecycle now.
           if (!instance->config_.auto_reconnect) { ESP_LOGW(kTag,"Auto-reconnect disabled."); }
           else { ESP_LOGI(kTag,"Auto-reconnect enabled. Yield task will handle retries."); }
      } else { ESP_LOGD(kTag, "disconnect_cb: Ignoring (already disconnected)."); }
  }

  // --- Background Task ---
  /** @brief Static function: Entry point for the FreeRTOS yield task. */
   static void yield_task_runner(void* param) {
        AwsIotClientImpl* instance = static_cast<AwsIotClientImpl*>(param);
        if (!instance) { ESP_LOGE(kTag,"Yield task null instance!"); vTaskDelete(nullptr); return; }
        ESP_LOGI(kTag, "Internal yield task starting loop (Client ID: %s).", instance->config_.client_id.c_str());
        while (instance->should_yield_task_run_.load()) {
            // Call SDK yield directly - no need for instance->yield() wrapper anymore
             if (instance->client_initialized_) { // Only yield if client is valid
                 aws_iot_mqtt_yield(&(instance->client_), kDefaultYieldTimeoutMs);
                 // Handle yield return codes? Disconnect callback handles most state changes.
             } else {
                  // Client not initialized, avoid yielding, maybe sleep briefly?
                  vTaskDelay(pdMS_TO_TICKS(100));
             }
        }
         ESP_LOGI(kTag, "Internal yield task stopping loop (Client ID: %s).", instance->config_.client_id.c_str());
         vTaskDelete(nullptr); // Delete self before exiting function
   }

};  // class AwsIotClientImpl

// --- Concrete Pimpl Class (Implements AwsIotInterface) ---
class AwsIotClient final : public AwsIotInterface {
 public:
  AwsIotClient() { try { pimpl_.reset(new AwsIotClientImpl()); } catch (...) { pimpl_ = nullptr; ESP_LOGE(kTag,"Exc constructing Impl");} }
  ~AwsIotClient() override = default;
  AwsIotClient(const AwsIotClient&) = delete;
  AwsIotClient& operator=(const AwsIotClient&) = delete;
  AwsIotClient(AwsIotClient&&) = delete;
  AwsIotClient& operator=(AwsIotClient&&) = delete;

  // --- Method Forwarding ---
  bool load_configuration(const AwsIotConfig& config) override { return pimpl_ ? pimpl_->load_configuration(config) : false; }
  bool set_root_ca(const char* d, size_t l) override { return pimpl_ ? pimpl_->set_root_ca(d, l) : false; }
  bool set_client_cert(const char* d, size_t l) override { return pimpl_ ? pimpl_->set_client_cert(d, l) : false; }
  bool set_client_key(const char* d, size_t l) override { return pimpl_ ? pimpl_->set_client_key(d, l) : false; }
  bool connect() override { return pimpl_ ? pimpl_->connect() : false; }
  bool disconnect() override { return pimpl_ ? pimpl_->disconnect() : false; }
  bool is_connected() const override { return pimpl_ ? pimpl_->is_connected() : false; }
  bool publish(std::string_view t, std::string_view p, int q = 0) override { return pimpl_ ? pimpl_->publish(t, p, q) : false; }
  bool subscribe(std::string_view t, int q, MessageHandler h) override { return pimpl_ ? pimpl_->subscribe(t, q, std::move(h)) : false; }
  bool unsubscribe(std::string_view t) override { return pimpl_ ? pimpl_->unsubscribe(t) : false; }
  bool subscribe_to_shadow_updates(ShadowUpdateHandler h) override { return pimpl_ ? pimpl_->subscribe_to_shadow_updates(std::move(h)) : false; }
  bool unsubscribe_from_shadow_updates() override { return pimpl_ ? pimpl_->unsubscribe_from_shadow_updates() : false; }
  bool publish_shadow_update(std::string_view p, int q = 1) override { return pimpl_ ? pimpl_->publish_shadow_update(p, q) : false; }
  bool publish_shadow_get_request() override { return pimpl_ ? pimpl_->publish_shadow_get_request() : false; }
  bool subscribe_to_job_notifications(JobNotificationHandler h) override { return pimpl_ ? pimpl_->subscribe_to_job_notifications(std::move(h)) : false; }
  bool unsubscribe_from_job_notifications() override { return pimpl_ ? pimpl_->unsubscribe_from_job_notifications() : false; }
  bool publish_job_execution_update(std::string_view j, std::string_view s, std::string_view d = "{}") override { return pimpl_ ? pimpl_->publish_job_execution_update(j, s, d) : false; }
  void set_on_connected_callback(ConnectionCallback h) override { if (pimpl_) pimpl_->set_on_connected_callback(std::move(h)); }
  void set_on_disconnected_callback(ConnectionCallback h) override { if (pimpl_) pimpl_->set_on_disconnected_callback(std::move(h)); }
  // yield() method removed

 private:
  std::unique_ptr<AwsIotClientImpl> pimpl_;
};  // class AwsIotClient


// --- Factory Function Implementation ---
std::unique_ptr<AwsIotInterface> create_aws_iot_client() {
  ESP_LOGD(kTag, "create_aws_iot_client factory called.");
  auto client = std::unique_ptr<AwsIotClient>(new (std::nothrow) AwsIotClient());
  if (!client) {
    ESP_LOGE(kTag, "Factory: Failed memory alloc for AwsIotClient.");
    return nullptr;
  }
  // Add check if pimpl is valid if necessary, depends on AwsIotClient constructor guarantees
  ESP_LOGI(kTag, "Factory: AwsIotClient instance created.");
  return client;
}

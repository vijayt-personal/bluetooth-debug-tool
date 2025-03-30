#include "aws_iot_mqtt_client.hpp"

#include <cstring> // For strncpy, strcmp, strlen, memset
#include <cstdio>  // For snprintf
#include <algorithm> // For std::min
// #include <chrono> // No longer needed

// ESP-IDF specific includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h" // Using FreeRTOS timers
#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "esp_tls.h"

// ESP IDF Logging tag
static const char* TAG = "AwsIotMqttClient";

namespace AwsIot {

// --- Constructor / Destructor --- (No changes)
AwsIotMqttClient::AwsIotMqttClient() :
    reconnect_timer_handle_(nullptr)
{
    for (auto& sub : subscriptions_) {
        sub.active = false;
        sub.pending_subscribe = false;
    }
}

AwsIotMqttClient::~AwsIotMqttClient() {
    ESP_LOGI(TAG, "Destructor called.");
    Disconnect();
}


// --- Initialization and Connection --- (No changes)
bool AwsIotMqttClient::Initialize(const MqttConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized.");
        return true;
    }
    if (config.aws_endpoint.empty() || config.client_id.empty() || config.thing_name.empty() ||
        !config.root_ca_pem || !config.device_cert_pem || !config.private_key_pem) {
        ESP_LOGE(TAG, "Initialization failed: Missing required configuration parameters.");
        return false;
    }
    config_ = config;
    initialized_ = true;
    disconnect_requested_ = false;
    ESP_LOGI(TAG, "Client initialized for endpoint: %s, ClientID: %s",
             config_.aws_endpoint.c_str(), config_.client_id.c_str());
    return true;
}

bool AwsIotMqttClient::InitializeMqttClient() {
    // Assumes mutex_ is already locked
    if (client_handle_) {
        ESP_LOGW(TAG, "MQTT client handle already exists. Cleaning up first.");
        CleanupMqttClient();
    }
    esp_mqtt_client_config_t mqtt_cfg = {};
    std::string uri = "mqtts://" + config_.aws_endpoint + ":" + std::to_string(config_.port);
    mqtt_cfg.broker.address.uri = uri.c_str();
    mqtt_cfg.broker.verification.certificate = config_.root_ca_pem;
    mqtt_cfg.credentials.authentication.certificate = config_.device_cert_pem;
    mqtt_cfg.credentials.authentication.key = config_.private_key_pem;
    mqtt_cfg.credentials.client_id = config_.client_id.c_str();
    mqtt_cfg.buffer.size = config_.rx_buffer_size;
    mqtt_cfg.buffer.out_size = config_.tx_buffer_size;
    ESP_LOGI(TAG, "MQTT RX buffer: %d, TX buffer: %d", mqtt_cfg.buffer.size, mqtt_cfg.buffer.out_size);
    mqtt_cfg.session.keepalive = 60;
    mqtt_cfg.user_context = this;
    mqtt_cfg.network.disable_auto_reconnect = true;
    ESP_LOGI(TAG, "Initializing ESP MQTT client...");
    client_handle_ = esp_mqtt_client_init(&mqtt_cfg);
    if (!client_handle_) {
        ESP_LOGE(TAG, "Failed to initialize ESP MQTT client");
        return false;
    }
    ESP_LOGI(TAG, "Registering ESP MQTT event handler...");
    esp_err_t err = esp_mqtt_client_register_event(client_handle_, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, MqttEventHandlerStatic, this);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(client_handle_);
        client_handle_ = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "ESP MQTT client initialized successfully.");
    return true;
}

void AwsIotMqttClient::CleanupMqttClient() {
    // Assumes mutex_ is already locked
    if (client_handle_) {
        ESP_LOGI(TAG, "Cleaning up MQTT client...");
        esp_err_t stop_err = esp_mqtt_client_stop(client_handle_);
        if (stop_err != ESP_OK && stop_err != ESP_ERR_INVALID_STATE) {
             ESP_LOGW(TAG, "Failed to stop MQTT client: %s", esp_err_to_name(stop_err));
        }
        esp_err_t destroy_err = esp_mqtt_client_destroy(client_handle_);
         if (destroy_err != ESP_OK) {
             ESP_LOGW(TAG, "Failed to destroy MQTT client: %s", esp_err_to_name(destroy_err));
         }
        client_handle_ = nullptr;
        ESP_LOGI(TAG, "MQTT client cleaned up.");
    }
    connected_ = false;
    connecting_ = false;
}

bool AwsIotMqttClient::Connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        ESP_LOGE(TAG, "Cannot connect: Client not initialized.");
        return false;
    }
    if (connected_ || connecting_) {
        ESP_LOGW(TAG, "Cannot connect: Already connected or connecting.");
        return !connected_;
    }
     if (disconnect_requested_) {
        ESP_LOGW(TAG, "Cannot connect: Disconnect recently requested.");
        return false;
    }
    ESP_LOGI(TAG, "Connect requested.");
    connecting_ = true;
    disconnect_requested_ = false;
    if (!InitializeMqttClient()) {
        connecting_ = false;
        return false;
    }
    ESP_LOGI(TAG, "Starting ESP MQTT client task...");
    esp_err_t start_err = esp_mqtt_client_start(client_handle_);
    if (start_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(start_err));
        CleanupMqttClient();
        connecting_ = false;
        return false;
    }
    current_reconnect_delay_ms_ = config_.base_reconnect_ms;
    StopReconnectTimer();
    ESP_LOGI(TAG, "MQTT client start initiated. Waiting for connection event...");
    return true;
}

void AwsIotMqttClient::Disconnect() {
     ESP_LOGI(TAG, "Disconnect requested.");
    {
        std::lock_guard<std::mutex> lock(mutex_);
        disconnect_requested_ = true;
        StopReconnectTimer();
        CleanupMqttClient();
    }
    ESP_LOGI(TAG, "Client disconnected.");
}

bool AwsIotMqttClient::IsConnected() const {
    return connected_.load();
}


// --- Publish / Subscribe / Unsubscribe ---

bool AwsIotMqttClient::Publish(const std::string& topic, const std::string& payload, int qos, bool retain) {
    // Forward to string_view version
    return Publish(topic, std::string_view(payload), qos, retain);
}

bool AwsIotMqttClient::Publish(const std::string& topic, std::string_view payload, int qos, bool retain) {
    // Forward to raw data version
     return Publish(topic, reinterpret_cast<const uint8_t*>(payload.data()), payload.length(), qos, retain);
}

// --- NEW OVERLOAD ---
bool AwsIotMqttClient::Publish(const std::string& topic, const uint8_t* payload, size_t len, int qos, bool retain) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_ || !client_handle_) {
        ESP_LOGW(TAG, "Cannot publish: Not connected.");
        return false;
    }
    if (topic.length() >= kMaxTopicLen) {
         ESP_LOGE(TAG, "Cannot publish: Topic length exceeds maximum (%d).", kMaxTopicLen);
         return false;
    }
     if (payload == nullptr && len > 0) {
         ESP_LOGE(TAG, "Cannot publish: Null payload with non-zero length.");
         return false;
     }

    // Note: esp_mqtt_client_publish takes 'const char *data'. Casting uint8_t* is generally safe
    // as long as the data is intended to be treated as a byte sequence.
    int msg_id = esp_mqtt_client_publish(client_handle_,
                                         topic.c_str(),
                                         reinterpret_cast<const char*>(payload),
                                         static_cast<int>(len), // ESP-MQTT uses int for length
                                         qos,
                                         retain);

    if (msg_id == -1) {
        // This can happen if the output buffer is full (for QoS 0), or other internal errors.
        ESP_LOGE(TAG, "Failed to publish message to topic '%s' (len %d, qos %d). Might be buffer full or other error.", topic.c_str(), len, qos);
        return false;
    }

    ESP_LOGD(TAG, "Publish queued to topic '%s', msg_id=%d, len=%d", topic.c_str(), msg_id, (int)len);
    return true;
}
// --- END NEW OVERLOAD ---


bool AwsIotMqttClient::Subscribe(const std::string& topic_filter, int qos, MqttMessageCallback callback) {
    // (No changes from previous version)
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        ESP_LOGE(TAG, "Cannot subscribe: Client not initialized.");
        return false;
    }
     if (topic_filter.length() >= kMaxTopicLen) {
         ESP_LOGE(TAG, "Cannot subscribe: Topic filter length exceeds maximum (%d).", kMaxTopicLen);
         return false;
     }
    int available_slot = -1;
    int existing_slot = -1;
    for (int i = 0; i < kMaxSubs; ++i) {
        if (subscriptions_[i].active) {
            if (strncmp(subscriptions_[i].topic, topic_filter.c_str(), kMaxTopicLen) == 0) {
                existing_slot = i; break; }
        } else if (available_slot == -1) { available_slot = i; }
    }
    int target_slot = -1;
    bool new_subscription = false;
    if (existing_slot != -1) {
        ESP_LOGI(TAG, "Updating existing subscription for topic: %s", topic_filter.c_str());
        target_slot = existing_slot;
    } else if (available_slot != -1) {
        ESP_LOGI(TAG, "Adding new subscription for topic: %s", topic_filter.c_str());
        target_slot = available_slot; new_subscription = true;
    } else {
        ESP_LOGE(TAG, "Cannot subscribe: Maximum number of subscriptions (%d) reached.", kMaxSubs); return false;
    }
    subscriptions_[target_slot].qos = qos;
    subscriptions_[target_slot].callback = std::move(callback);
    subscriptions_[target_slot].pending_subscribe = true;
    if (new_subscription) {
        strncpy(subscriptions_[target_slot].topic, topic_filter.c_str(), kMaxTopicLen - 1);
        subscriptions_[target_slot].topic[kMaxTopicLen - 1] = '\0';
        subscriptions_[target_slot].active = true;
        active_subscription_count_++;
    }
    if (connected_ && client_handle_) {
       if (SubscribeInternal(subscriptions_[target_slot].topic, subscriptions_[target_slot].qos)) {
           subscriptions_[target_slot].pending_subscribe = false;
       } else { return false; }
    } else {
        ESP_LOGI(TAG, "Subscription to '%s' is pending connection.", topic_filter.c_str());
    }
    return true;
}

bool AwsIotMqttClient::SubscribeInternal(const char* topic_filter, int qos) {
    // (No changes from previous version)
    ESP_LOGI(TAG, "Subscribing to topic '%s' with QoS %d", topic_filter, qos);
    int msg_id = esp_mqtt_client_subscribe(client_handle_, topic_filter, qos);
    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to subscribe to topic '%s'", topic_filter); return false;
    }
    ESP_LOGD(TAG, "Subscribe request sent for topic '%s', msg_id=%d", topic_filter, msg_id); return true;
}

bool AwsIotMqttClient::Unsubscribe(const std::string& topic_filter) {
    // (No changes from previous version)
     std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
         ESP_LOGE(TAG, "Cannot unsubscribe: Client not initialized."); return false;
    }
    int found_slot = -1;
    for (int i = 0; i < kMaxSubs; ++i) {
        if (subscriptions_[i].active && strncmp(subscriptions_[i].topic, topic_filter.c_str(), kMaxTopicLen) == 0) {
            found_slot = i; break;
        }
    }
    if (found_slot == -1) {
        ESP_LOGW(TAG, "Cannot unsubscribe: Topic '%s' not found.", topic_filter.c_str()); return false;
    }
    char topic_copy[kMaxTopicLen];
    strncpy(topic_copy, subscriptions_[found_slot].topic, kMaxTopicLen -1);
    topic_copy[kMaxTopicLen -1] = '\0';
    subscriptions_[found_slot].active = false;
    subscriptions_[found_slot].pending_subscribe = false;
    subscriptions_[found_slot].callback = nullptr;
    memset(subscriptions_[found_slot].topic, 0, kMaxTopicLen);
    active_subscription_count_--;
    ESP_LOGI(TAG, "Removed internal subscription for topic: %s", topic_copy);
    if (connected_ && client_handle_) {
        if (!UnsubscribeInternal(topic_copy)) { return false; }
    }
    return true;
}


bool AwsIotMqttClient::UnsubscribeInternal(const char* topic_filter) {
     // (No changes from previous version)
    ESP_LOGI(TAG, "Unsubscribing from topic '%s'", topic_filter);
    int msg_id = esp_mqtt_client_unsubscribe(client_handle_, topic_filter);
     if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to unsubscribe from topic '%s'", topic_filter); return false;
    }
    ESP_LOGD(TAG, "Unsubscribe request sent for topic '%s', msg_id=%d", topic_filter, msg_id); return true;
}


// --- Event Handling --- (No changes)
/*static*/ void AwsIotMqttClient::MqttEventHandlerStatic(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    AwsIotMqttClient* client = static_cast<AwsIotMqttClient*>(handler_args);
    if (client) { client->HandleMqttEvent(event_data); }
    else { ESP_LOGE(TAG, "Received MQTT event with null handler argument!"); }
}

void AwsIotMqttClient::HandleMqttEvent(void* event_data) {
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);
    std::lock_guard<std::mutex> lock(mutex_);
    if (disconnect_requested_) { ESP_LOGD(TAG, "Ignoring MQTT event %d", (int)event->event_id); return; }
    switch (event->event_id) {
        case MQTT_EVENT_BEFORE_CONNECT: break;
        case MQTT_EVENT_CONNECTED: ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED"); HandleConnect(); break;
        case MQTT_EVENT_DISCONNECTED: ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED"); HandleDisconnect(); break;
        case MQTT_EVENT_SUBSCRIBED: ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id); break;
        case MQTT_EVENT_UNSUBSCRIBED: ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id); break;
        case MQTT_EVENT_PUBLISHED: ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id); break;
        case MQTT_EVENT_DATA: ESP_LOGI(TAG, "MQTT_EVENT_DATA"); HandleData(event->topic, event->topic_len, event->data, event->data_len); break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle) {
                ESP_LOGE(TAG, "ESP-TLS Last ESP Error: 0x%x (%s)", event->error_handle->esp_tls_last_esp_err, esp_err_to_name(event->error_handle->esp_tls_last_esp_err));
                 if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) { ESP_LOGE(TAG, "Connection Refused, ErrorCode: 0x%x", event->error_handle->connect_return_code); }
            }
             if (connecting_) { connecting_ = false; ESP_LOGW(TAG,"Connection attempt failed, waiting for disconnect event."); }
             else if (!connected_) { HandleDisconnect(); }
            break;
         case MQTT_EVENT_DELETED: ESP_LOGI(TAG, "MQTT_EVENT_DELETED"); client_handle_ = nullptr; connected_ = false; connecting_ = false; break;
        default: ESP_LOGI(TAG, "Other event id: %d", (int)event->event_id); break;
    }
}

void AwsIotMqttClient::HandleConnect() {
    // (No changes)
    ESP_LOGI(TAG, "Successfully connected to MQTT broker.");
    connected_ = true; connecting_ = false; disconnect_requested_ = false;
    current_reconnect_delay_ms_ = config_.base_reconnect_ms; StopReconnectTimer();
    ResubscribePending();
    if (on_connected_cb_) { on_connected_cb_(); }
}

void AwsIotMqttClient::HandleDisconnect() {
    // (No changes)
    bool was_connected = connected_; connected_ = false; connecting_ = false;
    if (was_connected && on_disconnected_cb_) { on_disconnected_cb_(); }
    for (auto& sub : subscriptions_) { if (sub.active) { sub.pending_subscribe = true; } }
    if (!disconnect_requested_) { ESP_LOGW(TAG, "Unexpected disconnect. Scheduling reconnect..."); ScheduleReconnect(); }
    else { ESP_LOGI(TAG, "Disconnect event received after manual request."); if (client_handle_) { ESP_LOGW(TAG,"Cleaning up client handle in disconnect handler (safety net)."); CleanupMqttClient(); } }
}

void AwsIotMqttClient::ResubscribePending() {
    // (No changes)
    ESP_LOGI(TAG, "Resubscribing to pending topics..."); int count = 0;
    for (auto& sub : subscriptions_) {
        if (sub.active && sub.pending_subscribe) {
            if (SubscribeInternal(sub.topic, sub.qos)) { sub.pending_subscribe = false; count++; }
            else { ESP_LOGE(TAG, "Failed to resubscribe to topic: %s", sub.topic); }
        }
    } ESP_LOGI(TAG, "Attempted to resubscribe to %d topics.", count);
}


void AwsIotMqttClient::HandleData(const char* topic, int topic_len, const char* data, int data_len) {
    // (No changes - logic remains the same)
    if (!topic || !data) return;
    std::string topic_str(topic, topic_len);
    std::string_view payload_view(data, data_len);
    ESP_LOGD(TAG, "Searching callback for topic: %s", topic_str.c_str());
    bool handled = false;
    char shadow_prefix_buf[kMaxTopicLen];
    snprintf(shadow_prefix_buf, sizeof(shadow_prefix_buf), "$aws/things/%s/shadow/", config_.thing_name.c_str());
    size_t shadow_prefix_len = strlen(shadow_prefix_buf);
    if (topic_len > shadow_prefix_len && strncmp(topic, shadow_prefix_buf, shadow_prefix_len) == 0) {
        const char* suffix = topic + shadow_prefix_len; size_t suffix_len = topic_len - shadow_prefix_len;
        if ((strncmp(suffix, "update/", 7) == 0 && suffix_len > 7) || (strncmp(suffix, "delta", 5) == 0 && suffix_len == 5)) {
             if (shadow_update_cb_) {
                std::string type;
                if (suffix[0] == 'd') { type = "delta"; } else { type.assign(suffix + 7, suffix_len - 7); }
                ESP_LOGD(TAG, "Invoking shadow update callback for type '%s'", type.c_str()); shadow_update_cb_(type, payload_view); handled = true;
             }
        } else if (strncmp(suffix, "get/", 4) == 0 && suffix_len > 4) {
             if (shadow_get_cb_) {
                std::string type(suffix + 4, suffix_len - 4);
                ESP_LOGD(TAG, "Invoking shadow get callback for type '%s'", type.c_str()); shadow_get_cb_(type, payload_view); handled = true;
             }
        }
    }
    char jobs_notify_buf[kMaxTopicLen]; char jobs_update_prefix_buf[kMaxTopicLen];
    snprintf(jobs_notify_buf, sizeof(jobs_notify_buf), "$aws/things/%s/jobs/notify-next", config_.thing_name.c_str());
    snprintf(jobs_update_prefix_buf, sizeof(jobs_update_prefix_buf), "$aws/things/%s/jobs/", config_.thing_name.c_str());
    size_t jobs_update_prefix_len = strlen(jobs_update_prefix_buf);
    if (!handled && topic_str == jobs_notify_buf) {
        if (job_notify_cb_) {
            ESP_LOGD(TAG, "Invoking job notification callback (notify-next)"); job_notify_cb_("unknown_job_id", "QUEUED", std::string(payload_view)); handled = true;
        }
    } else if (!handled && topic_len > jobs_update_prefix_len && strncmp(topic, jobs_update_prefix_buf, jobs_update_prefix_len) == 0) {
         const char* job_suffix = topic + jobs_update_prefix_len; size_t job_suffix_len = topic_len - jobs_update_prefix_len;
         const char* update_accepted_str = "/update/accepted"; const char* update_rejected_str = "/update/rejected";
         size_t accepted_len = strlen(update_accepted_str); size_t rejected_len = strlen(update_rejected_str);
         std::string job_id_str; std::string status_str;
         if (job_suffix_len > accepted_len && strcmp(job_suffix + job_suffix_len - accepted_len, update_accepted_str) == 0) {
             job_id_str.assign(job_suffix, job_suffix_len - accepted_len); status_str = "ACCEPTED";
         } else if (job_suffix_len > rejected_len && strcmp(job_suffix + job_suffix_len - rejected_len, update_rejected_str) == 0) {
             job_id_str.assign(job_suffix, job_suffix_len - rejected_len); status_str = "REJECTED";
         }
         if (!job_id_str.empty() && job_notify_cb_) {
             ESP_LOGD(TAG, "Invoking job notification callback (job update %s for %s)", status_str.c_str(), job_id_str.c_str());
             job_notify_cb_(job_id_str, status_str, std::string(payload_view)); handled = true;
         }
    }
    if (!handled) {
        for (const auto& sub : subscriptions_) {
            if (sub.active && sub.callback && (topic_str == sub.topic)) {
                 ESP_LOGD(TAG, "Invoking generic callback for topic: %s", sub.topic); sub.callback(topic_str, payload_view); handled = true; break;
            }
        }
    }
    if (!handled) { ESP_LOGD(TAG, "No suitable callback found for topic: %s", topic_str.c_str()); }
}


// --- Reconnect Logic --- (No changes - uses FreeRTOS Timer)
/*static*/ void AwsIotMqttClient::ReconnectTimerCallbackStatic(void* timer_arg) {
     AwsIotMqttClient* client = static_cast<AwsIotMqttClient*>(timer_arg);
     if (client) { ESP_LOGI(TAG, "Reconnect timer fired. Attempting connection..."); client->Connect(); }
}

void AwsIotMqttClient::StartReconnectTimer() {
     // Assumes mutex_ is locked
     if (!reconnect_timer_handle_) {
        reconnect_timer_handle_ = xTimerCreate("MqttRecTmr", pdMS_TO_TICKS(current_reconnect_delay_ms_), pdFALSE, this, ReconnectTimerCallbackStatic);
         if (!reconnect_timer_handle_) { ESP_LOGE(TAG, "Failed to create reconnect timer!"); return; }
     }
    if (xTimerChangePeriod(reconnect_timer_handle_, pdMS_TO_TICKS(current_reconnect_delay_ms_), pdMS_TO_TICKS(100)) != pdPASS) { ESP_LOGE(TAG, "Failed to set reconnect timer period!"); }
    if (xTimerStart(reconnect_timer_handle_, pdMS_TO_TICKS(100)) != pdPASS) { ESP_LOGE(TAG, "Failed to start reconnect timer!"); }
    else { ESP_LOGI(TAG, "Reconnect timer started. Will attempt connection in %lu ms.", current_reconnect_delay_ms_); }
}

void AwsIotMqttClient::StopReconnectTimer() {
     // Assumes mutex_ is locked
    if (reconnect_timer_handle_ && xTimerIsTimerActive(reconnect_timer_handle_)) {
        xTimerStop(reconnect_timer_handle_, pdMS_TO_TICKS(100)); ESP_LOGI(TAG, "Reconnect timer stopped.");
    }
}

void AwsIotMqttClient::ScheduleReconnect() {
     // Assumes mutex_ is locked
     StopReconnectTimer();
     current_reconnect_delay_ms_ *= 2;
     if (current_reconnect_delay_ms_ > config_.max_reconnect_ms || current_reconnect_delay_ms_ < config_.base_reconnect_ms) { current_reconnect_delay_ms_ = config_.max_reconnect_ms; }
      if (current_reconnect_delay_ms_ < config_.base_reconnect_ms) { current_reconnect_delay_ms_ = config_.base_reconnect_ms; }
     ESP_LOGI(TAG, "Scheduling reconnect attempt in %lu ms", current_reconnect_delay_ms_); StartReconnectTimer();
}


// --- AWS IoT Specific Helpers --- (No changes needed in logic)
bool AwsIotMqttClient::GetShadowTopic(const std::string& operation, char* buffer, size_t buffer_size) {
    int written = snprintf(buffer, buffer_size, "$aws/things/%s/shadow/%s", config_.thing_name.c_str(), operation.c_str());
    return (written > 0 && written < buffer_size);
}
bool AwsIotMqttClient::GetJobsTopic(const std::string& operation, const std::string& job_id, char* buffer, size_t buffer_size) {
    int written;
    if (job_id.empty() || job_id == "+") { written = snprintf(buffer, buffer_size, "$aws/things/%s/jobs/%s", config_.thing_name.c_str(), operation.c_str()); }
    else { written = snprintf(buffer, buffer_size, "$aws/things/%s/jobs/%s/%s", config_.thing_name.c_str(), job_id.c_str(), operation.c_str()); }
    return (written > 0 && written < buffer_size);
}
bool AwsIotMqttClient::SubscribeToShadowUpdates(ShadowUpdateCallback callback) {
     shadow_update_cb_ = std::move(callback); bool success = true; char topic_buf[kMaxTopicLen];
     if (GetShadowTopic("update/accepted", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Shadow topic buffer too small!"); }
     if (GetShadowTopic("update/rejected", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Shadow topic buffer too small!"); }
     if (GetShadowTopic("update/delta", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Shadow topic buffer too small!"); }
     return success;
}
bool AwsIotMqttClient::SubscribeToShadowGetResponses(ShadowUpdateCallback callback) {
    shadow_get_cb_ = std::move(callback); bool success = true; char topic_buf[kMaxTopicLen];
     if (GetShadowTopic("get/accepted", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Shadow topic buffer too small!"); }
     if (GetShadowTopic("get/rejected", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Shadow topic buffer too small!"); }
     return success;
}
bool AwsIotMqttClient::UpdateShadow(const std::string& shadow_payload, int qos) { return UpdateShadow(std::string_view(shadow_payload), qos); }
bool AwsIotMqttClient::UpdateShadow(std::string_view shadow_payload, int qos) {
    char topic_buf[kMaxTopicLen];
    if (!GetShadowTopic("update", topic_buf, sizeof(topic_buf))) { ESP_LOGE(TAG, "Shadow topic buffer too small!"); return false; }
    return Publish(topic_buf, reinterpret_cast<const uint8_t*>(shadow_payload.data()), shadow_payload.length(), qos); // Use new overload
}
bool AwsIotMqttClient::GetShadow(const std::string& client_token) {
    char topic_buf[kMaxTopicLen];
    if (!GetShadowTopic("get", topic_buf, sizeof(topic_buf))) { ESP_LOGE(TAG, "Shadow topic buffer too small!"); return false; }
    char payload_buf[128];
    int len;
    if (client_token.empty()) { len = snprintf(payload_buf, sizeof(payload_buf), "{}"); }
    else { len = snprintf(payload_buf, sizeof(payload_buf), "{\"clientToken\":\"%.*s\"}", (int)client_token.length(), client_token.c_str()); }
    if (len <= 0 || len >= sizeof(payload_buf)) { ESP_LOGE(TAG, "Client token payload buffer too small or snprintf error!"); len = snprintf(payload_buf, sizeof(payload_buf), "{}"); }
    return Publish(topic_buf, reinterpret_cast<const uint8_t*>(payload_buf), len, 0); // Use new overload
}
bool AwsIotMqttClient::SubscribeToJobs(JobNotificationCallback callback) {
    job_notify_cb_ = std::move(callback); bool success = true; char topic_buf[kMaxTopicLen];
    if (GetJobsTopic("notify-next", "", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Jobs topic buffer too small!"); }
    if (GetJobsTopic("update/accepted", "+", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Jobs topic buffer too small!"); }
    if (GetJobsTopic("update/rejected", "+", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Jobs topic buffer too small!"); }
    return success;
}
bool AwsIotMqttClient::UpdateJobStatus(const std::string& job_id, const std::string& status, const std::string& status_details_json) { return UpdateJobStatus(job_id, status, std::string_view(status_details_json)); }
bool AwsIotMqttClient::UpdateJobStatus(const std::string& job_id, const std::string& status, std::string_view status_details_json) {
    if (job_id.empty() || status.empty()) { ESP_LOGE(TAG, "Job ID and Status cannot be empty"); return false; }
    char topic_buf[kMaxTopicLen];
    if (!GetJobsTopic("update", job_id, topic_buf, sizeof(topic_buf))) { ESP_LOGE(TAG, "Jobs topic buffer too small!"); return false; }
    char payload_buf[kMaxPayloadLen];
    int written = snprintf(payload_buf, sizeof(payload_buf), "{\"status\":\"%.*s\",\"statusDetails\":%.*s}", (int)status.length(), status.c_str(), (int)status_details_json.length(), status_details_json.data());
    if (written <= 0 || written >= sizeof(payload_buf)) { ESP_LOGE(TAG, "UpdateJobStatus payload buffer too small or snprintf error!"); return false; }
    return Publish(topic_buf, reinterpret_cast<const uint8_t*>(payload_buf), written, 1); // Use new overload
}

// --- Setters for Callbacks --- (No changes)
void AwsIotMqttClient::SetOnConnectedCallback(StatusCallback cb) { std::lock_guard<std::mutex> lock(mutex_); on_connected_cb_ = std::move(cb); }
void AwsIotMqttClient::SetOnDisconnectedCallback(StatusCallback cb) { std::lock_guard<std::mutex> lock(mutex_); on_disconnected_cb_ = std::move(cb); }

} // namespace AwsIot

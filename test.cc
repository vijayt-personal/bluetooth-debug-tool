#include "aws_iot_mqtt_client.hpp" // Our header first

#include <cstring> // For strncpy, strcmp, strlen, memset
#include <cstdio>  // For snprintf
#include <algorithm> // For std::min

// --- ESP-IDF specific includes --- REQUIRED HERE ONLY ---
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"    // Defines TimerHandle_t, TimerCallbackFunction_t, pvTimerGetTimerID etc.
#include "esp_log.h"
#include "esp_event.h"      // Needed for event handler signature and base types
#include "mqtt_client.h"    // Defines esp_mqtt_client_handle_t, config, events etc.
#include "esp_tls.h"        // For error checking if needed
// --- End ESP-IDF Includes ---

static const char* TAG = "AwsIotMqttClient";

namespace AwsIot {

// Helper macro for casting the opaque handle (optional)
#define GET_MQTT_HANDLE(opaque_handle) (static_cast<esp_mqtt_client_handle_t>(opaque_handle))

// --- Static callback function definitions ---
// Define static functions here, they don't need to be declared in the class definition in the header
// Signature must match esp_event_handler_t (void (void*, esp_event_base_t, int32_t, void*))
static void MqttEventHandlerStatic(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
// Signature must match TimerCallbackFunction_t (void (TimerHandle_t))
static void ReconnectTimerCallbackStatic(TimerHandle_t xTimer);

// --- Constructor / Destructor ---
AwsIotMqttClient::AwsIotMqttClient() :
    reconnect_timer_handle_{nullptr}
{
    // Ensure internal config buffers are initially empty
    memset(&config_, 0, sizeof(config_));
    for (auto& sub : subscriptions_) {
        sub.active = false;
        sub.pending_subscribe = false;
    }
}

AwsIotMqttClient::~AwsIotMqttClient() {
    ESP_LOGI(TAG, "Destructor called.");
    Disconnect(); // Ensure cleanup
}


// --- Initialization and Connection ---

bool AwsIotMqttClient::Initialize(const MqttConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized.");
        return true;
    }

    // *** Critical Check: Ensure certificate buffers in the passed config are populated ***
    if (config.aws_endpoint.empty() || config.client_id.empty() || config.thing_name.empty() ||
        config.root_ca_pem[0] == '\0' ||       // Check if first char is null
        config.device_cert_pem[0] == '\0' ||
        config.private_key_pem[0] == '\0' )
    {
        ESP_LOGE(TAG, "Initialization failed: Missing required config parameters or certificate data not copied into config buffers.");
        return false;
    }

    // Copy the entire configuration struct, including the large certificate arrays
    config_ = config; // This copies the ~6KB+ struct
    initialized_ = true;
    disconnect_requested_ = false;
    ESP_LOGI(TAG, "Client initialized for endpoint: %s, ClientID: %s",
             config_.aws_endpoint.c_str(), config_.client_id.c_str());
    ESP_LOGI(TAG, "Internal config struct size: %d bytes", (int)sizeof(config_));
    // Optional: Log actual lengths if needed, but strlen might be slow on huge strings
    // ESP_LOGI(TAG, "Cert lengths (max %d): CA=%d, Cert=%d, Key=%d", kMaxCertLen,
    //          strlen(config_.root_ca_pem), strlen(config_.device_cert_pem), strlen(config_.private_key_pem));
    return true;
}

bool AwsIotMqttClient::InitializeMqttClient() {
    // Assumes mutex_ is already locked
    if (client_handle_opaque_) {
        ESP_LOGW(TAG, "MQTT client handle already exists. Cleaning up first.");
        CleanupMqttClient();
    }

    esp_mqtt_client_config_t mqtt_cfg = {}; // Zero initialize

    // Check again if internal config certs are valid before using
    if (config_.root_ca_pem[0] == '\0' || config_.device_cert_pem[0] == '\0' || config_.private_key_pem[0] == '\0') {
         ESP_LOGE(TAG, "Internal certificate data is empty during MQTT client init!");
         return false;
    }

    std::string uri = "mqtts://" + config_.aws_endpoint + ":" + std::to_string(config_.port);
    mqtt_cfg.broker.address.uri = uri.c_str(); // Pointer valid during init call

    // Certificates: Point to the arrays *within* the internal config_ member struct
    mqtt_cfg.broker.verification.certificate = config_.root_ca_pem;
    mqtt_cfg.credentials.authentication.certificate = config_.device_cert_pem;
    mqtt_cfg.credentials.authentication.key = config_.private_key_pem;
    mqtt_cfg.credentials.client_id = config_.client_id.c_str();

    // Buffers and settings
    mqtt_cfg.buffer.size = config_.rx_buffer_size;
    mqtt_cfg.buffer.out_size = config_.tx_buffer_size;
    mqtt_cfg.session.keepalive = 60;
    // user_context is NOT set here for compatibility
    mqtt_cfg.network.disable_auto_reconnect = true; // We handle reconnect manually

    ESP_LOGI(TAG, "Initializing ESP MQTT client (esp-mqtt)...");
    // Initialize and store the handle in the opaque void* member
    client_handle_opaque_ = esp_mqtt_client_init(&mqtt_cfg);

    if (!client_handle_opaque_) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return false;
    }

    ESP_LOGI(TAG, "Registering ESP MQTT event handler...");
    // Get concrete handle for registration
    esp_mqtt_client_handle_t handle = GET_MQTT_HANDLE(client_handle_opaque_);
    // Pass 'this' as the event_handler_arg (last parameter)
    // Pass MqttEventHandlerStatic directly - its signature matches esp_event_handler_t
    esp_err_t err = esp_mqtt_client_register_event(handle,
                                                  (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, // Or MQTT_EVENT_ANY? Check IDF version docs if specific needed
                                                  MqttEventHandlerStatic, // Pass function pointer directly
                                                  this); // Pass context ('this' pointer) here
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_register_event failed: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(handle); // Use concrete handle for destroy
        client_handle_opaque_ = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "ESP MQTT client initialized successfully.");
    return true;
}

void AwsIotMqttClient::CleanupMqttClient() {
    // Assumes mutex_ is already locked
    if (client_handle_opaque_) {
        ESP_LOGI(TAG, "Cleaning up MQTT client...");
        esp_mqtt_client_handle_t handle = GET_MQTT_HANDLE(client_handle_opaque_);
        esp_err_t stop_err = esp_mqtt_client_stop(handle);
        // Suppress "invalid state" error if already stopped or never started
        if (stop_err != ESP_OK && stop_err != ESP_ERR_INVALID_STATE) {
             ESP_LOGW(TAG, "esp_mqtt_client_stop failed: %s", esp_err_to_name(stop_err));
        }
        esp_err_t destroy_err = esp_mqtt_client_destroy(handle);
         if (destroy_err != ESP_OK) {
             // This might happen if stop failed badly, log but continue
             ESP_LOGW(TAG, "esp_mqtt_client_destroy failed: %s", esp_err_to_name(destroy_err));
         }
        client_handle_opaque_ = nullptr; // Nullify the opaque handle
        ESP_LOGI(TAG, "MQTT client cleaned up.");
    }
    // Reset state flags
    connected_ = false;
    connecting_ = false;
}

bool AwsIotMqttClient::Connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        ESP_LOGE(TAG, "Cannot connect: Client not initialized."); return false;
    }
    if (connected_ || connecting_) {
        ESP_LOGW(TAG, "Cannot connect: Already connected or connecting."); return !connected_;
    }
     if (disconnect_requested_) {
        ESP_LOGW(TAG, "Cannot connect: Disconnect recently requested."); return false;
    }
    ESP_LOGI(TAG, "Connect requested.");
    connecting_ = true;
    disconnect_requested_ = false;
    if (!InitializeMqttClient()) { // This initializes client_handle_opaque_
        connecting_ = false;
        return false;
    }
    if (!client_handle_opaque_) { // Check if init succeeded
         ESP_LOGE(TAG, "Connect failed: Opaque handle is null after init.");
         connecting_ = false;
         return false;
    }
    // Get concrete handle to start
    esp_mqtt_client_handle_t handle = GET_MQTT_HANDLE(client_handle_opaque_);
    ESP_LOGI(TAG, "Starting ESP MQTT client task...");
    esp_err_t start_err = esp_mqtt_client_start(handle);
    if (start_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start failed: %s", esp_err_to_name(start_err));
        CleanupMqttClient(); // Cleans up using client_handle_opaque_
        connecting_ = false;
        return false;
    }
    // Reset reconnect delay and stop any previous timer
    current_reconnect_delay_ms_ = config_.base_reconnect_ms;
    StopReconnectTimer();
    ESP_LOGI(TAG, "MQTT client start initiated. Waiting for connection event...");
    return true; // Indicates the connection process has started
}

void AwsIotMqttClient::Disconnect() {
     ESP_LOGI(TAG, "Disconnect requested.");
    {
        std::lock_guard<std::mutex> lock(mutex_);
        disconnect_requested_ = true;
        StopReconnectTimer(); // Stop trying to reconnect
        CleanupMqttClient();  // Stop and destroy the client
    }
    ESP_LOGI(TAG, "Client disconnected action complete.");
}

bool AwsIotMqttClient::IsConnected() const {
    // atomic bool read is safe without lock
    return connected_.load();
}

// --- Publish / Subscribe / Unsubscribe --- (Use opaque handle internally)

bool AwsIotMqttClient::Publish(const std::string& topic, const std::string& payload, int qos, bool retain) {
    return Publish(topic, std::string_view(payload), qos, retain); // Forward
}
bool AwsIotMqttClient::Publish(const std::string& topic, std::string_view payload, int qos, bool retain) {
     return Publish(topic, reinterpret_cast<const uint8_t*>(payload.data()), payload.length(), qos, retain); // Forward
}

bool AwsIotMqttClient::Publish(const std::string& topic, const uint8_t* payload, size_t len, int qos, bool retain) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_.load() || !client_handle_opaque_) { // Use load() for atomic read
        ESP_LOGW(TAG, "Cannot publish: Not connected or handle is null.");
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
    esp_mqtt_client_handle_t handle = GET_MQTT_HANDLE(client_handle_opaque_);
    int msg_id = esp_mqtt_client_publish(handle, topic.c_str(), reinterpret_cast<const char*>(payload), static_cast<int>(len), qos, retain);
    if (msg_id == -1) {
        // esp_mqtt_client_publish returns -1 on error (e.g. buffer full, not connected anymore)
        ESP_LOGE(TAG, "esp_mqtt_client_publish failed for topic '%s' (len %d, qos %d).", topic.c_str(), (int)len, qos);
        return false;
    }
    ESP_LOGD(TAG, "Publish queued to topic '%s', msg_id=%d, len=%d", topic.c_str(), msg_id, (int)len);
    return true;
}

bool AwsIotMqttClient::Subscribe(const std::string& topic_filter, int qos, MqttMessageCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) { ESP_LOGE(TAG, "Cannot subscribe: Client not initialized."); return false; }
    if (topic_filter.length() >= kMaxTopicLen) { ESP_LOGE(TAG, "Cannot subscribe: Topic filter too long."); return false; }
    int available_slot = -1; int existing_slot = -1;
    for (int i = 0; i < kMaxSubs; ++i) { if (subscriptions_[i].active) { if (strncmp(subscriptions_[i].topic, topic_filter.c_str(), kMaxTopicLen) == 0) { existing_slot = i; break; } } else if (available_slot == -1) { available_slot = i; } }
    int target_slot = -1; bool new_subscription = false;
    if (existing_slot != -1) { ESP_LOGI(TAG, "Updating subscription: %s", topic_filter.c_str()); target_slot = existing_slot; }
    else if (available_slot != -1) { ESP_LOGI(TAG, "Adding subscription: %s", topic_filter.c_str()); target_slot = available_slot; new_subscription = true; }
    else { ESP_LOGE(TAG, "Cannot subscribe: Max subs (%d) reached.", kMaxSubs); return false; }
    subscriptions_[target_slot].qos = qos; subscriptions_[target_slot].callback = std::move(callback); subscriptions_[target_slot].pending_subscribe = true;
    if (new_subscription) { strncpy(subscriptions_[target_slot].topic, topic_filter.c_str(), kMaxTopicLen - 1); subscriptions_[target_slot].topic[kMaxTopicLen - 1] = '\0'; subscriptions_[target_slot].active = true; active_subscription_count_++; }
    if (connected_.load() && client_handle_opaque_) { // Use load() and check opaque handle
       if (SubscribeInternal(subscriptions_[target_slot].topic, subscriptions_[target_slot].qos)) { subscriptions_[target_slot].pending_subscribe = false; }
       else { ESP_LOGE(TAG, "SubscribeInternal failed for %s", topic_filter.c_str()); /* Keep pending true */ }
    } else { ESP_LOGI(TAG, "Subscription to '%s' pending connection.", topic_filter.c_str()); }
    return true;
}

bool AwsIotMqttClient::SubscribeInternal(const char* topic_filter, int qos) {
    // Assumes mutex_ locked and client_handle_opaque_ valid
    if (!client_handle_opaque_) return false; // Safety check
    esp_mqtt_client_handle_t handle = GET_MQTT_HANDLE(client_handle_opaque_);
    ESP_LOGI(TAG, "Subscribing internal to topic '%s' with QoS %d", topic_filter, qos);
    int msg_id = esp_mqtt_client_subscribe(handle, topic_filter, qos);
    if (msg_id < 0) { // esp-mqtt returns negative on error for subscribe
        ESP_LOGE(TAG, "esp_mqtt_client_subscribe failed for topic '%s'", topic_filter); return false;
    }
    ESP_LOGD(TAG, "Subscribe request sent for topic '%s', msg_id=%d", topic_filter, msg_id); return true;
}

bool AwsIotMqttClient::Unsubscribe(const std::string& topic_filter) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) { ESP_LOGE(TAG, "Cannot unsubscribe: Client not initialized."); return false; }
    int found_slot = -1;
    for (int i = 0; i < kMaxSubs; ++i) { if (subscriptions_[i].active && strncmp(subscriptions_[i].topic, topic_filter.c_str(), kMaxTopicLen) == 0) { found_slot = i; break; } }
    if (found_slot == -1) { ESP_LOGW(TAG, "Cannot unsubscribe: Topic '%s' not found.", topic_filter.c_str()); return false; }
    char topic_copy[kMaxTopicLen]; strncpy(topic_copy, subscriptions_[found_slot].topic, kMaxTopicLen -1); topic_copy[kMaxTopicLen -1] = '\0';
    subscriptions_[found_slot].active = false; subscriptions_[found_slot].pending_subscribe = false; subscriptions_[found_slot].callback = nullptr; memset(subscriptions_[found_slot].topic, 0, kMaxTopicLen); active_subscription_count_--;
    ESP_LOGI(TAG, "Removed internal subscription for topic: %s", topic_copy);
    if (connected_.load() && client_handle_opaque_) { // Use load() and check opaque handle
        if (!UnsubscribeInternal(topic_copy)) { ESP_LOGE(TAG, "UnsubscribeInternal failed for %s", topic_copy); /* Still removed locally */ }
    }
    return true; // Return true even if broker unsubscribe fails, as it's removed locally
}

bool AwsIotMqttClient::UnsubscribeInternal(const char* topic_filter) {
    // Assumes mutex_ locked and client_handle_opaque_ valid
     if (!client_handle_opaque_) return false; // Safety check
    esp_mqtt_client_handle_t handle = GET_MQTT_HANDLE(client_handle_opaque_);
    ESP_LOGI(TAG, "Unsubscribing internal from topic '%s'", topic_filter);
    int msg_id = esp_mqtt_client_unsubscribe(handle, topic_filter);
     if (msg_id < 0) { // esp-mqtt returns negative on error
        ESP_LOGE(TAG, "esp_mqtt_client_unsubscribe failed for topic '%s'", topic_filter); return false;
    }
    ESP_LOGD(TAG, "Unsubscribe request sent for topic '%s', msg_id=%d", topic_filter, msg_id); return true;
}


// --- Event Handling ---

// Static handler defined earlier, signature matches esp_event_handler_t
/*static*/ void MqttEventHandlerStatic(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    AwsIotMqttClient* client = static_cast<AwsIotMqttClient*>(handler_args); // Context passed via register_event
    if (client) {
        // Pass the specific event data (already correct type from ESP-IDF)
        client->HandleMqttEvent(event_data);
    } else {
        ESP_LOGE(TAG, "MQTT event with null handler_args!");
    }
}

void AwsIotMqttClient::HandleMqttEvent(void* event_data_void) {
    if (!event_data_void) { ESP_LOGE(TAG, "HandleMqttEvent received null event_data!"); return; }
    // Cast the void* event data back to the ESP-IDF event type
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data_void);
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if the event is for the client instance we are managing
    if (client_handle_opaque_ && event->client != GET_MQTT_HANDLE(client_handle_opaque_)) {
         ESP_LOGW(TAG, "Event received for an unexpected client instance (%p vs %p). Ignoring.", event->client, GET_MQTT_HANDLE(client_handle_opaque_));
         return;
    }

    if (disconnect_requested_.load()) { ESP_LOGD(TAG, "Ignoring MQTT event %d (disconnect requested)", (int)event->event_id); return; }

    switch (event->event_id) {
        case MQTT_EVENT_BEFORE_CONNECT: break; // Usually logged by esp-mqtt itself
        case MQTT_EVENT_CONNECTED: ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED"); HandleConnect(); break;
        case MQTT_EVENT_DISCONNECTED: ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED"); HandleDisconnect(); break;
        case MQTT_EVENT_SUBSCRIBED: ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id); break;
        case MQTT_EVENT_UNSUBSCRIBED: ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id); break;
        case MQTT_EVENT_PUBLISHED: ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id); break; // Often only DEBUG level needed
        case MQTT_EVENT_DATA: ESP_LOGD(TAG, "MQTT_EVENT_DATA received"); HandleData(event->topic, event->topic_len, event->data, event->data_len); break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle) {
                ESP_LOGE(TAG, "  Error Type: %d", (int)event->error_handle->error_type);
                ESP_LOGE(TAG, "  ESP TLS Last ESP Error: 0x%x (%s)", event->error_handle->esp_tls_last_esp_err, esp_err_to_name(event->error_handle->esp_tls_last_esp_err));
                if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                     ESP_LOGE(TAG, "  Connection Refused, MQTT Return Code: 0x%x", event->error_handle->connect_return_code);
                }
                // Add more error details if needed (esp_tls_stack_err, esp_tls_cert_verify_flags)
            }
            if (connecting_.load()) {
                connecting_ = false; // Mark connection attempt as failed
                ESP_LOGW(TAG,"Connection attempt failed, disconnect event should follow to trigger reconnect.");
            }
            // If currently connected, the subsequent MQTT_EVENT_DISCONNECTED will handle scheduling reconnect.
            // If already disconnected, HandleDisconnect (called by timer or previous error) handles scheduling.
            break;
         case MQTT_EVENT_DELETED:
            ESP_LOGW(TAG, "MQTT_EVENT_DELETED (client handle destroyed)");
            // Handle should already be nullified by CleanupMqttClient
            if(client_handle_opaque_ != nullptr) {
                 ESP_LOGW(TAG, "MQTT_EVENT_DELETED but opaque handle was not null! Nullifying.");
                 client_handle_opaque_ = nullptr; // Ensure it's null
            }
            connected_ = false; connecting_ = false;
            break;
        default: ESP_LOGI(TAG, "Other MQTT event id: %d", (int)event->event_id); break;
    }
}

void AwsIotMqttClient::HandleConnect() {
    // Assumes mutex is locked
    ESP_LOGI(TAG, "HandleConnect: Successfully connected.");
    connected_ = true; connecting_ = false; disconnect_requested_ = false;
    // Reset reconnect delay upon successful connection
    current_reconnect_delay_ms_ = config_.base_reconnect_ms;
    StopReconnectTimer(); // Stop timer if it was running for reconnect
    // Resubscribe to all topics marked as pending
    ResubscribePending();
    // Call user callback
    if (on_connected_cb_) {
        // Consider running callback outside lock if it's long/blocking?
        on_connected_cb_();
    }
}

void AwsIotMqttClient::HandleDisconnect() {
     // Assumes mutex is locked
    ESP_LOGW(TAG, "HandleDisconnect: Processing disconnect.");
    bool was_connected = connected_.load();
    connected_ = false;
    connecting_ = false; // Reset connecting flag too

    // Call user callback if we were previously connected
    if (was_connected && on_disconnected_cb_) {
        on_disconnected_cb_();
    }

    // Mark all active subscriptions as pending for resubscription on next connect
    for (auto& sub : subscriptions_) {
        if (sub.active) {
            sub.pending_subscribe = true;
        }
    }

    // Trigger reconnect logic ONLY if disconnect was not requested manually
    if (!disconnect_requested_.load()) {
        ESP_LOGW(TAG, "Unexpected disconnect detected. Scheduling reconnect...");
        ScheduleReconnect();
    } else {
        ESP_LOGI(TAG, "Expected disconnect event received (manual request or cleanup). No reconnect scheduled.");
        // Ensure client handle is cleaned up if not already done. Safety net.
        if (client_handle_opaque_) {
            ESP_LOGW(TAG,"Cleaning up client handle in disconnect handler (safety net).");
            CleanupMqttClient();
        }
    }
}

void AwsIotMqttClient::ResubscribePending() {
    // Assumes mutex is locked and client is connected
    ESP_LOGI(TAG, "Resubscribing to pending topics...");
    int count = 0;
    if (!client_handle_opaque_) { // Check handle before using SubscribeInternal
        ESP_LOGE(TAG, "Cannot resubscribe, client handle is null.");
        return;
    }
    for (auto& sub : subscriptions_) {
        if (sub.active && sub.pending_subscribe) {
            if (SubscribeInternal(sub.topic, sub.qos)) {
                sub.pending_subscribe = false; // Mark as done
                count++;
            } else {
                ESP_LOGE(TAG, "Failed to resubscribe to topic: %s (will retry on next connect)", sub.topic);
                // Keep pending_subscribe = true
            }
        }
    }
    ESP_LOGI(TAG, "Resubscribe attempt complete for %d topics.", count);
}

void AwsIotMqttClient::HandleData(const char* topic, int topic_len, const char* data, int data_len) {
    // Assumes mutex is locked
    // (No changes needed in topic matching logic)
    /* ... unchanged ... */
     if (!topic || !data) return;
    std::string topic_str(topic, topic_len); std::string_view payload_view(data, data_len);
    ESP_LOGD(TAG, "Searching callback for topic: %s", topic_str.c_str()); bool handled = false;
    char shadow_prefix_buf[kMaxTopicLen]; snprintf(shadow_prefix_buf, sizeof(shadow_prefix_buf), "$aws/things/%s/shadow/", config_.thing_name.c_str()); size_t shadow_prefix_len = strlen(shadow_prefix_buf);
    if (topic_len > shadow_prefix_len && strncmp(topic, shadow_prefix_buf, shadow_prefix_len) == 0) {
        const char* suffix = topic + shadow_prefix_len; size_t suffix_len = topic_len - shadow_prefix_len;
        if ((strncmp(suffix, "update/", 7) == 0 && suffix_len > 7) || (strncmp(suffix, "delta", 5) == 0 && suffix_len == 5)) {
             if (shadow_update_cb_) { std::string type; if (suffix[0] == 'd') { type = "delta"; } else { type.assign(suffix + 7, suffix_len - 7); } ESP_LOGD(TAG, "Invoking shadow update callback for type '%s'", type.c_str()); shadow_update_cb_(type, payload_view); handled = true; }
        } else if (strncmp(suffix, "get/", 4) == 0 && suffix_len > 4) {
             if (shadow_get_cb_) { std::string type(suffix + 4, suffix_len - 4); ESP_LOGD(TAG, "Invoking shadow get callback for type '%s'", type.c_str()); shadow_get_cb_(type, payload_view); handled = true; }
        }
    }
    char jobs_notify_buf[kMaxTopicLen]; char jobs_update_prefix_buf[kMaxTopicLen]; snprintf(jobs_notify_buf, sizeof(jobs_notify_buf), "$aws/things/%s/jobs/notify-next", config_.thing_name.c_str()); snprintf(jobs_update_prefix_buf, sizeof(jobs_update_prefix_buf), "$aws/things/%s/jobs/", config_.thing_name.c_str()); size_t jobs_update_prefix_len = strlen(jobs_update_prefix_buf);
    if (!handled && topic_str == jobs_notify_buf) { if (job_notify_cb_) { ESP_LOGD(TAG, "Invoking job notification callback (notify-next)"); job_notify_cb_("unknown_job_id", "QUEUED", std::string(payload_view)); handled = true; }
    } else if (!handled && topic_len > jobs_update_prefix_len && strncmp(topic, jobs_update_prefix_buf, jobs_update_prefix_len) == 0) {
         const char* job_suffix = topic + jobs_update_prefix_len; size_t job_suffix_len = topic_len - jobs_update_prefix_len; const char* update_accepted_str = "/update/accepted"; const char* update_rejected_str = "/update/rejected"; size_t accepted_len = strlen(update_accepted_str); size_t rejected_len = strlen(update_rejected_str); std::string job_id_str; std::string status_str;
         if (job_suffix_len > accepted_len && strcmp(job_suffix + job_suffix_len - accepted_len, update_accepted_str) == 0) { job_id_str.assign(job_suffix, job_suffix_len - accepted_len); status_str = "ACCEPTED";
         } else if (job_suffix_len > rejected_len && strcmp(job_suffix + job_suffix_len - rejected_len, update_rejected_str) == 0) { job_id_str.assign(job_suffix, job_suffix_len - rejected_len); status_str = "REJECTED"; }
         if (!job_id_str.empty() && job_notify_cb_) { ESP_LOGD(TAG, "Invoking job notification callback (job update %s for %s)", status_str.c_str(), job_id_str.c_str()); job_notify_cb_(job_id_str, status_str, std::string(payload_view)); handled = true; }
    }
    if (!handled) { for (const auto& sub : subscriptions_) { if (sub.active && sub.callback && (topic_str == sub.topic)) { ESP_LOGD(TAG, "Invoking generic callback for topic: %s", sub.topic); sub.callback(topic_str, payload_view); handled = true; break; } } }
    if (!handled) { ESP_LOGD(TAG, "No suitable callback found for topic: %s", topic_str.c_str()); }
}


// --- Reconnect Logic --- (Timer callback signature and usage corrected)

// Static callback defined earlier, signature matches TimerCallbackFunction_t
/*static*/ void ReconnectTimerCallbackStatic(TimerHandle_t xTimer) { // Correct signature
     AwsIotMqttClient* client = static_cast<AwsIotMqttClient*>(pvTimerGetTimerID(xTimer)); // Get context
     if (client) {
        ESP_LOGI(TAG, "Reconnect timer fired. Attempting connection...");
        client->Connect(); // Connect handles its own locking
     } else {
        ESP_LOGE(TAG, "Reconnect timer fired with null TimerID!");
     }
}

void AwsIotMqttClient::StartReconnectTimer() {
    // Assumes mutex_ is locked
     if (!reconnect_timer_handle_) {
        // Pass 'this' as the pvTimerID (the context) when creating the timer
        reconnect_timer_handle_ = xTimerCreate(
            "MqttRecTmr",                   // Timer name
            pdMS_TO_TICKS(current_reconnect_delay_ms_), // Timer period
            pdFALSE,                        // One-shot timer
            this,                           // Pass client instance as Timer ID (context)
            ReconnectTimerCallbackStatic    // Static callback defined above
        );
         if (!reconnect_timer_handle_) { ESP_LOGE(TAG, "xTimerCreate failed for reconnect timer!"); return; }
     }
    // Ensure timer period is updated before starting
    if (xTimerChangePeriod(reconnect_timer_handle_, pdMS_TO_TICKS(current_reconnect_delay_ms_), pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGE(TAG, "xTimerChangePeriod failed for reconnect timer!");
    }
    if (xTimerIsTimerActive(reconnect_timer_handle_)) {
         ESP_LOGD(TAG, "Reconnect timer already active.");
         // Optional: Reset period again if desired? Usually not needed for one-shot.
         // xTimerReset(reconnect_timer_handle_, pdMS_TO_TICKS(100));
         return;
    }
    if (xTimerStart(reconnect_timer_handle_, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGE(TAG, "xTimerStart failed for reconnect timer!");
    } else {
         ESP_LOGI(TAG, "Reconnect timer started, will fire in %lu ms.", current_reconnect_delay_ms_);
    }
}

void AwsIotMqttClient::StopReconnectTimer() {
    // Assumes mutex_ is locked
    if (reconnect_timer_handle_) {
         if (xTimerIsTimerActive(reconnect_timer_handle_)) {
            if(xTimerStop(reconnect_timer_handle_, pdMS_TO_TICKS(100)) == pdPASS) {
                ESP_LOGI(TAG, "Reconnect timer stopped.");
            } else {
                ESP_LOGE(TAG, "xTimerStop failed for reconnect timer!");
            }
         }
    }
}

void AwsIotMqttClient::ScheduleReconnect() {
    // Assumes mutex_ is locked
     StopReconnectTimer(); // Stop any existing timer first

     // Calculate next delay with exponential backoff & limits
     current_reconnect_delay_ms_ *= 2;
     if (current_reconnect_delay_ms_ > config_.max_reconnect_ms || current_reconnect_delay_ms_ < config_.base_reconnect_ms /* handles overflow */) {
         current_reconnect_delay_ms_ = config_.max_reconnect_ms;
     }
     if (current_reconnect_delay_ms_ < config_.base_reconnect_ms) {
         current_reconnect_delay_ms_ = config_.base_reconnect_ms;
     }

     ESP_LOGI(TAG, "Scheduling reconnect attempt in %lu ms", current_reconnect_delay_ms_);
     StartReconnectTimer(); // Start the timer with the new delay
}


// --- AWS IoT Specific Helpers --- (No changes needed, use base Publish)
bool AwsIotMqttClient::GetShadowTopic(const std::string& operation, char* buffer, size_t buffer_size) { /* ... unchanged ... */ int w = snprintf(buffer, buffer_size, "$aws/things/%s/shadow/%s", config_.thing_name.c_str(), operation.c_str()); return (w > 0 && w < (int)buffer_size); }
bool AwsIotMqttClient::GetJobsTopic(const std::string& operation, const std::string& job_id, char* buffer, size_t buffer_size) { /* ... unchanged ... */ int w; if (job_id.empty() || job_id == "+") { w = snprintf(buffer, buffer_size, "$aws/things/%s/jobs/%s", config_.thing_name.c_str(), operation.c_str()); } else { w = snprintf(buffer, buffer_size, "$aws/things/%s/jobs/%s/%s", config_.thing_name.c_str(), job_id.c_str(), operation.c_str()); } return (w > 0 && w < (int)buffer_size); }
bool AwsIotMqttClient::SubscribeToShadowUpdates(ShadowUpdateCallback callback) { /* ... unchanged ... */ shadow_update_cb_ = std::move(callback); bool success = true; char topic_buf[kMaxTopicLen]; if (GetShadowTopic("update/accepted", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Shadow topic buffer small!"); } if (GetShadowTopic("update/rejected", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Shadow topic buffer small!"); } if (GetShadowTopic("update/delta", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Shadow topic buffer small!"); } return success; }
bool AwsIotMqttClient::SubscribeToShadowGetResponses(ShadowUpdateCallback callback) { /* ... unchanged ... */ shadow_get_cb_ = std::move(callback); bool success = true; char topic_buf[kMaxTopicLen]; if (GetShadowTopic("get/accepted", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Shadow topic buffer small!"); } if (GetShadowTopic("get/rejected", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Shadow topic buffer small!"); } return success; }
bool AwsIotMqttClient::UpdateShadow(const std::string& shadow_payload, int qos) { return UpdateShadow(std::string_view(shadow_payload), qos); }
bool AwsIotMqttClient::UpdateShadow(std::string_view shadow_payload, int qos) { /* ... unchanged ... */ char topic_buf[kMaxTopicLen]; if (!GetShadowTopic("update", topic_buf, sizeof(topic_buf))) { ESP_LOGE(TAG, "Shadow topic buffer small!"); return false; } return Publish(topic_buf, reinterpret_cast<const uint8_t*>(shadow_payload.data()), shadow_payload.length(), qos); }
bool AwsIotMqttClient::GetShadow(const std::string& client_token) { /* ... unchanged ... */ char topic_buf[kMaxTopicLen]; if (!GetShadowTopic("get", topic_buf, sizeof(topic_buf))) { ESP_LOGE(TAG, "Shadow topic buffer small!"); return false; } char payload_buf[128]; int len; if (client_token.empty()) { len = snprintf(payload_buf, sizeof(payload_buf), "{}"); } else { len = snprintf(payload_buf, sizeof(payload_buf), "{\"clientToken\":\"%.*s\"}", (int)client_token.length(), client_token.c_str()); } if (len <= 0 || len >= (int)sizeof(payload_buf)) { ESP_LOGE(TAG, "Client token payload buffer small!"); len = snprintf(payload_buf, sizeof(payload_buf), "{}"); } return Publish(topic_buf, reinterpret_cast<const uint8_t*>(payload_buf), len, 0); }
bool AwsIotMqttClient::SubscribeToJobs(JobNotificationCallback callback) { /* ... unchanged ... */ job_notify_cb_ = std::move(callback); bool success = true; char topic_buf[kMaxTopicLen]; if (GetJobsTopic("notify-next", "", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Jobs topic buffer small!"); } if (GetJobsTopic("update/accepted", "+", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Jobs topic buffer small!"); } if (GetJobsTopic("update/rejected", "+", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Jobs topic buffer small!"); } return success; }
bool AwsIotMqttClient::UpdateJobStatus(const std::string& job_id, const std::string& status, const std::string& status_details_json) { return UpdateJobStatus(job_id, status, std::string_view(status_details_json)); }
bool AwsIotMqttClient::UpdateJobStatus(const std::string& job_id, const std::string& status, std::string_view status_details_json) { /* ... unchanged ... */ if (job_id.empty() || status.empty()) { ESP_LOGE(TAG, "Job ID and Status cannot be empty"); return false; } char topic_buf[kMaxTopicLen]; if (!GetJobsTopic("update", job_id, topic_buf, sizeof(topic_buf))) { ESP_LOGE(TAG, "Jobs topic buffer small!"); return false; } char payload_buf[kMaxPayloadLen]; int written = snprintf(payload_buf, sizeof(payload_buf), "{\"status\":\"%.*s\",\"statusDetails\":%.*s}", (int)status.length(), status.c_str(), (int)status_details_json.length(), status_details_json.data()); if (written <= 0 || written >= (int)sizeof(payload_buf)) { ESP_LOGE(TAG, "UpdateJobStatus payload buffer small!"); return false; } return Publish(topic_buf, reinterpret_cast<const uint8_t*>(payload_buf), written, 1); }

// --- Setters for Callbacks ---
void AwsIotMqttClient::SetOnConnectedCallback(StatusCallback cb) { std::lock_guard<std::mutex> lock(mutex_); on_connected_cb_ = std::move(cb); }
void AwsIotMqttClient::SetOnDisconnectedCallback(StatusCallback cb) { std::lock_guard<std::mutex> lock(mutex_); on_disconnected_cb_ = std::move(cb); }

} // namespace AwsIot

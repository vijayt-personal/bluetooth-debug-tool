#include "aws_iot_mqtt_client.hpp" // Our header first

#include <cstring> // For strncpy, strcmp, strlen, memset
#include <cstdio>  // For snprintf
#include <algorithm> // For std::min

// --- ESP-IDF specific includes --- REQUIRED HERE ONLY ---
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"   // For vTaskDelay (if used internally, though not anymore for reconnect)
#include "freertos/semphr.h" // For mutex
// #include "freertos/timers.h" // REMOVED - No longer using FreeRTOS timers
#include "esp_log.h"
#include "esp_event.h"      // Needed for event handler signature and base types
#include "mqtt_client.h"    // Defines esp_mqtt_client_handle_t, config, events etc.
#include "esp_tls.h"        // For error checking if needed
// --- End ESP-IDF Includes ---

static const char* TAG = "AwsIotMqttClient";

namespace AwsIot {

// Helper macro for casting the opaque handle
#define GET_MQTT_HANDLE(opaque_handle) (static_cast<esp_mqtt_client_handle_t>(opaque_handle))

// --- Static callback function definition ---
// Signature must match esp_event_handler_t (void (void*, esp_event_base_t, int32_t, void*))
static void MqttEventHandlerStatic(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
// --- Timer callback REMOVED ---

// --- Constructor / Destructor ---
AwsIotMqttClient::AwsIotMqttClient() {
    memset(&config_, 0, sizeof(config_));
    for (auto& sub : subscriptions_) {
        sub.active = false;
        sub.pending_subscribe = false;
    }
}

AwsIotMqttClient::~AwsIotMqttClient() {
    ESP_LOGI(TAG, "Destructor called.");
    Disconnect();
}


// --- Initialization and Connection ---

bool AwsIotMqttClient::Initialize(const MqttConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized."); return true;
    }
    if (config.aws_endpoint.empty() || config.client_id.empty() || config.thing_name.empty() ||
        config.root_ca_pem[0] == '\0' || config.device_cert_pem[0] == '\0' || config.private_key_pem[0] == '\0') {
        ESP_LOGE(TAG, "Init failed: Missing config/certs."); return false;
    }
    config_ = config; // Copy config struct
    initialized_ = true;
    disconnect_requested_ = false;
    ESP_LOGI(TAG, "Client initialized for %s", config_.client_id.c_str());
    ESP_LOGI(TAG, "Config size: %d bytes", (int)sizeof(config_));
    return true;
}

bool AwsIotMqttClient::InitializeMqttClient() {
    // Assumes mutex_ is already locked
    if (client_handle_opaque_) { CleanupMqttClient(); }

    esp_mqtt_client_config_t mqtt_cfg = {};
    if (config_.root_ca_pem[0] == '\0' || config_.device_cert_pem[0] == '\0' || config_.private_key_pem[0] == '\0') {
         ESP_LOGE(TAG, "Internal certificate data is empty during MQTT client init!"); return false;
    }
    std::string uri = "mqtts://" + config_.aws_endpoint + ":" + std::to_string(config_.port);
    mqtt_cfg.broker.address.uri = uri.c_str();
    mqtt_cfg.broker.verification.certificate = config_.root_ca_pem;
    mqtt_cfg.credentials.authentication.certificate = config_.device_cert_pem;
    mqtt_cfg.credentials.authentication.key = config_.private_key_pem;
    mqtt_cfg.credentials.client_id = config_.client_id.c_str();
    mqtt_cfg.buffer.size = config_.rx_buffer_size;
    mqtt_cfg.buffer.out_size = config_.tx_buffer_size;
    mqtt_cfg.session.keepalive = 60;
    mqtt_cfg.network.disable_auto_reconnect = true; // Ensure esp-mqtt doesn't auto-reconnect

    ESP_LOGI(TAG, "Initializing ESP MQTT client (esp-mqtt)...");
    client_handle_opaque_ = esp_mqtt_client_init(&mqtt_cfg);
    if (!client_handle_opaque_) { ESP_LOGE(TAG, "esp_mqtt_client_init failed"); return false; }

    ESP_LOGI(TAG, "Registering ESP MQTT event handler...");
    esp_mqtt_client_handle_t handle = GET_MQTT_HANDLE(client_handle_opaque_);
    // Pass 'this' as context via last arg; pass static handler directly
    esp_err_t err = esp_mqtt_client_register_event(handle, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, MqttEventHandlerStatic, this);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_register_event failed: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(handle);
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
        if (stop_err != ESP_OK && stop_err != ESP_ERR_INVALID_STATE) { ESP_LOGW(TAG, "esp_mqtt_client_stop failed: %s", esp_err_to_name(stop_err)); }
        esp_err_t destroy_err = esp_mqtt_client_destroy(handle);
        if (destroy_err != ESP_OK) { ESP_LOGW(TAG, "esp_mqtt_client_destroy failed: %s", esp_err_to_name(destroy_err)); }
        client_handle_opaque_ = nullptr;
        ESP_LOGI(TAG, "MQTT client cleaned up.");
    }
    connected_ = false;
    connecting_ = false;
}

bool AwsIotMqttClient::Connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        ESP_LOGE(TAG, "Cannot connect: Client not initialized."); return false;
    }
    if (connected_.load() || connecting_.load()) {
        ESP_LOGW(TAG, "Connect() called while already %s.", connected_.load() ? "connected" : "connecting");
        return false;
    }
     if (disconnect_requested_.load()) {
        ESP_LOGW(TAG, "Connect() called after disconnect requested."); return false;
    }
    ESP_LOGI(TAG, "Connect requested by application.");
    connecting_ = true; // Set flag: attempt is in progress
    disconnect_requested_ = false;
    if (!InitializeMqttClient()) { // Re-init internal client state if needed
        connecting_ = false;
        return false;
    }
    if (!client_handle_opaque_) {
         ESP_LOGE(TAG, "Connect failed: Opaque handle is null after init.");
         connecting_ = false;
         return false;
    }
    esp_mqtt_client_handle_t handle = GET_MQTT_HANDLE(client_handle_opaque_);
    ESP_LOGI(TAG, "Starting ESP MQTT client task...");
    esp_err_t start_err = esp_mqtt_client_start(handle);
    if (start_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start failed: %s", esp_err_to_name(start_err));
        CleanupMqttClient();
        connecting_ = false;
        return false;
    }
    // --- Reconnect timer/delay logic REMOVED ---
    ESP_LOGI(TAG, "MQTT client start initiated. Waiting for connection event...");
    return true; // Indicates the connection *attempt* has started
}

void AwsIotMqttClient::Disconnect() {
     ESP_LOGI(TAG, "Disconnect requested by user.");
    {
        std::lock_guard<std::mutex> lock(mutex_);
        disconnect_requested_ = true; // Signal intention to stop
        // --- Stop timer logic REMOVED ---
        CleanupMqttClient(); // Stop and destroy the client
    }
    ESP_LOGI(TAG, "Client disconnect action complete.");
}

bool AwsIotMqttClient::IsConnected() const {
    return connected_.load();
}

// --- Publish / Subscribe / Unsubscribe ---
bool AwsIotMqttClient::Publish(const std::string& topic, const std::string& payload, int qos, bool retain) { return Publish(topic, std::string_view(payload), qos, retain); }
bool AwsIotMqttClient::Publish(const std::string& topic, std::string_view payload, int qos, bool retain) { return Publish(topic, reinterpret_cast<const uint8_t*>(payload.data()), payload.length(), qos, retain); }
bool AwsIotMqttClient::Publish(const std::string& topic, const uint8_t* payload, size_t len, int qos, bool retain) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_.load() || !client_handle_opaque_) { ESP_LOGW(TAG, "Cannot publish: Not connected or handle null."); return false; }
    if (topic.length() >= kMaxTopicLen) { ESP_LOGE(TAG, "Cannot publish: Topic too long."); return false; }
    if (payload == nullptr && len > 0) { ESP_LOGE(TAG, "Cannot publish: Null payload non-zero len."); return false; }
    esp_mqtt_client_handle_t handle = GET_MQTT_HANDLE(client_handle_opaque_);
    int msg_id = esp_mqtt_client_publish(handle, topic.c_str(), reinterpret_cast<const char*>(payload), static_cast<int>(len), qos, retain);
    if (msg_id == -1) { ESP_LOGE(TAG, "esp_mqtt_client_publish failed for topic '%s'.", topic.c_str()); return false; }
    ESP_LOGD(TAG, "Publish queued to topic '%s', msg_id=%d, len=%d", topic.c_str(), msg_id, (int)len); return true;
}

bool AwsIotMqttClient::Subscribe(const std::string& topic_filter, int qos, MqttMessageCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) { ESP_LOGE(TAG, "Cannot subscribe: Not initialized."); return false; }
    if (topic_filter.length() >= kMaxTopicLen) { ESP_LOGE(TAG, "Cannot subscribe: Topic too long."); return false; }
    int available_slot = -1; int existing_slot = -1;
    for (int i = 0; i < kMaxSubs; ++i) { if (subscriptions_[i].active) { if (strncmp(subscriptions_[i].topic, topic_filter.c_str(), kMaxTopicLen) == 0) { existing_slot = i; break; } } else if (available_slot == -1) { available_slot = i; } }
    int target_slot = -1; bool new_subscription = false;
    if (existing_slot != -1) { ESP_LOGI(TAG, "Updating subscription: %s", topic_filter.c_str()); target_slot = existing_slot; }
    else if (available_slot != -1) { ESP_LOGI(TAG, "Adding subscription: %s", topic_filter.c_str()); target_slot = available_slot; new_subscription = true; }
    else { ESP_LOGE(TAG, "Cannot subscribe: Max subs (%d) reached.", kMaxSubs); return false; }
    subscriptions_[target_slot].qos = qos; subscriptions_[target_slot].callback = std::move(callback); subscriptions_[target_slot].pending_subscribe = true;
    if (new_subscription) { strncpy(subscriptions_[target_slot].topic, topic_filter.c_str(), kMaxTopicLen - 1); subscriptions_[target_slot].topic[kMaxTopicLen - 1] = '\0'; subscriptions_[target_slot].active = true; active_subscription_count_++; }
    if (connected_.load() && client_handle_opaque_) {
       if (SubscribeInternal(subscriptions_[target_slot].topic, subscriptions_[target_slot].qos)) { subscriptions_[target_slot].pending_subscribe = false; }
       else { ESP_LOGE(TAG, "SubscribeInternal failed for %s", topic_filter.c_str()); /* Keep pending true */ }
    } else { ESP_LOGI(TAG, "Subscription to '%s' pending connection.", topic_filter.c_str()); }
    return true;
}

bool AwsIotMqttClient::SubscribeInternal(const char* topic_filter, int qos) {
    if (!client_handle_opaque_) return false;
    esp_mqtt_client_handle_t handle = GET_MQTT_HANDLE(client_handle_opaque_);
    ESP_LOGI(TAG, "Subscribing internal to topic '%s' QoS %d", topic_filter, qos);
    int msg_id = esp_mqtt_client_subscribe(handle, topic_filter, qos);
    if (msg_id < 0) { ESP_LOGE(TAG, "esp_mqtt_client_subscribe failed for '%s'", topic_filter); return false; }
    ESP_LOGD(TAG, "Subscribe request sent for '%s', msg_id=%d", topic_filter, msg_id); return true;
}

bool AwsIotMqttClient::Unsubscribe(const std::string& topic_filter) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) { ESP_LOGE(TAG, "Cannot unsubscribe: Not initialized."); return false; }
    int found_slot = -1;
    for (int i = 0; i < kMaxSubs; ++i) { if (subscriptions_[i].active && strncmp(subscriptions_[i].topic, topic_filter.c_str(), kMaxTopicLen) == 0) { found_slot = i; break; } }
    if (found_slot == -1) { ESP_LOGW(TAG, "Cannot unsubscribe: Topic '%s' not found.", topic_filter.c_str()); return false; }
    char topic_copy[kMaxTopicLen]; strncpy(topic_copy, subscriptions_[found_slot].topic, kMaxTopicLen -1); topic_copy[kMaxTopicLen -1] = '\0';
    subscriptions_[found_slot].active = false; subscriptions_[found_slot].pending_subscribe = false; subscriptions_[found_slot].callback = nullptr; memset(subscriptions_[found_slot].topic, 0, kMaxTopicLen); active_subscription_count_--;
    ESP_LOGI(TAG, "Removed internal subscription for: %s", topic_copy);
    if (connected_.load() && client_handle_opaque_) {
        if (!UnsubscribeInternal(topic_copy)) { ESP_LOGE(TAG, "UnsubscribeInternal failed for %s", topic_copy); /* Still removed locally */ }
    }
    return true; // Return true even if broker unsubscribe fails, as it's removed locally
}

bool AwsIotMqttClient::UnsubscribeInternal(const char* topic_filter) {
    if (!client_handle_opaque_) return false;
    esp_mqtt_client_handle_t handle = GET_MQTT_HANDLE(client_handle_opaque_);
    ESP_LOGI(TAG, "Unsubscribing internal from topic '%s'", topic_filter);
    int msg_id = esp_mqtt_client_unsubscribe(handle, topic_filter);
     if (msg_id < 0) { ESP_LOGE(TAG, "esp_mqtt_client_unsubscribe failed for '%s'", topic_filter); return false; }
    ESP_LOGD(TAG, "Unsubscribe request sent for '%s', msg_id=%d", topic_filter, msg_id); return true;
}


// --- Event Handling ---

// Static handler defined earlier
/*static*/ void MqttEventHandlerStatic(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    AwsIotMqttClient* client = static_cast<AwsIotMqttClient*>(handler_args);
    if (client) {
        client->HandleMqttEvent(event_data); // Call the public method
    } else { ESP_LOGE(TAG, "MQTT event with null handler_args!"); }
}

// Now PUBLIC method (moved in header)
void AwsIotMqttClient::HandleMqttEvent(void* event_data_void) {
    if (!event_data_void) { ESP_LOGE(TAG, "HandleMqttEvent received null event_data!"); return; }
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data_void);
    std::lock_guard<std::mutex> lock(mutex_);

    if (client_handle_opaque_ && event->client != GET_MQTT_HANDLE(client_handle_opaque_)) {
         if (client_handle_opaque_ != nullptr) { ESP_LOGW(TAG, "Event for unexpected client (%p vs %p). Ignoring.", event->client, GET_MQTT_HANDLE(client_handle_opaque_)); }
         return;
    }
    if (disconnect_requested_.load() && event->event_id != MQTT_EVENT_DELETED) {
        ESP_LOGD(TAG, "Ignoring MQTT event %d (disconnect requested)", (int)event->event_id); return;
    }

    switch (event->event_id) {
        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT"); // Informative log
            break;
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            HandleConnect();
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
            HandleDisconnect(); // Now just sets state + calls callback
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGD(TAG, "MQTT_EVENT_DATA received");
            HandleData(event->topic, event->topic_len, event->data, event->data_len);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle) {
                ESP_LOGE(TAG, "  Error Type: %d", (int)event->error_handle->error_type);
                ESP_LOGE(TAG, "  ESP TLS Last ESP Error: 0x%x (%s)", event->error_handle->esp_tls_last_esp_err, esp_err_to_name(event->error_handle->esp_tls_last_esp_err));
                if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                     ESP_LOGE(TAG, "  Connection Refused, MQTT Return Code: 0x%x", event->error_handle->connect_return_code);
                }
            }
            if (connecting_.load()) {
                connecting_ = false;
                ESP_LOGW(TAG,"Connection attempt failed during connect phase.");
                // The disconnect event should follow shortly, triggering user callback.
            } else if (connected_.load()) {
                 ESP_LOGW(TAG,"Error occurred while connected. Disconnect likely imminent.");
                 // Let the disconnect event trigger the user callback.
            } else {
                 ESP_LOGW(TAG,"Error occurred while already disconnected.");
                 // Ensure state is consistent if error happens during disconnect cleanup?
                 connecting_ = false; // Ensure connecting flag is clear
            }
            break;
         case MQTT_EVENT_DELETED:
            ESP_LOGW(TAG, "MQTT_EVENT_DELETED (client handle destroyed)");
            if(client_handle_opaque_ != nullptr) { ESP_LOGW(TAG, "MQTT_EVENT_DELETED but opaque handle was not null! Nullifying."); client_handle_opaque_ = nullptr; }
            connected_ = false; connecting_ = false;
            break;
        default:
            ESP_LOGI(TAG, "Other MQTT event id: %d", (int)event->event_id);
            break;
    }
}

void AwsIotMqttClient::HandleConnect() {
    // Assumes mutex is locked
    ESP_LOGI(TAG, "HandleConnect: Successfully connected.");
    connected_ = true;
    connecting_ = false; // Clear connecting flag
    disconnect_requested_ = false;
    // --- Timer reset REMOVED ---
    ResubscribePending();
    if (on_connected_cb_) {
        on_connected_cb_(); // Call user callback
    }
}

void AwsIotMqttClient::HandleDisconnect() {
     // Assumes mutex is locked
    ESP_LOGW(TAG, "HandleDisconnect: Processing disconnect. State updated, calling user callback.");
    bool was_connected = connected_.load();
    connected_ = false;
    connecting_ = false; // Clear connecting flag
    for (auto& sub : subscriptions_) {
        if (sub.active) { sub.pending_subscribe = true; }
    }
    // --- Reconnect scheduling REMOVED ---

    // Call user callback AFTER updating internal state
    // Application is responsible for detecting !IsConnected() and retrying Connect()
    if (was_connected && on_disconnected_cb_) {
        on_disconnected_cb_();
    }
}

void AwsIotMqttClient::ResubscribePending() {
    // Assumes mutex is locked and client is connected
    ESP_LOGI(TAG, "Resubscribing to pending topics...");
    int count = 0;
    if (!client_handle_opaque_) {
        ESP_LOGE(TAG, "Cannot resubscribe, client handle is null."); return;
    }
    for (auto& sub : subscriptions_) {
        if (sub.active && sub.pending_subscribe) {
            if (SubscribeInternal(sub.topic, sub.qos)) {
                sub.pending_subscribe = false; count++;
            } else {
                ESP_LOGE(TAG, "Failed to resubscribe to topic: %s (will retry on next connect)", sub.topic);
            }
        }
    }
    ESP_LOGI(TAG, "Resubscribe attempt complete for %d topics.", count);
}

void AwsIotMqttClient::HandleData(const char* topic, int topic_len, const char* data, int data_len) {
    // Assumes mutex is locked
    if (!topic || !data) return;
    std::string topic_str(topic, topic_len); std::string_view payload_view(data, data_len);
    ESP_LOGD(TAG, "Searching callback for topic: %s", topic_str.c_str()); bool handled = false;

    // AWS Shadow Topic Matching
    char shadow_prefix_buf[kMaxTopicLen];
    snprintf(shadow_prefix_buf, sizeof(shadow_prefix_buf), "$aws/things/%s/shadow/", config_.thing_name.c_str());
    size_t shadow_prefix_len = strlen(shadow_prefix_buf);
    if (topic_len > shadow_prefix_len && strncmp(topic, shadow_prefix_buf, shadow_prefix_len) == 0) {
        const char* suffix = topic + shadow_prefix_len; size_t suffix_len = topic_len - shadow_prefix_len;
        if ((strncmp(suffix, "update/", 7) == 0 && suffix_len > 7) || (strncmp(suffix, "delta", 5) == 0 && suffix_len == 5)) {
             if (shadow_update_cb_) { std::string type; if (suffix[0] == 'd') { type = "delta"; } else { type.assign(suffix + 7, suffix_len - 7); } ESP_LOGD(TAG, "Invoking shadow update callback for type '%s'", type.c_str()); shadow_update_cb_(type, payload_view); handled = true; }
        } else if (strncmp(suffix, "get/", 4) == 0 && suffix_len > 4) {
             if (shadow_get_cb_) { std::string type(suffix + 4, suffix_len - 4); ESP_LOGD(TAG, "Invoking shadow get callback for type '%s'", type.c_str()); shadow_get_cb_(type, payload_view); handled = true; }
        }
    }

    // AWS Jobs Topic Matching
    char jobs_notify_buf[kMaxTopicLen]; char jobs_update_prefix_buf[kMaxTopicLen];
    snprintf(jobs_notify_buf, sizeof(jobs_notify_buf), "$aws/things/%s/jobs/notify-next", config_.thing_name.c_str());
    snprintf(jobs_update_prefix_buf, sizeof(jobs_update_prefix_buf), "$aws/things/%s/jobs/", config_.thing_name.c_str());
    size_t jobs_update_prefix_len = strlen(jobs_update_prefix_buf);
    if (!handled && topic_str == jobs_notify_buf) { if (job_notify_cb_) { ESP_LOGD(TAG, "Invoking job notification callback (notify-next)"); job_notify_cb_("unknown_job_id", "QUEUED", std::string(payload_view)); handled = true; }
    } else if (!handled && topic_len > jobs_update_prefix_len && strncmp(topic, jobs_update_prefix_buf, jobs_update_prefix_len) == 0) {
         const char* job_suffix = topic + jobs_update_prefix_len; size_t job_suffix_len = topic_len - jobs_update_prefix_len; const char* update_accepted_str = "/update/accepted"; const char* update_rejected_str = "/update/rejected"; size_t accepted_len = strlen(update_accepted_str); size_t rejected_len = strlen(update_rejected_str); std::string job_id_str; std::string status_str;
         if (job_suffix_len > accepted_len && strcmp(job_suffix + job_suffix_len - accepted_len, update_accepted_str) == 0) { job_id_str.assign(job_suffix, job_suffix_len - accepted_len); status_str = "ACCEPTED";
         } else if (job_suffix_len > rejected_len && strcmp(job_suffix + job_suffix_len - rejected_len, update_rejected_str) == 0) { job_id_str.assign(job_suffix, job_suffix_len - rejected_len); status_str = "REJECTED"; }
         if (!job_id_str.empty() && job_notify_cb_) { ESP_LOGD(TAG, "Invoking job notification callback (job update %s for %s)", status_str.c_str(), job_id_str.c_str()); job_notify_cb_(job_id_str, status_str, std::string(payload_view)); handled = true; }
    }

    // Generic Subscription Matching (Exact match only)
    if (!handled) {
        for (const auto& sub : subscriptions_) {
            if (sub.active && sub.callback && (topic_str == sub.topic)) {
                 ESP_LOGD(TAG, "Invoking generic callback for topic: %s", sub.topic);
                 sub.callback(topic_str, payload_view);
                 handled = true;
                 break;
            }
        }
    }
    if (!handled) { ESP_LOGD(TAG, "No suitable callback found for topic: %s", topic_str.c_str()); }
}


// --- Reconnect Logic Methods REMOVED ---

// --- AWS IoT Specific Helpers ---
bool AwsIotMqttClient::GetShadowTopic(const std::string& operation, char* buffer, size_t buffer_size) {
    int written = snprintf(buffer, buffer_size, "$aws/things/%s/shadow/%s", config_.thing_name.c_str(), operation.c_str());
    return (written > 0 && written < (int)buffer_size);
}
bool AwsIotMqttClient::GetJobsTopic(const std::string& operation, const std::string& job_id, char* buffer, size_t buffer_size) {
    int written;
    if (job_id.empty() || job_id == "+") { written = snprintf(buffer, buffer_size, "$aws/things/%s/jobs/%s", config_.thing_name.c_str(), operation.c_str()); }
    else { written = snprintf(buffer, buffer_size, "$aws/things/%s/jobs/%s/%s", config_.thing_name.c_str(), job_id.c_str(), operation.c_str()); }
    return (written > 0 && written < (int)buffer_size);
}
bool AwsIotMqttClient::SubscribeToShadowUpdates(ShadowUpdateCallback callback) {
    shadow_update_cb_ = std::move(callback); bool success = true; char topic_buf[kMaxTopicLen];
    if (GetShadowTopic("update/accepted", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Shadow topic buffer small!"); }
    if (GetShadowTopic("update/rejected", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Shadow topic buffer small!"); }
    if (GetShadowTopic("update/delta", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Shadow topic buffer small!"); }
    return success;
}
bool AwsIotMqttClient::SubscribeToShadowGetResponses(ShadowUpdateCallback callback) {
    shadow_get_cb_ = std::move(callback); bool success = true; char topic_buf[kMaxTopicLen];
    if (GetShadowTopic("get/accepted", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Shadow topic buffer small!"); }
    if (GetShadowTopic("get/rejected", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Shadow topic buffer small!"); }
    return success;
}
bool AwsIotMqttClient::UpdateShadow(const std::string& shadow_payload, int qos) { return UpdateShadow(std::string_view(shadow_payload), qos); }
bool AwsIotMqttClient::UpdateShadow(std::string_view shadow_payload, int qos) {
    char topic_buf[kMaxTopicLen];
    if (!GetShadowTopic("update", topic_buf, sizeof(topic_buf))) { ESP_LOGE(TAG, "Shadow topic buffer small!"); return false; }
    return Publish(topic_buf, reinterpret_cast<const uint8_t*>(shadow_payload.data()), shadow_payload.length(), qos);
}
bool AwsIotMqttClient::GetShadow(const std::string& client_token) {
    char topic_buf[kMaxTopicLen];
    if (!GetShadowTopic("get", topic_buf, sizeof(topic_buf))) { ESP_LOGE(TAG, "Shadow topic buffer small!"); return false; }
    char payload_buf[128]; int len;
    if (client_token.empty()) { len = snprintf(payload_buf, sizeof(payload_buf), "{}"); }
    else { len = snprintf(payload_buf, sizeof(payload_buf), "{\"clientToken\":\"%.*s\"}", (int)client_token.length(), client_token.c_str()); }
    if (len <= 0 || len >= (int)sizeof(payload_buf)) { ESP_LOGE(TAG, "Client token payload buffer small!"); len = snprintf(payload_buf, sizeof(payload_buf), "{}"); }
    return Publish(topic_buf, reinterpret_cast<const uint8_t*>(payload_buf), len, 0);
}
bool AwsIotMqttClient::SubscribeToJobs(JobNotificationCallback callback) {
    job_notify_cb_ = std::move(callback); bool success = true; char topic_buf[kMaxTopicLen];
    if (GetJobsTopic("notify-next", "", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Jobs topic buffer small!"); }
    if (GetJobsTopic("update/accepted", "+", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Jobs topic buffer small!"); }
    if (GetJobsTopic("update/rejected", "+", topic_buf, sizeof(topic_buf))) { success &= Subscribe(topic_buf, 1, nullptr); } else { success = false; ESP_LOGE(TAG, "Jobs topic buffer small!"); }
    return success;
}
bool AwsIotMqttClient::UpdateJobStatus(const std::string& job_id, const std::string& status, const std::string& status_details_json) { return UpdateJobStatus(job_id, status, std::string_view(status_details_json)); }
bool AwsIotMqttClient::UpdateJobStatus(const std::string& job_id, const std::string& status, std::string_view status_details_json) {
    if (job_id.empty() || status.empty()) { ESP_LOGE(TAG, "Job ID and Status cannot be empty"); return false; }
    char topic_buf[kMaxTopicLen];
    if (!GetJobsTopic("update", job_id, topic_buf, sizeof(topic_buf))) { ESP_LOGE(TAG, "Jobs topic buffer small!"); return false; }
    char payload_buf[kMaxPayloadLen];
    int written = snprintf(payload_buf, sizeof(payload_buf), "{\"status\":\"%.*s\",\"statusDetails\":%.*s}", (int)status.length(), status.c_str(), (int)status_details_json.length(), status_details_json.data());
    if (written <= 0 || written >= (int)sizeof(payload_buf)) { ESP_LOGE(TAG, "UpdateJobStatus payload buffer small!"); return false; }
    return Publish(topic_buf, reinterpret_cast<const uint8_t*>(payload_buf), written, 1);
}

// --- Setters for Callbacks ---
void AwsIotMqttClient::SetOnConnectedCallback(StatusCallback cb) { std::lock_guard<std::mutex> lock(mutex_); on_connected_cb_ = std::move(cb); }
void AwsIotMqttClient::SetOnDisconnectedCallback(StatusCallback cb) { std::lock_guard<std::mutex> lock(mutex_); on_disconnected_cb_ = std::move(cb); }

} // namespace AwsIot

#include "aws_iot_mqtt_client.hpp" // Our header first

#include <cstring>
#include <cstdio>
#include <algorithm> // For std::min

// --- ESP-IDF specific includes --- REQUIRED HERE ONLY ---
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"   // For vTaskDelay (if used internally, though not anymore for reconnect)
#include "freertos/semphr.h" // For mutex
// #include "freertos/timers.h" // REMOVED - No longer using FreeRTOS timers
#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "esp_tls.h"
// --- End ESP-IDF Includes ---

static const char* TAG = "AwsIotMqttClient";

namespace AwsIot {

// Helper macro for casting the opaque handle
#define GET_MQTT_HANDLE(opaque_handle) (static_cast<esp_mqtt_client_handle_t>(opaque_handle))

// --- Static callback function definitions ---
// Signature must match esp_event_handler_t (void (void*, esp_event_base_t, int32_t, void*))
static void MqttEventHandlerStatic(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
// --- Timer callback REMOVED ---
// static void ReconnectTimerCallbackStatic(TimerHandle_t xTimer);

// --- Constructor / Destructor ---
AwsIotMqttClient::AwsIotMqttClient() /* : reconnect_timer_handle_{nullptr} <-- REMOVED */
{
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
    // Pass 'this' as context via last arg
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
    // Prevent rapid connect calls if already connecting or connected
    if (connected_.load() || connecting_.load()) {
        ESP_LOGW(TAG, "Connect() called while already %s.", connected_.load() ? "connected" : "connecting");
        return false; // Or return true if connecting is ok? Let's prevent stacking calls.
    }
     if (disconnect_requested_.load()) {
        ESP_LOGW(TAG, "Connect() called after disconnect requested. Call Initialize() again if needed."); return false;
    }
    ESP_LOGI(TAG, "Connect requested.");
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
    // Connection result (success/failure) will come via HandleMqttEvent
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

// --- Publish / Subscribe / Unsubscribe --- (No changes needed from previous version)
bool AwsIotMqttClient::Publish(const std::string& topic, const std::string& payload, int qos, bool retain) { /* ... */ }
bool AwsIotMqttClient::Publish(const std::string& topic, std::string_view payload, int qos, bool retain) { /* ... */ }
bool AwsIotMqttClient::Publish(const std::string& topic, const uint8_t* payload, size_t len, int qos, bool retain) { /* ... */ }
bool AwsIotMqttClient::Subscribe(const std::string& topic_filter, int qos, MqttMessageCallback callback) { /* ... */ }
bool AwsIotMqttClient::SubscribeInternal(const char* topic_filter, int qos) { /* ... */ }
bool AwsIotMqttClient::Unsubscribe(const std::string& topic_filter) { /* ... */ }
bool AwsIotMqttClient::UnsubscribeInternal(const char* topic_filter) { /* ... */ }


// --- Event Handling ---

// Static handler defined earlier
/*static*/ void MqttEventHandlerStatic(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    AwsIotMqttClient* client = static_cast<AwsIotMqttClient*>(handler_args);
    if (client) {
        // Call the now PUBLIC HandleMqttEvent method
        client->HandleMqttEvent(event_data);
    } else {
        ESP_LOGE(TAG, "MQTT event with null handler_args!");
    }
}

// Now PUBLIC method (moved in header)
void AwsIotMqttClient::HandleMqttEvent(void* event_data_void) {
    if (!event_data_void) { ESP_LOGE(TAG, "HandleMqttEvent received null event_data!"); return; }
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data_void);
    std::lock_guard<std::mutex> lock(mutex_);

    if (client_handle_opaque_ && event->client != GET_MQTT_HANDLE(client_handle_opaque_)) {
         if (client_handle_opaque_ != nullptr) { ESP_LOGW(TAG, "Event received for unexpected client (%p vs %p). Ignoring.", event->client, GET_MQTT_HANDLE(client_handle_opaque_)); }
         return;
    }

    // Don't process events if a disconnect was manually requested
    // (except MQTT_EVENT_DELETED which can happen during cleanup)
    if (disconnect_requested_.load() && event->event_id != MQTT_EVENT_DELETED) {
        ESP_LOGD(TAG, "Ignoring MQTT event %d (disconnect requested)", (int)event->event_id);
        return;
    }

    switch (event->event_id) {
        case MQTT_EVENT_BEFORE_CONNECT: break;
        case MQTT_EVENT_CONNECTED: ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED"); HandleConnect(); break;
        case MQTT_EVENT_DISCONNECTED: ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED"); HandleDisconnect(); break;
        // ... other cases (SUB, UNSUB, PUB, DATA, ERROR) ...
        case MQTT_EVENT_SUBSCRIBED: /* ... */ break;
        case MQTT_EVENT_UNSUBSCRIBED: /* ... */ break;
        case MQTT_EVENT_PUBLISHED: /* ... */ break;
        case MQTT_EVENT_DATA: /* ... */ HandleData(event->topic, event->topic_len, event->data, event->data_len); break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            // ... log error details ...
            if (connecting_.load()) {
                connecting_ = false; // Mark connection attempt as failed
                ESP_LOGW(TAG,"Connection attempt failed, disconnect event should follow.");
                // NOTE: Disconnect event might trigger user callback, app should handle state
            }
            break;
         case MQTT_EVENT_DELETED:
            ESP_LOGW(TAG, "MQTT_EVENT_DELETED (client handle destroyed)");
            if(client_handle_opaque_ != nullptr) { ESP_LOGW(TAG, "MQTT_EVENT_DELETED but opaque handle was not null!"); client_handle_opaque_ = nullptr; }
            connected_ = false; connecting_ = false;
            break;
        default: ESP_LOGI(TAG, "Other MQTT event id: %d", (int)event->event_id); break;
    }
}

void AwsIotMqttClient::HandleConnect() {
    // Assumes mutex is locked
    ESP_LOGI(TAG, "HandleConnect: Successfully connected.");
    connected_ = true;
    connecting_ = false; // Clear connecting flag
    disconnect_requested_ = false;
    // --- Timer logic REMOVED ---
    // current_reconnect_delay_ms_ = config_.base_reconnect_ms;
    // StopReconnectTimer();
    ResubscribePending();
    if (on_connected_cb_) {
        on_connected_cb_(); // Call user callback
    }
}

void AwsIotMqttClient::HandleDisconnect() {
     // Assumes mutex is locked
    ESP_LOGW(TAG, "HandleDisconnect: Processing disconnect.");
    bool was_connected = connected_.load();
    connected_ = false;
    connecting_ = false; // Clear connecting flag

    // Mark subscriptions as needing resubscribe on next successful connect
    for (auto& sub : subscriptions_) {
        if (sub.active) {
            sub.pending_subscribe = true;
        }
    }

    // --- Reconnect scheduling REMOVED ---
    // if (!disconnect_requested_.load()) { ScheduleReconnect(); } else { ... }

    // Call user callback AFTER updating state
    if (was_connected && on_disconnected_cb_) {
        on_disconnected_cb_();
    }

    // If disconnect was unexpected, the application loop should detect
    // !IsConnected() and trigger its own reconnect logic.
    // If disconnect was requested, CleanupMqttClient was likely already called.
    // Add a safety cleanup here ONLY if disconnect wasn't requested? Risky.
    // Let Disconnect() method handle cleanup for requested disconnects.
}

void AwsIotMqttClient::ResubscribePending() { /* (No changes needed) */ }
void AwsIotMqttClient::HandleData(const char* topic, int topic_len, const char* data, int data_len) { /* (No changes needed) */ }


// --- Reconnect Logic Methods REMOVED ---
// void AwsIotMqttClient::StartReconnectTimer() { /* REMOVED */ }
// void AwsIotMqttClient::StopReconnectTimer() { /* REMOVED */ }
// void AwsIotMqttClient::ScheduleReconnect() { /* REMOVED */ }


// --- AWS IoT Specific Helpers --- (No changes needed)
bool AwsIotMqttClient::GetShadowTopic(const std::string& operation, char* buffer, size_t buffer_size) { /* ... */ }
bool AwsIotMqttClient::GetJobsTopic(const std::string& operation, const std::string& job_id, char* buffer, size_t buffer_size) { /* ... */ }
bool AwsIotMqttClient::SubscribeToShadowUpdates(ShadowUpdateCallback callback) { /* ... */ }
bool AwsIotMqttClient::SubscribeToShadowGetResponses(ShadowUpdateCallback callback) { /* ... */ }
bool AwsIotMqttClient::UpdateShadow(const std::string& shadow_payload, int qos) { /* ... */ }
bool AwsIotMqttClient::UpdateShadow(std::string_view shadow_payload, int qos) { /* ... */ }
bool AwsIotMqttClient::GetShadow(const std::string& client_token) { /* ... */ }
bool AwsIotMqttClient::SubscribeToJobs(JobNotificationCallback callback) { /* ... */ }
bool AwsIotMqttClient::UpdateJobStatus(const std::string& job_id, const std::string& status, const std::string& status_details_json) { /* ... */ }
bool AwsIotMqttClient::UpdateJobStatus(const std::string& job_id, const std::string& status, std::string_view status_details_json) { /* ... */ }

// --- Setters for Callbacks --- (No changes needed)
void AwsIotMqttClient::SetOnConnectedCallback(StatusCallback cb) { /* ... */ }
void AwsIotMqttClient::SetOnDisconnectedCallback(StatusCallback cb) { /* ... */ }

} // namespace AwsIot

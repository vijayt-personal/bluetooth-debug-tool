#ifndef AWS_IOT_MQTT_CLIENT_HPP_
#define AWS_IOT_MQTT_CLIENT_HPP_

#include <functional>
#include <string>
#include <string_view>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <memory> // For potential future PIMPL use, though not strictly needed now

// NO ESP-IDF specific headers or types here. Platform Agnostic.

namespace AwsIot {

// --- Configuration ---
constexpr size_t kMaxTopicLen = 256;    // Max length for topic strings
constexpr size_t kMaxPayloadLen = 1024; // Max length for internally generated payloads
constexpr size_t kMaxSubs = 10;         // Max concurrent subscriptions stored
// Define size for certificate buffers within MqttConfig
// WARNING: Setting this large significantly increases the size of MqttConfig objects!
// This memory will be allocated on Stack or Globally depending on where MqttConfig is declared.
constexpr size_t kMaxCertLen = 2048;

struct MqttConfig {
    // Endpoint/IDs remain strings for convenience during setup.
    // Allocation happens once when the config object is populated.
    std::string aws_endpoint;
    std::string client_id;
    std::string thing_name;
    uint16_t    port = 8883;

    // --- Certificate Buffers ---
    // WARNING: These arrays store the full certificate data and reside
    //          within the MqttConfig object (Stack/Global). Total = 3 * kMaxCertLen bytes!
    //          Ensure this size (kMaxCertLen) is adequate for your certs.
    //          You MUST copy your certificate data into these buffers
    //          before calling Initialize(). Ensure null-termination.
    char root_ca_pem[kMaxCertLen] = {0}; // Zero-initialize
    char device_cert_pem[kMaxCertLen] = {0};
    char private_key_pem[kMaxCertLen] = {0};
    // --- End Certificate Buffers ---

    // MQTT RX/TX Buffer sizes used by underlying esp-mqtt
    int         rx_buffer_size = 2048;
    int         tx_buffer_size = 2048;

    // Reconnect Timing
    uint32_t    base_reconnect_ms = 1000;
    uint32_t    max_reconnect_ms = 60000;
};

// --- Callbacks ---
using MqttMessageCallback = std::function<void(const std::string& topic, std::string_view payload)>;
using StatusCallback = std::function<void()>;
using JobNotificationCallback = std::function<void(const std::string& job_id, const std::string& status, const std::string& document)>;
using ShadowUpdateCallback = std::function<void(const std::string& update_type, std::string_view payload)>;

// --- Main Class ---
class AwsIotMqttClient {
public:
    AwsIotMqttClient();
    ~AwsIotMqttClient();

    // Disable copy/move semantics
    AwsIotMqttClient(const AwsIotMqttClient&) = delete;
    AwsIotMqttClient& operator=(const AwsIotMqttClient&) = delete;
    AwsIotMqttClient(AwsIotMqttClient&&) = delete;
    AwsIotMqttClient& operator=(AwsIotMqttClient&&) = delete;

    /**
     * @brief Initializes the MQTT client with the given configuration.
     * Certificate data must be copied into the config struct's arrays
     * *before* calling this function.
     * @param config Configuration structure containing connection details and cert data.
     * @return True on success, false otherwise.
     */
    bool Initialize(const MqttConfig& config);

    /**
     * @brief Connects to the MQTT broker asynchronously.
     * Handles auto-reconnect internally using exponential backoff.
     * @return True if connection attempt initiated, false on immediate error.
     */
    bool Connect();

    /**
     * @brief Disconnects from the MQTT broker and stops reconnect attempts.
     */
    void Disconnect();

    /**
     * @brief Checks if the client is currently connected to the broker.
     * @return True if connected, false otherwise. Thread-safe.
     */
    bool IsConnected() const;

    // --- Publish Overloads ---
    bool Publish(const std::string& topic, const std::string& payload, int qos = 0, bool retain = false);
    bool Publish(const std::string& topic, std::string_view payload, int qos = 0, bool retain = false);
    /**
     * @brief Publishes raw binary data to a specific topic.
     * @param topic The topic to publish to. Must not exceed kMaxTopicLen.
     * @param payload Pointer to the byte buffer.
     * @param len Length of the byte buffer.
     * @param qos Quality of Service level (0, 1, or 2).
     * @param retain Retain flag.
     * @return True if the publish request was successfully queued, false otherwise. Thread-safe.
     */
    bool Publish(const std::string& topic, const uint8_t* payload, size_t len, int qos = 0, bool retain = false);
    // --- End Publish Overloads ---

    /**
     * @brief Subscribes to a topic filter.
     * @param topic_filter The topic filter (e.g., "my/topic", "my/topic/#"). Must not exceed kMaxTopicLen.
     * @param qos Quality of Service level (0, 1, or 2).
     * @param callback Function to call when a message matching the filter arrives.
     * @return True if subscription stored/queued, false if max subscriptions reached. Thread-safe.
     */
    bool Subscribe(const std::string& topic_filter, int qos, MqttMessageCallback callback);

    /**
     * @brief Unsubscribes from a topic filter.
     * @param topic_filter The exact topic filter previously subscribed to.
     * @return True if unsubscribe queued or subscription removed locally, false if not found. Thread-safe.
     */
    bool Unsubscribe(const std::string& topic_filter);

    // --- AWS IoT Specific Helpers ---
    bool SubscribeToShadowUpdates(ShadowUpdateCallback callback);
    bool SubscribeToShadowGetResponses(ShadowUpdateCallback callback);
    bool UpdateShadow(const std::string& shadow_payload, int qos = 0);
    bool UpdateShadow(std::string_view shadow_payload, int qos = 0);
    bool GetShadow(const std::string& client_token = "");
    bool SubscribeToJobs(JobNotificationCallback callback);
    bool UpdateJobStatus(const std::string& job_id, const std::string& status, const std::string& status_details_json = "{}");
    bool UpdateJobStatus(const std::string& job_id, const std::string& status, std::string_view status_details_json);

    // --- Setters for Callbacks ---
    void SetOnConnectedCallback(StatusCallback cb);
    void SetOnDisconnectedCallback(StatusCallback cb);

private:
    // --- Private Implementation Details ---
    struct Subscription { // Internal struct, definition okay here
        char topic[kMaxTopicLen] = {0};
        int qos = 0;
        MqttMessageCallback callback;
        bool active = false;
        bool pending_subscribe = false;
    };

    std::mutex                  mutex_;
    MqttConfig                  config_; // Internal copy, includes large cert arrays
    std::atomic<bool>           initialized_{false};
    std::atomic<bool>           connected_{false};
    std::atomic<bool>           connecting_{false};
    std::atomic<bool>           disconnect_requested_{false};
    void* client_handle_opaque_{nullptr}; // OPAQUE HANDLE to hide esp_mqtt_client_handle_t
    Subscription                subscriptions_[kMaxSubs];
    size_t                      active_subscription_count_ = 0;
    StatusCallback              on_connected_cb_ = nullptr;
    StatusCallback              on_disconnected_cb_ = nullptr;
    JobNotificationCallback     job_notify_cb_ = nullptr;
    ShadowUpdateCallback        shadow_update_cb_ = nullptr;
    ShadowUpdateCallback        shadow_get_cb_ = nullptr;
    uint32_t                    current_reconnect_delay_ms_ = 0;
    void* reconnect_timer_handle_{nullptr}; // OPAQUE FreeRTOS Timer Handle

    // --- Private Methods --- (Declarations not needed in header)
    bool InitializeMqttClient();
    void CleanupMqttClient();
    bool SubscribeInternal(const char* topic_filter, int qos);
    bool UnsubscribeInternal(const char* topic_filter);
    void HandleMqttEvent(void* event_data); // event_data is esp_mqtt_event_handle_t passed as void*
    void HandleConnect();
    void HandleDisconnect();
    void HandleData(const char* topic, int topic_len, const char* data, int data_len);
    void ResubscribePending();
    void StartReconnectTimer();
    void StopReconnectTimer();
    void ScheduleReconnect();
    bool GetShadowTopic(const std::string& operation, char* buffer, size_t buffer_size);
    bool GetJobsTopic(const std::string& operation, const std::string& job_id, char* buffer, size_t buffer_size);

    // NOTE: Static callbacks are implementation details defined only in the .cpp file.

}; // class AwsIotMqttClient

} // namespace AwsIot

#endif // AWS_IOT_MQTT_CLIENT_HPP_

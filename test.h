#ifndef AWS_IOT_MQTT_CLIENT_HPP_
#define AWS_IOT_MQTT_CLIENT_HPP_

#include <functional>
#include <string>
#include <string_view>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <memory>

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
    std::string aws_endpoint;
    std::string client_id;
    std::string thing_name;
    uint16_t    port = 8883;

    // Certificate Buffers (Require manual copy into these before Initialize)
    char root_ca_pem[kMaxCertLen] = {0};
    char device_cert_pem[kMaxCertLen] = {0};
    char private_key_pem[kMaxCertLen] = {0};

    // MQTT RX/TX Buffer sizes used by underlying esp-mqtt
    int         rx_buffer_size = 2048;
    int         tx_buffer_size = 2048;

    // Reconnect timing is now managed by the application, removed from here.
};

// --- Callbacks ---
using MqttMessageCallback = std::function<void(const std::string& topic, std::string_view payload)>;
using StatusCallback = std::function<void()>;
using JobNotificationCallback = std::function<void(const std::string& job_id, const std::string& status, const std::string& document)>;
using ShadowUpdateCallback = std::function<void(const std::string& update_type, std::string_view payload)>;

// --- Main Class ---
class AwsIotMqttClient {
public: // Public Interface
    AwsIotMqttClient();
    ~AwsIotMqttClient();

    // Disable copy/move semantics
    AwsIotMqttClient(const AwsIotMqttClient&) = delete;
    AwsIotMqttClient& operator=(const AwsIotMqttClient&) = delete;
    AwsIotMqttClient(AwsIotMqttClient&&) = delete;
    AwsIotMqttClient& operator=(AwsIotMqttClient&&) = delete;

    /**
     * @brief Initializes the MQTT client. Cert data must be pre-copied into config.
     * @param config Configuration structure.
     * @return True on success, false otherwise.
     */
    bool Initialize(const MqttConfig& config);

    /**
     * @brief Attempts to connect to the MQTT broker.
     * NOTE: This version does NOT automatically reconnect. Reconnection attempts
     * must be managed by the application by calling Connect() again after a delay.
     * @return True if connection attempt initiated, false on immediate error.
     */
    bool Connect();

    /**
     * @brief Disconnects from the MQTT broker.
     */
    void Disconnect();

    /**
     * @brief Checks if the client is currently connected.
     * @return True if connected, false otherwise. Thread-safe.
     */
    bool IsConnected() const;

    // --- Publish Overloads ---
    bool Publish(const std::string& topic, const std::string& payload, int qos = 0, bool retain = false);
    bool Publish(const std::string& topic, std::string_view payload, int qos = 0, bool retain = false);
    bool Publish(const std::string& topic, const uint8_t* payload, size_t len, int qos = 0, bool retain = false);

    // --- Subscribe / Unsubscribe ---
    bool Subscribe(const std::string& topic_filter, int qos, MqttMessageCallback callback);
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

    // --- Public Event Handler Method ---
    /**
     * @brief Public Event Handler (Called internally via static C callback).
     * Processes MQTT events. Made public to allow calling from static C handler.
     * NOTE: Not intended for direct call by user application code.
     * @param event_data_void Pointer to the esp_mqtt_event_handle_t structure.
     */
    void HandleMqttEvent(void* event_data_void);

private: // Private Implementation Details
    // --- Private Data Members ---
    struct Subscription {
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
    std::atomic<bool>           connecting_{false}; // Prevent rapid Connect() calls
    std::atomic<bool>           disconnect_requested_{false}; // Manual disconnect requested by user
    void* client_handle_opaque_{nullptr}; // OPAQUE HANDLE
    Subscription                subscriptions_[kMaxSubs];
    size_t                      active_subscription_count_ = 0;
    StatusCallback              on_connected_cb_ = nullptr;
    StatusCallback              on_disconnected_cb_ = nullptr;
    JobNotificationCallback     job_notify_cb_ = nullptr;
    ShadowUpdateCallback        shadow_update_cb_ = nullptr;
    ShadowUpdateCallback        shadow_get_cb_ = nullptr;
    // --- Reconnect members REMOVED ---

    // --- Private Methods ---
    bool InitializeMqttClient();
    void CleanupMqttClient();
    bool SubscribeInternal(const char* topic_filter, int qos);
    bool UnsubscribeInternal(const char* topic_filter);
    // HandleMqttEvent moved to public
    void HandleConnect();
    void HandleDisconnect(); // Modified - no longer schedules reconnect
    void HandleData(const char* topic, int topic_len, const char* data, int data_len);
    void ResubscribePending();
    // --- Timer/Schedule methods REMOVED ---
    bool GetShadowTopic(const std::string& operation, char* buffer, size_t buffer_size);
    bool GetJobsTopic(const std::string& operation, const std::string& job_id, char* buffer, size_t buffer_size);

    // NOTE: Static MQTT handler callback is an implementation detail defined only in the .cpp file.

}; // class AwsIotMqttClient

} // namespace AwsIot

#endif // AWS_IOT_MQTT_CLIENT_HPP_

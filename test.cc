#ifndef AWS_IOT_MQTT_CLIENT_HPP_
#define AWS_IOT_MQTT_CLIENT_HPP_

#include <functional>
#include <string>
#include <string_view> // Include string_view explicitly
// #include <vector> // No longer needed if not using std::vector explicitly
#include <mutex>
#include <atomic>
#include <cstdint>
#include <memory> // For std::unique_ptr (though not currently used for PIMPL)

// Forward declaration for implementation details hidden in the .cpp file
struct esp_mqtt_client;
typedef struct esp_mqtt_client* esp_mqtt_client_handle_t;

namespace AwsIot {

// --- Configuration ---
constexpr size_t kMaxTopicLen = 256;
constexpr size_t kMaxPayloadLen = 1024; // Consider increasing for large binary data if needed
constexpr size_t kMaxSubs = 10;

struct MqttConfig {
    std::string aws_endpoint;
    uint16_t    port = 8883;
    std::string client_id;
    std::string thing_name;
    const char* root_ca_pem = nullptr;
    const char* device_cert_pem = nullptr;
    const char* private_key_pem = nullptr;
    int         rx_buffer_size = 2048;
    int         tx_buffer_size = 2048;
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

    bool Initialize(const MqttConfig& config);
    bool Connect();
    void Disconnect();
    bool IsConnected() const;

    // --- Publish Overloads ---
    bool Publish(const std::string& topic, const std::string& payload, int qos = 0, bool retain = false);
    bool Publish(const std::string& topic, std::string_view payload, int qos = 0, bool retain = false);
    /**
     * @brief Publishes raw binary data to a specific topic.
     * @param topic The topic to publish to.
     * @param payload Pointer to the byte buffer.
     * @param len Length of the byte buffer.
     * @param qos Quality of Service level (0, 1, or 2).
     * @param retain Retain flag.
     * @return True if the publish request was successfully queued, false otherwise. Thread-safe.
     */
    bool Publish(const std::string& topic, const uint8_t* payload, size_t len, int qos = 0, bool retain = false);
    // --- End Publish Overloads ---


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

private:
    // --- Private Implementation ---
    struct Subscription {
        char topic[kMaxTopicLen] = {0};
        int qos = 0;
        MqttMessageCallback callback;
        bool active = false;
        bool pending_subscribe = false;
    };

    std::mutex                  mutex_;
    MqttConfig                  config_;
    std::atomic<bool>           initialized_{false};
    std::atomic<bool>           connected_{false};
    std::atomic<bool>           connecting_{false};
    std::atomic<bool>           disconnect_requested_{false};
    esp_mqtt_client_handle_t    client_handle_{nullptr};
    Subscription                subscriptions_[kMaxSubs];
    size_t                      active_subscription_count_ = 0;

    StatusCallback              on_connected_cb_ = nullptr;
    StatusCallback              on_disconnected_cb_ = nullptr;
    JobNotificationCallback     job_notify_cb_ = nullptr;
    ShadowUpdateCallback        shadow_update_cb_ = nullptr;
    ShadowUpdateCallback        shadow_get_cb_ = nullptr;

    uint32_t                    current_reconnect_delay_ms_ = 0;
    void* reconnect_timer_handle_ = nullptr; // FreeRTOS Timer Handle

    // --- Private Methods ---
    bool InitializeMqttClient();
    void CleanupMqttClient();
    bool SubscribeInternal(const char* topic_filter, int qos);
    bool UnsubscribeInternal(const char* topic_filter);
    void HandleMqttEvent(void* event_data);
    void HandleConnect();
    void HandleDisconnect();
    void HandleData(const char* topic, int topic_len, const char* data, int data_len);
    void ResubscribePending();
    void StartReconnectTimer();
    void StopReconnectTimer();
    void ScheduleReconnect();

    bool GetShadowTopic(const std::string& operation, char* buffer, size_t buffer_size);
    bool GetJobsTopic(const std::string& operation, const std::string& job_id, char* buffer, size_t buffer_size);

    static void MqttEventHandlerStatic(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
    static void ReconnectTimerCallbackStatic(void* timer_arg); // FreeRTOS Timer Callback

}; // class AwsIotMqttClient

} // namespace AwsIot

#endif // AWS_IOT_MQTT_CLIENT_HPP_

#include "ProvisioningModule.h"
#include <vector>
#include <string>
#include <tuple>
#include <cstring> // For memcpy, strlen etc.
#include <algorithm> // For std::min

// --- Logging Macro (replace with your actual logging system) ---
#include <iostream>
#define LOG_INFO(msg) std::cout << "[INFO] Prov: " << msg << std::endl
#define LOG_WARN(msg) std::cout << "[WARN] Prov: " << msg << std::endl
#define LOG_ERROR(msg) std::cout << "[ERROR] Prov: " << msg << std::endl

// --- Status Codes ---
const uint32_t STATUS_OK = 0;
const uint32_t STATUS_FAIL_GENERIC = 1;
// ... other status codes ...
const uint32_t STATUS_COMMISSIONING_COMPLETE = 99;


ProvisioningModule::ProvisioningModule(BLEManager& bleManager, WiFiManager& wifiManager)
    : bleManager_(bleManager),           // Store reference
      wifiManager_(wifiManager),         // Store reference
      currentState_(ProvState::UNINITIALIZED),
      isProvClientConnected_(false)
{
    // Constructor simply stores dependencies
    LOG_INFO("Provisioning Module Created.");
}

bool ProvisioningModule::Init() {
    LOG_INFO("Initializing Provisioning Module...");
    if (currentState_ != ProvState::UNINITIALIZED) {
        LOG_WARN("Already initialized.");
        return true;
    }

    // Assumes WiFiManager is ready or initialized externally/by its constructor
    // Assumes BLEManager is ready and initialized externally

    // Register Provisioning Service and Characteristics
    if (!RegisterBleServices()) {
        LOG_ERROR("Failed to register BLE provisioning services.");
        SetState(ProvState::ERROR);
        return false;
    }

    LOG_INFO("Provisioning Module Initialized.");
    SetState(ProvState::IDLE); // Ready state
    return true;
}

bool ProvisioningModule::RegisterBleServices() {
    LOG_INFO("Registering BLE Provisioning Service...");
    std::vector<std::tuple<std::string, BLEManager::CharProperty, BLEManager::CharCallback>> characteristics;

    // --- Request Characteristic (Write) ---
    characteristics.emplace_back(
        PROV_REQUEST_CHAR_UUID,
        BLEManager::CharProperty::WRITE, // Or WRITE_NR
        nullptr // Callback handled externally, calls ProcessBleRequest
    );

    // --- Response Characteristic (Read/Notify) ---
    characteristics.emplace_back(
        PROV_RESPONSE_CHAR_UUID,
        BLEManager::CharProperty::NOTIFY, // Or READ | NOTIFY
        nullptr // No server-side callback needed
    );

    bleManager_.RegisterService(PROV_SVC_UUID, characteristics);
    // TODO: Add error checking based on BLEManager's return value
    LOG_INFO("BLE Provisioning Service Registration requested.");
    return true;
}


void ProvisioningModule::HandleConnect() {
    LOG_INFO("Handling Provisioning Client Connect.");
    isProvClientConnected_ = true;
    cloudConnectionAttempted_ = false; // Reset flag on new connection
    // Set state to connected only if idle/failed previously
     if (GetState() == ProvState::IDLE || GetState() == ProvState::ERROR || GetState() == ProvState::WIFI_FAILED || GetState() == ProvState::CLOUD_FAILED) {
         if (GetState() != ProvState::COMMISSIONED) {
              SetState(ProvState::BLE_CLIENT_CONNECTED);
         }
    } else if (GetState() == ProvState::COMMISSIONED) {
        SendStatusResponse(STATUS_COMMISSIONING_COMPLETE, "Already commissioned");
    }
}

void ProvisioningModule::HandleDisconnect() {
    LOG_INFO("Handling Provisioning Client Disconnect.");
    isProvClientConnected_ = false;
    ProvState currentState = GetState();
    if (currentState != ProvState::COMMISSIONED && currentState != ProvState::UNINITIALIZED) {
        LOG_INFO("Resetting provisioning state to IDLE due to disconnect.");
        SetState(ProvState::IDLE);
        // Stop ongoing WiFi operations if necessary
        if (wifiManager_.GetWiFiState() == WiFiState::kScanning) {
            wifiManager_.StopScan();
        }
         if (wifiManager_.GetWiFiState() == WiFiState::kConnecting) {
             wifiManager_.Disconnect();
         }
    }
}

void ProvisioningModule::ProcessBleRequest(const void* data, size_t size) {
     if (GetState() == ProvState::UNINITIALIZED) {
        LOG_ERROR("Cannot process request: Not initialized.");
        return;
    }
     if (!isProvClientConnected_) {
        LOG_WARN("Ignoring BLE request: No client connected.");
        return;
    }
    // (Add size check)
    // ...

    // --- Decode Protobuf Request ---
    ProvRequest request = ProvRequest_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(static_cast<const uint8_t*>(data), size);

    if (!pb_decode(&stream, ProvRequest_fields, &request)) {
        LOG_ERROR("Protobuf decoding failed: " + std::string(PB_GET_ERROR(&stream)));
        SendStatusResponse(STATUS_INVALID_MSG, "Protobuf decode error");
        return;
    }

    LOG_INFO("Processing Request: type=" + std::to_string(request.which_payload));

    // --- Dispatch (Switch statement remains the same) ---
    // ... calls internal handlers like HandleWifiScanRequest ...
    switch (request.which_payload) {
        case ProvRequest_scan_request_tag:
            HandleWifiScanRequest(request.payload.scan_request);
            break;
        case ProvRequest_config_request_tag:
             HandleWifiConfiguration(request.payload.config_request);
            break;
        case ProvRequest_end_request_tag:
             HandleCommissioningEnd(request.payload.end_request);
            break;
        default:
             LOG_WARN("Received unknown request type: " + std::to_string(request.which_payload));
             SendStatusResponse(STATUS_INVALID_MSG, "Unknown request type");
            break;
    }
}

// --- Internal Command Handlers ---
void ProvisioningModule::HandleWifiScanRequest(const WiFiScanRequest& request) {
    LOG_INFO("Handling WiFi Scan Request.");
    ProvState currentState = GetState();
     if (currentState != ProvState::BLE_CLIENT_CONNECTED && currentState != ProvState::WIFI_SCAN_COMPLETE && currentState != ProvState::WIFI_FAILED) {
         LOG_WARN("Ignoring Scan Request: Invalid state " + std::to_string((int)currentState));
         SendStatusResponse(STATUS_INVALID_STATE, "Cannot scan now");
         return;
     }
     if (wifiManager_.GetWiFiState() == WiFiState::kScanning) {
         LOG_WARN("Ignoring Scan Request: Scan already in progress.");
         SendStatusResponse(STATUS_BUSY, "Scan in progress");
         return;
     }

     LOG_INFO("Starting WiFi Scan...");
     SetState(ProvState::WIFI_SCANNING); // State change triggers polling in Run()
     wifiManager_.StartScan(false); // Non-blocking
     // SendStatusResponse(STATUS_OK, "Scan started"); // Optional ack
}

// (HandleWifiConfiguration and HandleCommissioningEnd remain the same as previous version)
// ...
void ProvisioningModule::HandleWifiConfiguration(const WiFiConfiguration& request) {
    // ... Check state ...
    // ... Parse credentials ...
    SetState(ProvState::WIFI_CRED_RECEIVED);
    wifiManager_.LoadCredentials(/*ssid*/ "...", /*password*/ "...");
    SetState(ProvState::WIFI_CONNECTING); // State change triggers polling in Run()
    SendStatusResponse(STATUS_WIFI_CONNECTING, "Connecting to WiFi...");
    wifiManager_.Connect();
}

void ProvisioningModule::HandleCommissioningEnd(const CommissioningEndRequest& request) {
    // ... Check state ...
    SetState(ProvState::COMMISSIONING_ENDING);
    FinalizeCommissioning();
    SetState(ProvState::COMMISSIONED);
    SendCommissioningEndResponse(); // Or SendStatusResponse
}
// ...


// --- Periodic Execution (`Run()`) ---
void ProvisioningModule::Run() {
     ProvState currentState = GetState();

     // Check different states where polling/action is needed
     switch (currentState) {
          case ProvState::WIFI_SCANNING:
               CheckWifiScanStatus();
               break;

          case ProvState::WIFI_CONNECTING:
               CheckWifiConnectionStatus();
               break;

          case ProvState::WIFI_CONNECTED:
               // Attempt cloud connection only once after entering this state
               if (!cloudConnectionAttempted_) {
                    AttemptCloudConnection();
                    cloudConnectionAttempted_ = true; // Mark as attempted
               }
               break;

          // Add cases for timeouts or other periodic checks if needed
          // case ProvState::CLOUD_CONNECTING:
          //      // Check cloud connection timeout?
          //      break;

          default:
               // No periodic action needed for other states like IDLE, COMMISSIONED, etc.
               break;
     }
}


// --- Internal Polling/Action Helpers called from Run() ---

void ProvisioningModule::CheckWifiScanStatus() {
     if (wifiManager_.GetWiFiState() != WiFiState::kScanning) {
          LOG_INFO("WiFi Scan appears complete (state changed from Scanning).");
          std::vector<APInfo> results = wifiManager_.GetScanResults();
          LOG_INFO("Found " + std::to_string(results.size()) + " networks.");
          SetState(ProvState::WIFI_SCAN_COMPLETE);
          SendWifiScanResults(results);
     }
     // Else: Still scanning, do nothing this cycle.
}

void ProvisioningModule::CheckWifiConnectionStatus() {
     WiFiState wifiState = wifiManager_.GetWiFiState();

     if (wifiState == WiFiState::kConnected) {
          LOG_INFO("WiFi Connected Successfully (polled).");
          SetState(ProvState::WIFI_CONNECTED);
          SendStatusResponse(STATUS_WIFI_CONNECTED, "WiFi Connected");
          // Cloud connection will be attempted in the next Run cycle
     } else if (wifiState == WiFiState::kDisconnected) {
          // Assumes Disconnected state after Connecting means failure
          LOG_ERROR("WiFi Connection Failed (polled).");
          SetState(ProvState::WIFI_FAILED);
          // TODO: Get specific failure reason if WiFiManager provides it
          SendStatusResponse(STATUS_WIFI_FAIL_OTHER, "WiFi Connection Failed");
     }
     // Else: Still connecting, do nothing this cycle.
}

void ProvisioningModule::AttemptCloudConnection() {
    LOG_INFO("Attempting Cloud Connection...");
    SetState(ProvState::CLOUD_CONNECTING); // Set state *before* blocking/async call
    SendStatusResponse(STATUS_CLOUD_CONNECTING, "Connecting to Cloud...");

    // Perform actual connection (blocking or trigger async operation)
    if (ConnectToCloud()) {
        LOG_INFO("Cloud Connection Successful.");
        SetState(ProvState::CLOUD_CONNECTED);
        SendStatusResponse(STATUS_CLOUD_CONNECTED, "Cloud Connected");
    } else {
        LOG_ERROR("Cloud Connection Failed.");
        SetState(ProvState::CLOUD_FAILED);
        SendStatusResponse(STATUS_CLOUD_FAIL, "Cloud Connection Failed");
    }
}


// --- Internal Response Senders ---
// (SendResponse, SendStatusResponse, SendWifiScanResults, SendCommissioningEndResponse
//  remain the same as the previous version, using bleManager_ reference)
// ... Example:
bool ProvisioningModule::SendResponse(const ProvResponse& response) {
     if (!isProvClientConnected_) {
         LOG_WARN("Cannot send response: Client not connected.");
         return false;
     }
     // ... (encode using nanopb into pb_response_buffer_) ...
     pb_ostream_t stream = pb_ostream_from_buffer(pb_response_buffer_, sizeof(pb_response_buffer_));
     if (!pb_encode(&stream, ProvResponse_fields, &response)) {
        // ... handle encode error ...
        return false;
     }
     // Use the injected BLE manager reference
     bleManager_.SetCharacteristicValue(PROV_RESPONSE_CHAR_UUID, pb_response_buffer_, stream.bytes_written, true); // Notify
     return true;
}

void ProvisioningModule::SendStatusResponse(uint32_t statusCode, const std::string& message) {
     ProvResponse response = ProvResponse_init_zero;
     response.which_payload = ProvResponse_status_tag;
     // ... populate status fields ...
     SendResponse(response);
}

void ProvisioningModule::SendWifiScanResults(const std::vector<APInfo>& results) {
     ProvResponse response = ProvResponse_init_zero;
     response.which_payload = ProvResponse_scan_results_tag;
     // ... populate scan results list ...
     SendResponse(response);
}

void ProvisioningModule::SendCommissioningEndResponse() {
     ProvResponse response = ProvResponse_init_zero;
     response.which_payload = ProvResponse_end_response_tag;
     // ... populate end response ...
     SendResponse(response);
}
// ...

// --- Internal Helpers ---
void ProvisioningModule::SetState(ProvState newState) {
    ProvState oldState = currentState_.exchange(newState);
    if (oldState != newState) {
        LOG_INFO("Prov State changed: " + std::to_string((int)oldState) + " -> " + std::to_string((int)newState));
        // Reset cloud attempt flag when leaving WIFI_CONNECTED state relevantly
        if (oldState == ProvState::WIFI_CONNECTED && newState != ProvState::CLOUD_CONNECTING && newState != ProvState::CLOUD_CONNECTED && newState != ProvState::COMMISSIONING_ENDING) {
             cloudConnectionAttempted_ = false;
        }
    }
}

// (FinalizeCommissioning, ConnectToCloud remain the same placeholders)
// ...
void ProvisioningModule::FinalizeCommissioning() {
     LOG_INFO("Performing final device configuration steps...");
}
bool ProvisioningModule::ConnectToCloud() {
    LOG_INFO("Simulating cloud connection attempt...");
    bool success = (rand() % 5 != 0);
    return success;
}
// ...

ProvisioningModule::ProvState ProvisioningModule::GetState() const {
    return currentState_.load();
}

-------------------------------------------------------------------------------------------------------------

  #ifndef PROVISIONING_MODULE_H_
#define PROVISIONING_MODULE_H_

#include "WiFiManager.h" // Dependency
#include "BLEManager.h"   // Dependency
#include <string>
#include <vector>
#include <atomic>
#include <cstdint>

// --- Nanopb Includes ---
#include "pb_encode.h"
#include "pb_decode.h"
#include "provisioning.pb.h" // Assuming top-level ProvRequest/ProvResponse
#include "wifiscanning.pb.h"
#include "wificonfiguration.pb.h"
#include "commissioningend.pb.h"
// Add others as needed

// --- Placeholder UUIDs ---
// Ensure these match the definitions used externally
#define PROV_SVC_UUID               "YOUR_PROV_SERVICE_UUID"  // Replace
#define PROV_REQUEST_CHAR_UUID      "YOUR_REQUEST_CHAR_UUID"  // Replace
#define PROV_RESPONSE_CHAR_UUID     "YOUR_RESPONSE_CHAR_UUID" // Replace

// --- Max buffer size for nanopb ---
#define MAX_PROTO_MSG_SIZE 256

class ProvisioningModule {
public:
    // Provisioning process states
    enum class ProvState {
        UNINITIALIZED,
        IDLE,                 // Waiting for BLE connection or commands
        BLE_CLIENT_CONNECTED, // Client connected (via HandleConnect)
        WIFI_SCANNING,
        WIFI_SCAN_COMPLETE,   // Scan results obtained (polled)
        WIFI_CRED_RECEIVED,
        WIFI_CONNECTING,
        WIFI_CONNECTED,
        WIFI_FAILED,
        CLOUD_CONNECTING,
        CLOUD_CONNECTED,
        CLOUD_FAILED,
        COMMISSIONING_ENDING,
        COMMISSIONED,
        ERROR
    };

    /**
     * @brief Constructor. Injects BLE and WiFi manager dependencies.
     * @param bleManager Reference to the BLEManager instance.
     * @param wifiManager Reference to the WiFiManager instance.
     */
    ProvisioningModule(BLEManager& bleManager, WiFiManager& wifiManager);

    /**
     * @brief Initializes the Provisioning Module's internal state and
     * registers the provisioning BLE service and characteristics.
     * Call this once after dependencies are ready.
     * @return True if initialization (service registration) was successful, false otherwise.
     */
    bool Init();

    /**
     * @brief Notifies the module that a BLE client has connected.
     */
    void HandleConnect();

    /**
     * @brief Notifies the module that a BLE client has disconnected.
     */
    void HandleDisconnect();

    /**
     * @brief Processes incoming data received on the BLE request characteristic.
     * @param data Pointer to the received data buffer.
     * @param size Size of the received data.
     */
    void ProcessBleRequest(const void* data, size_t size);

    /**
     * @brief Periodic task function. Handles polling for WiFi state changes
     * (scan complete, connection status) and drives state machine.
     */
    void Run();

    /**
     * @brief Gets the current provisioning state.
     * @return The current ProvState.
     */
    ProvState GetState() const;

private:
    // --- Dependencies (Stored References) ---
    BLEManager& bleManager_;
    WiFiManager& wifiManager_;

    // --- State ---
    std::atomic<ProvState> currentState_;
    std::atomic<bool> isProvClientConnected_; // Is HandleConnect active?
    bool cloudConnectionAttempted_ = false; // Flag for Run() logic

    // --- Buffers for Nanopb ---
    uint8_t pb_request_buffer_[MAX_PROTO_MSG_SIZE];
    uint8_t pb_response_buffer_[MAX_PROTO_MSG_SIZE];

    // --- Internal Command Handlers ---
    void HandleWifiScanRequest(const WiFiScanRequest& request);
    void HandleWifiConfiguration(const WiFiConfiguration& request);
    void HandleCommissioningEnd(const CommissioningEndRequest& request);

    // --- Internal Response Senders ---
    bool SendResponse(const ProvResponse& response); // Encodes and sends over BLE
    void SendStatusResponse(uint32_t statusCode, const std::string& message = "");
    void SendWifiScanResults(const std::vector<APInfo>& results);
    void SendCommissioningEndResponse(/* potential data */);

    // --- Internal Helper Methods ---
    void SetState(ProvState newState);
    bool RegisterBleServices(); // Uses stored bleManager_
    void CheckWifiScanStatus(); // Called from Run()
    void CheckWifiConnectionStatus(); // Called from Run()
    void AttemptCloudConnection(); // Called from Run()
    void FinalizeCommissioning();

    // --- Placeholder Cloud Functions ---
    bool ConnectToCloud();
};

#endif // PROVISIONING_MODULE_H_

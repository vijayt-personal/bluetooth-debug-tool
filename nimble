#ifndef PLATFORM_BLE_MANAGER_H_
#define PLATFORM_BLE_MANAGER_H_

#include <functional>
#include <map>
#include <string>
#include <vector>
#include <cstdint> // For uint16_t, etc.
#include <memory>

// No platform-specific headers or forward declarations needed.

namespace platform {
namespace connectivity {

/**
 * @brief BLEManager class (Singleton) for managing BLE services (Platform Agnostic Header).
 */
class BLEManager {
public:
    /** @brief BLE connection state */
    enum class BLEState { IDLE, ADVERTISING, CONNECTED, DISCONNECTED };

    /** @brief BLE characteristic property flags (matching common BLE properties) */
    enum class CharProperty : uint8_t {
        READ       = 0x02, // Corresponds to BLE_GATT_CHR_F_READ
        WRITE      = 0x08, // Corresponds to BLE_GATT_CHR_F_WRITE
        NOTIFY     = 0x10, // Corresponds to BLE_GATT_CHR_F_NOTIFY
        // Add others like WRITE_NO_RSP (0x04), INDICATE (0x20) if needed
    };

    /** @brief User callback type for characteristic write events */
    using CharWriteCallback = std::function<void(const std::string& uuid, const void* data, size_t size)>;
    /** @brief User callback for connection events */
    using ConnectionCallback = std::function<void()>;
    /** @brief User callback for disconnection events */
    using DisconnectionCallback = std::function<void()>;

    /**
     * @brief Get the Singleton instance.
     * @return Reference to the BLEManager instance.
     */
    static BLEManager& GetInstance();

    // Delete copy/move semantics for Singleton
    BLEManager(const BLEManager&) = delete;
    BLEManager& operator=(const BLEManager&) = delete;
    BLEManager(BLEManager&&) = delete;
    BLEManager& operator=(BLEManager&&) = delete;

    /**
     * @brief Initializes the BLE Manager and the underlying BLE stack.
     * NOTE (ESP-IDF): Call esp_nimble_hci_and_controller_init() before this.
     * @param device_name The name the device will advertise.
     * @return True on success, false on failure.
     */
    bool Init(const std::string& device_name);

    /**
     * @brief Defines a BLE service and its characteristics.
     * Must be called before StartAdvertising(). Can be called multiple times for different services.
     * @param service_uuid UUID of the service as string ("xxxx" or "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx").
     * @param characteristics Vector of tuples: {char_uuid_str, char_properties_bitmask, write_callback}.
     * Properties are bitwise OR'd flags from CharProperty enum.
     * Callback is invoked on successful write operations to that characteristic.
     * @return True if definition was accepted, false otherwise (e.g., already registered/advertising).
     */
    bool RegisterService(
        const std::string& service_uuid,
        const std::vector<std::tuple<std::string, uint8_t, CharWriteCallback>>& characteristics);

    /**
     * @brief Set the value of a characteristic and optionally notify connected clients.
     * @param char_uuid UUID of the characteristic as string.
     * @param value Raw data buffer.
     * @param size Size of the data buffer.
     * @param notify If true, send a notification (if characteristic supports it and handle is known).
     * @return True on success (value cached, notification attempted if applicable), false on failure.
     */
    bool SetCharacteristicValue(const std::string& char_uuid, const void* value, size_t size, bool notify = false);

    /**
     * @brief Disconnect the current BLE connection, if any.
     */
    void Disconnect();

    /**
     * @brief Set the callback function to be invoked upon client connection.
     */
    void SetConnectionCallback(ConnectionCallback callback);

    /**
     * @brief Set the callback function to be invoked upon client disconnection.
     */
    void SetDisconnectionCallback(DisconnectionCallback callback);

    /**
     * @brief Starts BLE advertising with the configured services.
     * This also triggers the internal registration of defined services with the underlying BLE stack.
     * @param connectable_enable True for connectable advertising, false for non-connectable.
     * @param adv_interval_ms Advertising interval in milliseconds (approx).
     * @return True if advertising started successfully, false otherwise.
     */
    bool StartAdvertising(bool connectable_enable = true, uint16_t adv_interval_ms = 100);

    /**
     * @brief Stops BLE advertising.
     */
    void StopAdvertising();

    /**
     * @brief Get the current BLE connection state.
     * @return Current BLEState.
     */
    BLEState GetState();

private:
    // --- Private Constructor for Singleton ---
    BLEManager();
    ~BLEManager();

    // --- Internal Structures (using void* for opaque platform pointers) ---
    struct CharacteristicDefinition {
        std::string uuid_str;
        uint8_t properties; // Bitmask of CharProperty flags
        CharWriteCallback write_callback;
        std::vector<uint8_t> value_cache; // Internal value cache
        uint16_t attr_handle = 0;         // GATT attribute handle (0 if unknown)
        void* c_chr_def_ptr = nullptr;    // Opaque pointer to underlying C characteristic definition
    };

    struct ServiceDefinition {
        std::string uuid_str;
        std::vector<CharacteristicDefinition> characteristics;
        void* c_svc_def_ptr = nullptr;    // Opaque pointer to underlying C service definition
        void* c_svc_uuid_ptr = nullptr;   // Opaque pointer to allocated C UUID structure
    };

    // --- Private Methods ---
    /** @brief Performs actual registration with BLE stack. Called before advertising. */
    bool _RegisterGATTDatabase();
    /** @brief Frees memory allocated for underlying C GATT definitions and UUIDs. */
    void _FreeGATTDefinitions();

    // --- Static C Callback Functions (Friend declarations allow access to private static members) ---
    // These need C linkage if callbacks are set directly in C API, but NimBLE allows standard C++ static functions.
    friend int gatt_svr_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
    friend int gap_event_cb(struct ble_gap_event *event, void *arg);
    friend void ble_host_task(void *param);


    // --- Static Members (State managed globally due to underlying C API nature) ---
    static bool s_initialized_;
    static bool s_gatt_registered_;
    static BLEState s_state_;
    static uint16_t s_conn_handle_;
    static std::string s_device_name_;
    static ConnectionCallback s_on_connect_cb_;
    static DisconnectionCallback s_on_disconnect_cb_;
    static std::vector<ServiceDefinition> s_service_definitions_; // Stores user-defined services
    static void* s_ble_mutex_; // Use void* to hide FreeRTOS type SemaphoreHandle_t

    // Opaque Pointers to allocated underlying C definition arrays
    static void* s_gatt_svc_defs_; // Pointer to allocated array of C service definitions
    static void* s_gatt_chr_defs_; // Pointer to allocated array of C characteristic definitions

}; // class BLEManager

// Helper operators for CharProperty bitmasking
inline BLEManager::CharProperty operator|(BLEManager::CharProperty a, BLEManager::CharProperty b) {
    return static_cast<BLEManager::CharProperty>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline uint8_t& operator|=(uint8_t& a, BLEManager::CharProperty b) {
    a |= static_cast<uint8_t>(b);
    return a;
}
// Check if flags contain a specific property
inline bool operator&(uint8_t flags, BLEManager::CharProperty prop) {
    return (flags & static_cast<uint8_t>(prop));
}


} // namespace connectivity
} // namespace platform

#endif // PLATFORM_BLE_MANAGER_H_

---------------------------------------------

#include "ble_manager.h"

#include <cstring> // For memcpy, memset, strlen
#include <cstdlib> // For malloc, free, strtol
#include <vector>
#include <map>
#include <memory>
#include <algorithm> // For std::find_if
#include <exception> // For std::exception in callbacks

// ESP-IDF & NimBLE C Headers
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_nimble_hci.h" // Required before nimble includes
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h" // For ble_uuid_from_str, addr_str
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// Define a logging tag
static const char* TAG = "BLEManager";

// --- Static C Callback Forward Declarations ---
static int gatt_svr_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gap_event_cb(struct ble_gap_event *event, void *arg);
static void ble_host_task(void *param);
static void ble_sync_callback(void);
static void ble_reset_callback(int reason);


namespace platform {
namespace connectivity {

// --- Static Member Definitions ---
bool BLEManager::s_initialized_ = false;
bool BLEManager::s_gatt_registered_ = false;
BLEManager::BLEState BLEManager::s_state_ = BLEManager::BLEState::IDLE;
uint16_t BLEManager::s_conn_handle_ = BLE_HS_CONN_HANDLE_NONE;
std::string BLEManager::s_device_name_ = "nimble-device"; // Default name
BLEManager::ConnectionCallback BLEManager::s_on_connect_cb_ = nullptr;
BLEManager::DisconnectionCallback BLEManager::s_on_disconnect_cb_ = nullptr;
std::vector<BLEManager::ServiceDefinition> BLEManager::s_service_definitions_;
void* BLEManager::s_ble_mutex_ = nullptr;
void* BLEManager::s_gatt_svc_defs_ = nullptr; // Using void*
void* BLEManager::s_gatt_chr_defs_ = nullptr; // Using void*


// Helper: Lock the mutex
static inline bool lock_mutex() {
    if (!BLEManager::s_ble_mutex_) return false;
    return xSemaphoreTake(static_cast<SemaphoreHandle_t>(BLEManager::s_ble_mutex_), portMAX_DELAY) == pdTRUE;
}

// Helper: Unlock the mutex
static inline void unlock_mutex() {
    if (!BLEManager::s_ble_mutex_) return;
    xSemaphoreGive(static_cast<SemaphoreHandle_t>(BLEManager::s_ble_mutex_));
}

// Helper to convert std::string UUID to ble_uuid_t (allocates memory!)
// Returns allocated pointer on success (caller must free!), NULL on error.
static ble_uuid_t* alloc_string_to_uuid(const std::string& uuid_str) {
    if (uuid_str.length() == 4) { // 16-bit UUID "xxxx"
        ble_uuid16_t* u16 = (ble_uuid16_t*)malloc(sizeof(ble_uuid16_t));
        if (!u16) return nullptr;
        u16->u.type = BLE_UUID_TYPE_16;
        char* end = nullptr;
        // Use strtoul for unsigned conversion, check for errors
        unsigned long val = strtoul(uuid_str.c_str(), &end, 16);
        if (*end != '\0' || val > UINT16_MAX) { // Handle conversion error or overflow
            free(u16);
            ESP_LOGE(TAG, "Invalid 16-bit UUID string: %s", uuid_str.c_str());
            return nullptr;
         }
        u16->value = (uint16_t)val;
        return &u16->u;
    } else if (uuid_str.length() == 36) { // 128-bit UUID "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
        ble_uuid128_t* u128 = (ble_uuid128_t*)malloc(sizeof(ble_uuid128_t));
         if (!u128) return nullptr;
        u128->u.type = BLE_UUID_TYPE_128;
        if (ble_uuid_from_str(uuid_str.c_str(), &u128->value) != 0) {
            free(u128);
            ESP_LOGE(TAG, "Invalid 128-bit UUID string: %s", uuid_str.c_str());
            return nullptr;
        }
        return &u128->u;
    }
    ESP_LOGE(TAG, "UUID format not supported (use xxxx or xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx): %s", uuid_str.c_str());
    return nullptr;
}

// --- GATT Server Access Callback (C Style) ---
static int gatt_svr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // arg points to our CharacteristicDefinition
    BLEManager::CharacteristicDefinition* char_def = static_cast<BLEManager::CharacteristicDefinition*>(arg);
    if (!char_def) {
        ESP_LOGE(TAG, "GATT CB Error: Invalid arg pointer!");
        return BLE_ATT_ERR_UNLIKELY;
    }

    int rc = BLE_ATT_ERR_UNLIKELY; // Default error

    if (!lock_mutex()) {
         ESP_LOGE(TAG, "GATT CB Error: Failed to lock mutex!");
         return BLE_ATT_ERR_UNLIKELY;
    }

    // Store the attribute handle if we don't have it yet (crucial for notifications)
    // Check if the handle belongs to the characteristic value itself.
    if (char_def->attr_handle == 0) {
         // Cast opaque pointer back to check against context's characteristic definition pointer
         if(ctxt->chr == static_cast<const ble_gatt_chr_def*>(char_def->c_chr_def_ptr)) {
             // NimBLE provides the value handle directly in the characteristic definition structure
             // after registration (if val_handle ptr was provided to ble_gatt_chr_def)
             // Or, the attr_handle in the context might be the value handle during direct access.
            char_def->attr_handle = attr_handle; // Store the handle seen during access
            ESP_LOGI(TAG, "GATT CB: Stored handle %d for char %s", char_def->attr_handle, char_def->uuid_str.c_str());
         }
        // Add more robust handle detection if needed (e.g., checking handle ranges if known)
    }

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGD(TAG, "GATT Read CB: Char %s (handle %d)", char_def->uuid_str.c_str(), char_def->attr_handle);
        // Append data from our internal cache to the response mbuf
        rc = os_mbuf_append(ctxt->om, char_def->value_cache.data(), char_def->value_cache.size());
        rc = (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        break;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        ESP_LOGD(TAG, "GATT Write CB: Char %s (handle %d)", char_def->uuid_str.c_str(), char_def->attr_handle);
        {
            size_t data_len = OS_MBUF_PKTLEN(ctxt->om);
            // Resize cache and copy data from mbuf
            char_def->value_cache.resize(data_len);
            int flatten_rc = ble_hs_mbuf_to_flat(ctxt->om, char_def->value_cache.data(), data_len, NULL);

            if (flatten_rc == 0) {
                ESP_LOGI(TAG, "GATT Write CB: Received %d bytes for char %s", (int)data_len, char_def->uuid_str.c_str());
                // Call the user's C++ callback if registered
                if (char_def->write_callback) {
                    // Unlock *before* calling user code to prevent potential deadlocks
                    unlock_mutex();
                    try {
                       char_def->write_callback(char_def->uuid_str, char_def->value_cache.data(), char_def->value_cache.size());
                       // Re-lock is not needed after callback finishes here
                    } catch (const std::exception& e) {
                        ESP_LOGE(TAG, "Exception in write callback for %s: %s", char_def->uuid_str.c_str(), e.what());
                    } catch (...) {
                         ESP_LOGE(TAG, "Unknown exception in write callback for %s", char_def->uuid_str.c_str());
                    }
                    return 0; // Callback handled, return success without re-locking
                }
                // If no callback, just return success
                rc = 0;
            } else {
                ESP_LOGE(TAG, "GATT Write CB: Failed to flatten mbuf for %s", char_def->uuid_str.c_str());
                rc = BLE_ATT_ERR_UNLIKELY;
            }
        }
        break;

    default:
        ESP_LOGW(TAG, "GATT CB: Unhandled op %d for char %s", ctxt->op, char_def->uuid_str.c_str());
        rc = BLE_ATT_ERR_UNSUPPORTED_OPCODE;
        break;
    }

    unlock_mutex();
    return rc;
}


// --- GAP Event Callback (C Style) ---
static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    ESP_LOGD(TAG, "GAP Event: type=%d", event->type);

    if (!lock_mutex()) {
         ESP_LOGE(TAG, "GAP CB Error: Failed to lock mutex!");
         return 0;
    }

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "GAP Connect: status=%d handle=%d", event->connect.status, event->connect.conn_handle);
        if (event->connect.status == 0) { // Success
             BLEManager::s_conn_handle_ = event->connect.conn_handle;
             BLEManager::s_state_ = BLEManager::BLEState::CONNECTED;

             // Log peer address
             rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
             if (rc == 0) {
                  ESP_LOGI(TAG, " Connected to %s", addr_str(desc.peer_id_addr.val));
             }

             // Call user C++ callback if registered
            if (BLEManager::s_on_connect_cb_) {
                 unlock_mutex(); // Unlock before user callback
                 try { BLEManager::s_on_connect_cb_(); }
                 catch (const std::exception& e) { ESP_LOGE(TAG, "Exc in connect cb: %s", e.what()); }
                 catch (...) { ESP_LOGE(TAG, "Unknown exc in connect cb"); }
                 return 0; // Callback handled, return without re-locking
            }
        } else {
             BLEManager::s_state_ = BLEManager::BLEState::IDLE;
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
         ESP_LOGI(TAG, "GAP Disconnect: handle=%d reason=0x%02X (%s)",
                 event->disconnect.conn.conn_handle,
                 event->disconnect.reason,
                 ble_hs_err_str(event->disconnect.reason)); // Log NimBLE error string
         BLEManager::s_conn_handle_ = BLE_HS_CONN_HANDLE_NONE;
         BLEManager::s_state_ = BLEManager::BLEState::DISCONNECTED;

         // Call user C++ callback if registered
         if (BLEManager::s_on_disconnect_cb_) {
              unlock_mutex(); // Unlock before user callback
              try { BLEManager::s_on_disconnect_cb_(); }
              catch (const std::exception& e) { ESP_LOGE(TAG, "Exc in disconnect cb: %s", e.what()); }
              catch (...) { ESP_LOGE(TAG, "Unknown exc in disconnect cb"); }
              return 0; // Callback handled, return without re-locking
         }
         break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "GAP Advertising Complete: reason=0x%02X (%s)",
                event->adv_complete.reason,
                ble_hs_err_str(event->adv_complete.reason));
        if (BLEManager::s_state_ == BLEManager::BLEState::ADVERTISING) {
             BLEManager::s_state_ = BLEManager::BLEState::IDLE;
        }
        break;

     case BLE_GAP_EVENT_MTU:
         ESP_LOGI(TAG, "GAP MTU Update: handle=%d channel=%d mtu=%d",
                  event->mtu.conn_handle, event->mtu.channel_id, event->mtu.value);
         break;

    case BLE_GAP_EVENT_SUBSCRIBE:
         ESP_LOGI(TAG, "GAP Subscribe: handle=%d attr_handle=%d reason=%d prev=%d cur=%d notify=%d indicate=%d",
                  event->subscribe.conn_handle, event->subscribe.attr_handle, event->subscribe.reason,
                  event->subscribe.prev_notify, event->subscribe.cur_notify,
                  event->subscribe.prev_indicate, event->subscribe.cur_indicate);
         break;

    default:
         ESP_LOGD(TAG, "GAP Event (Unhandled): type=%d", event->type);
        break;
    }

    unlock_mutex();
    return 0;
}

// --- BLE Host Task Function ---
static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run(); // This blocks until nimble_port_stop() is called
    nimble_port_freertos_deinit();
    ESP_LOGI(TAG, "BLE Host Task Stopped");
}

// --- BLE Stack Sync/Reset Callbacks ---
static void ble_sync_callback(void) { ESP_LOGI(TAG, "BLE Host Synchronized"); }
static void ble_reset_callback(int reason)
{
    ESP_LOGE(TAG, "BLE Host Reset: reason=%d (0x%X)", reason, reason);
     if (lock_mutex()) {
        BLEManager::s_initialized_ = false;
        BLEManager::s_state_ = BLEManager::BLEState::IDLE;
        BLEManager::s_conn_handle_ = BLE_HS_CONN_HANDLE_NONE;
        BLEManager::s_gatt_registered_ = false;
        // Optionally clear definitions and free GATT defs here too
        unlock_mutex();
     }
}

// --- BLEManager Method Implementations ---

// Private constructor
BLEManager::BLEManager() {
    if (s_ble_mutex_ == nullptr) {
        s_ble_mutex_ = xSemaphoreCreateMutexStatic(&s_ble_mutex_buffer_); // Use static allocation
         if (s_ble_mutex_ == nullptr) {
             ESP_LOGE(TAG, "Failed to create static BLE manager mutex!");
             assert(s_ble_mutex_ != nullptr && "Failed to create BLE Mutex");
        }
    }
}

// Destructor
BLEManager::~BLEManager() {
    // Mutex created with xSemaphoreCreateMutexStatic doesn't need deletion
    // If dynamic allocation was used:
    // if (s_ble_mutex_ != nullptr) {
    //    vSemaphoreDelete(static_cast<SemaphoreHandle_t>(s_ble_mutex_));
    //    s_ble_mutex_ = nullptr;
    // }
     _FreeGATTDefinitions(); // Ensure C defs are freed
}

// Get Singleton Instance
BLEManager& BLEManager::GetInstance() {
    static BLEManager instance;
    return instance;
}

// Init
bool BLEManager::Init(const std::string& device_name) {
     ESP_LOGI(TAG, "Initializing BLEManager...");
    if (!lock_mutex()) { ESP_LOGE(TAG, "Init: Mutex lock failed!"); return false; }

    if (s_initialized_) {
        ESP_LOGW(TAG, "BLEManager already initialized.");
        unlock_mutex(); return true;
    }
    s_device_name_ = device_name;

    // Configure BLE host stack callbacks
    ble_hs_cfg.sync_cb = ble_sync_callback;
    ble_hs_cfg.reset_cb = ble_reset_callback;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_svc_gap_device_name_set(s_device_name_.c_str());
    if (rc != 0) { ESP_LOGE(TAG, "Failed to set device name: rc=%d", rc); }

    // Start the NimBLE host background task
    nimble_port_freertos_init(ble_host_task);

    s_state_ = BLEState::IDLE;
    s_initialized_ = true;
    s_gatt_registered_ = false;
    s_conn_handle_ = BLE_HS_CONN_HANDLE_NONE;
    s_service_definitions_.clear();
    _FreeGATTDefinitions();

    unlock_mutex();
    ESP_LOGI(TAG, "BLEManager initialization sequence complete.");
    return true;
}

// Register Service Definition
bool BLEManager::RegisterService(
    const std::string& service_uuid,
    const std::vector<std::tuple<std::string, uint8_t, CharWriteCallback>>& characteristics)
{
    if (!lock_mutex()) return false;
    if (!s_initialized_) { ESP_LOGE(TAG, "RegisterService: Not initialized."); unlock_mutex(); return false; }
    if (s_gatt_registered_) { ESP_LOGE(TAG, "RegisterService: Cannot register after GATT DB active."); unlock_mutex(); return false; }

    ESP_LOGI(TAG, "Defining Service %s for registration", service_uuid.c_str());
    ServiceDefinition new_service;
    new_service.uuid_str = service_uuid;
    for (const auto& char_tuple : characteristics) {
        CharacteristicDefinition new_char;
        new_char.uuid_str = std::get(char_tuple);
        new_char.properties = std::get(char_tuple);
        new_char.write_callback = std::get(char_tuple);
        new_char.attr_handle = 0;
        new_char.c_chr_def_ptr = nullptr; // Will be set during _RegisterGATTDatabase
        new_service.characteristics.push_back(std::move(new_char));
        ESP_LOGD(TAG, " Defined Char %s", new_char.uuid_str.c_str());
    }
    s_service_definitions_.push_back(std::move(new_service));
    unlock_mutex();
    return true;
}

// Free memory allocated for C GATT definition structures and UUIDs
void BLEManager::_FreeGATTDefinitions() {
    // Cast void* back to specific type for iteration
    ble_gatt_svc_def* svc_defs_ptr = static_cast<ble_gatt_svc_def*>(s_gatt_svc_defs_);
    if (svc_defs_ptr) {
        int svc_idx = 0;
        while(svc_defs_ptr[svc_idx].type != 0) {
            free((void*)svc_defs_ptr[svc_idx].uuid); // Free allocated service UUID
            const ble_gatt_chr_def* chrs = svc_defs_ptr[svc_idx].characteristics;
            if (chrs) {
                int chr_idx = 0;
                while(chrs[chr_idx].uuid != NULL) {
                    free((void*)chrs[chr_idx].uuid); // Free allocated characteristic UUID
                    chr_idx++;
                }
            }
            svc_idx++;
        }
        free(s_gatt_svc_defs_); // Free the main service definition array
        s_gatt_svc_defs_ = nullptr;
    }
    free(s_gatt_chr_defs_); // Free the characteristic definition array
    s_gatt_chr_defs_ = nullptr;

    // Clear internal pointers in C++ definitions
    for (auto& svc_def : s_service_definitions_) {
        svc_def.c_svc_def_ptr = nullptr;
        svc_def.c_svc_uuid_ptr = nullptr;
        for (auto& char_def : svc_def.characteristics) {
            char_def.c_chr_def_ptr = nullptr;
        }
    }
}


// Internal: Build C GATT structures and register with NimBLE stack
bool BLEManager::_RegisterGATTDatabase() {
    // Assumes mutex is held
    if (s_gatt_registered_) return true;
    if (s_service_definitions_.empty()) return true;
    _FreeGATTDefinitions(); // Free any previous attempt

    // --- Allocation ---
    int total_svcs = s_service_definitions_.size();
    int total_chrs = 0;
    for (const auto& svc_def : s_service_definitions_) total_chrs += svc_def.characteristics.size();
    s_gatt_svc_defs_ = calloc(total_svcs + 1, sizeof(ble_gatt_svc_def));
    s_gatt_chr_defs_ = calloc(total_chrs + total_svcs, sizeof(ble_gatt_chr_def)); // +1 null term per service
    if (!s_gatt_svc_defs_ || !s_gatt_chr_defs_) { ESP_LOGE(TAG,"GATT alloc failed!"); _FreeGATTDefinitions(); return false; }
    ESP_LOGI(TAG,"Building GATT DB: %d services, %d characteristics", total_svcs, total_chrs);

    // Cast void* back to specific type for population
    ble_gatt_svc_def* svc_defs_ptr = static_cast<ble_gatt_svc_def*>(s_gatt_svc_defs_);
    ble_gatt_chr_def* chr_defs_ptr = static_cast<ble_gatt_chr_def*>(s_gatt_chr_defs_);

    // --- Population ---
    int svc_idx = 0;
    int chr_idx_offset = 0;
    for (auto& svc_def : s_service_definitions_) { // Need non-const ref
        ble_gatt_svc_def* current_c_svc = &svc_defs_ptr[svc_idx];
        svc_def.c_svc_def_ptr = static_cast<void*>(current_c_svc); // Store opaque C pointer
        current_c_svc->type = BLE_GATT_SVC_TYPE_PRIMARY;
        current_c_svc->uuid = alloc_string_to_uuid(svc_def.uuid_str);
        svc_def.c_svc_uuid_ptr = (void*)current_c_svc->uuid; // Store allocated UUID pointer
        if (!current_c_svc->uuid) { ESP_LOGE(TAG,"Svc UUID alloc failed: %s", svc_def.uuid_str.c_str()); _FreeGATTDefinitions(); return false; }
        current_c_svc->characteristics = &chr_defs_ptr[chr_idx_offset];

        int current_svc_chr_count = 0;
        for (auto& char_def : svc_def.characteristics) { // Need non-const ref
            ble_gatt_chr_def* current_c_chr = &chr_defs_ptr[chr_idx_offset + current_svc_chr_count];
            char_def.c_chr_def_ptr = static_cast<void*>(current_c_chr); // Store opaque C pointer
            current_c_chr->uuid = alloc_string_to_uuid(char_def.uuid_str);
            if (!current_c_chr->uuid) { ESP_LOGE(TAG,"Char UUID alloc failed: %s", char_def.uuid_str.c_str()); _FreeGATTDefinitions(); return false; }
            current_c_chr->access_cb = gatt_svr_access_cb;
            current_c_chr->arg = &char_def; // Link C callback arg to C++ definition struct
            current_c_chr->flags = char_def.properties;
            current_c_chr->min_key_size = 0;
            current_c_chr->val_handle = &char_def.attr_handle; // Give NimBLE address to store handle
            current_svc_chr_count++;
        }
        chr_defs_ptr[chr_idx_offset + current_svc_chr_count].uuid = NULL; // Null terminate char list
        chr_idx_offset += (current_svc_chr_count + 1);
        svc_idx++;
    }
    svc_defs_ptr[svc_idx].type = 0; // Null terminate service list

    // --- Registration ---
    // Pass cast pointers to NimBLE functions
    int rc = ble_gatts_count_cfg(static_cast<ble_gatt_svc_def*>(s_gatt_svc_defs_));
    if (rc != 0) { ESP_LOGE(TAG,"ble_gatts_count_cfg failed: %d", rc); _FreeGATTDefinitions(); return false; }
    rc = ble_gatts_add_svcs(static_cast<ble_gatt_svc_def*>(s_gatt_svc_defs_));
    if (rc != 0) { ESP_LOGE(TAG,"ble_gatts_add_svcs failed: %d", rc); _FreeGATTDefinitions(); return false; }

    ESP_LOGI(TAG,"GATT database successfully registered with NimBLE stack.");
    s_gatt_registered_ = true;
    // Do NOT free UUIDs here, they are freed later
    return true;
}


// Set Characteristic Value
bool BLEManager::SetCharacteristicValue(const std::string& char_uuid, const void* value, size_t size, bool notify) {
    if (!lock_mutex()) return false;
    if (!s_initialized_) { ESP_LOGE(TAG, "SetVal: Not initialized."); unlock_mutex(); return false; }

    CharacteristicDefinition* target_char_def = nullptr;
    for (auto& svc_def : s_service_definitions_) {
        auto it = std::find_if(svc_def.characteristics.begin(), svc_def.characteristics.end(),
                               [&](const CharacteristicDefinition& cd) { return cd.uuid_str == char_uuid; });
        if (it != svc_def.characteristics.end()) { target_char_def = &(*it); break; }
    }
    if (!target_char_def) { ESP_LOGE(TAG,"SetVal: Char %s not defined.", char_uuid.c_str()); unlock_mutex(); return false; }

    // Update cache
    target_char_def->value_cache.assign(static_cast<const uint8_t*>(value), static_cast<const uint8_t*>(value) + size);
    ESP_LOGD(TAG,"SetVal: Updated cache for %s (size %d)", char_uuid.c_str(), size);

    // Notify if needed
    if (notify && (target_char_def->properties & CharProperty::NOTIFY)) {
        if (s_state_ == BLEState::CONNECTED && s_conn_handle_ != BLE_HS_CONN_HANDLE_NONE) {
            if (target_char_def->attr_handle != 0) {
                 int rc = ble_gatts_notify(s_conn_handle_, target_char_def->attr_handle);
                 if (rc == 0) { ESP_LOGD(TAG,"Sent notify for char %s (handle %d)", char_uuid.c_str(), target_char_def->attr_handle); }
                 else { ESP_LOGE(TAG,"ble_gatts_notify failed for %s: rc=%d (0x%X)", char_uuid.c_str(), rc, rc); }
            } else { ESP_LOGW(TAG,"Cannot notify for %s: Attr handle unknown.", char_uuid.c_str()); }
        } else { ESP_LOGW(TAG,"Cannot notify for %s: Not connected.", char_uuid.c_str()); }
    }
    unlock_mutex();
    return true;
}


// Disconnect
void BLEManager::Disconnect() {
    if (!lock_mutex()) return;
    if (!s_initialized_) { unlock_mutex(); return; }
    if (s_state_ == BLEState::CONNECTED && s_conn_handle_ != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG,"Requesting disconnection from handle %d", s_conn_handle_);
        int rc = ble_gap_terminate(s_conn_handle_, BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0 && rc != BLE_HS_EALREADY) { ESP_LOGE(TAG,"ble_gap_terminate failed: rc=%d", rc); }
    } else { ESP_LOGW(TAG,"Disconnect called but not connected."); }
    unlock_mutex();
}

// Set Callbacks
void BLEManager::SetConnectionCallback(ConnectionCallback callback) {
    if (!lock_mutex()) return;
    s_on_connect_cb_ = std::move(callback);
    unlock_mutex();
}
void BLEManager::SetDisconnectionCallback(DisconnectionCallback callback) {
    if (!lock_mutex()) return;
    s_on_disconnect_cb_ = std::move(callback);
    unlock_mutex();
}

// Start Advertising
bool BLEManager::StartAdvertising(bool connectable_enable, uint16_t adv_interval_ms) {
    if (!lock_mutex()) return false;
    if (!s_initialized_) { ESP_LOGE(TAG,"StartAdv: Not initialized."); unlock_mutex(); return false; }
    if (s_state_ == BLEState::ADVERTISING) { ESP_LOGW(TAG,"Already advertising."); unlock_mutex(); return true; }
    if (s_state_ == BLEState::CONNECTED) { ESP_LOGW(TAG,"Cannot start advertising while connected."); unlock_mutex(); return false; }

    // Register GATT DB if not already done
    if (!s_gatt_registered_) {
        ESP_LOGI(TAG,"Registering GATT database before advertising...");
        if (!_RegisterGATTDatabase()) { ESP_LOGE(TAG,"Failed to register GATT DB."); unlock_mutex(); return false; }
    }

    // Configure Adv Data
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    // TODO: Add optional service UUIDs / TX power etc. if needed
    fields.name = (uint8_t*)s_device_name_.c_str();
    fields.name_len = std::min((int)s_device_name_.length(), BLE_HS_ADV_MAX_SZ); // Use NimBLE defined max size
    fields.name_is_complete = (fields.name_len == s_device_name_.length());
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) { ESP_LOGE(TAG,"ble_gap_adv_set_fields failed: rc=%d (0x%X)", rc, rc); unlock_mutex(); return false; }

    // Configure Adv Params
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = connectable_enable ? BLE_GAP_CONN_MODE_UND : BLE_GAP_CONN_MODE_NON;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    uint32_t interval_units = (adv_interval_ms * 1000) / 625;
    adv_params.itvl_min = std::max((uint32_t)0x0020, interval_units); // Min 20ms (0x20 * 0.625ms)
    adv_params.itvl_max = std::max((uint32_t)0x0020, interval_units) + 32; // Add 20ms range (32 * 0.625ms)

    // Start Advertising
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, gap_event_cb, NULL);
    if (rc != 0) { ESP_LOGE(TAG,"ble_gap_adv_start failed: rc=%d (0x%X)", rc, rc); unlock_mutex(); return false; }

    s_state_ = BLEState::ADVERTISING;
    ESP_LOGI(TAG,"Started advertising%s.", connectable_enable ? " (connectable)" : " (non-connectable)");
    unlock_mutex();
    return true;
}

// Stop Advertising
void BLEManager::StopAdvertising() {
     if (!lock_mutex()) return;
     if (!s_initialized_) { unlock_mutex(); return; }
     if (s_state_ == BLEState::ADVERTISING) {
         int rc = ble_gap_adv_stop();
         if (rc == 0 || rc == BLE_HS_EALREADY) { ESP_LOGI(TAG,"Stopped advertising."); }
         else { ESP_LOGE(TAG,"ble_gap_adv_stop failed: rc=%d", rc); }
     } else { ESP_LOGW(TAG,"StopAdvertising called, but not advertising."); }
     unlock_mutex();
}

// Get State
BLEManager::BLEState BLEManager::GetState() {
    if (!lock_mutex()) return BLEState::IDLE; // Default on mutex error
    BLEState current_state = s_state_;
    unlock_mutex();
    return current_state;
}

} // namespace connectivity
} // namespace platform

----------------------------------------------------------------------------------------------------

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_bt.h"              // For esp_bt_controller_init etc.
#include "esp_nimble_hci.h"      // For esp_nimble_hci_and_controller_init
#include "nimble/nimble_port.h"  // For nimble_port_init/deinit if needed explicitly
#include "ble_manager.h"       // Include your header

static const char* TAG_MAIN = "AppMain";

// Define UUIDs (replace with your actual UUIDs)
#define SERVICE_UUID        "180A" // Device Information Service (Example)
#define CHAR_MANUFACTURER   "2A29" // Manufacturer Name String (Example)
#define CHAR_SERIAL_NUMBER  "2A25" // Serial Number String (Example)
#define CHAR_CUSTOM_WRITE   "DEAD" // Custom Writable Characteristic (Example)
#define CHAR_NOTIFY         "BEEF" // Custom Notify Characteristic (Example)


using platform::connectivity::BLEManager;

// Example callback function for the custom writable characteristic
void handle_custom_write(const std::string& uuid, const void* data, size_t size) {
    ESP_LOGI(TAG_MAIN, "Received write for Char %s, %d bytes", uuid.c_str(), size);
    // Print received data as hex for example
    printf("  Data: ");
    const uint8_t* byte_data = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        printf("%02X ", byte_data[i]);
    }
    printf("\n");

    // Example: Echo back modified data to the Serial Number characteristic
    std::string response = "RX:";
    response.append((const char*)data, size);
    BLEManager::GetInstance().SetCharacteristicValue(CHAR_SERIAL_NUMBER, response.c_str(), response.length());

    // Example: Send a notification on the NOTIFY characteristic
    std::string notify_msg = "Wrote ";
    notify_msg += std::to_string(size);
    notify_msg += "b";
    BLEManager::GetInstance().SetCharacteristicValue(CHAR_NOTIFY, notify_msg.c_str(), notify_msg.length(), true); // Notify=true

}

// Example connection callback
void on_connect() {
    ESP_LOGI(TAG_MAIN, "********* CLIENT CONNECTED *********");
}

// Example disconnection callback
void on_disconnect() {
    ESP_LOGW(TAG_MAIN, "********* CLIENT DISCONNECTED *********");
    // Example: Restart advertising after disconnection
    ESP_LOGI(TAG_MAIN, "Restarting advertising...");
    // It's often better to let the main loop handle restarts or use a dedicated task state machine.
    // Direct call from callback might have timing issues.
    // For simplicity here:
    // vTaskDelay(pdMS_TO_TICKS(500)); // Short delay before restarting
    // BLEManager::GetInstance().StartAdvertising();
}


extern "C" void app_main(void)
{
    esp_err_t ret;

    // --- 1. Initialize NVS ---
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // --- 2. Initialize Bluetooth Controller ---
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) { ESP_LOGE(TAG_MAIN,"BT controller init failed: %s", esp_err_to_name(ret)); return; }
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) { ESP_LOGE(TAG_MAIN,"BT controller enable failed: %s", esp_err_to_name(ret)); return; }

    // --- 3. Initialize NimBLE Host Stack ---
    ESP_LOGI(TAG_MAIN, "Initializing NimBLE stack...");
    ret = esp_nimble_hci_and_controller_init();
     if (ret != ESP_OK) { ESP_LOGE(TAG_MAIN,"NimBLE init failed: %s", esp_err_to_name(ret)); return; }

    // --- 4. Initialize BLE Manager (Singleton) ---
    BLEManager& bleMgr = BLEManager::GetInstance();
    if (!bleMgr.Init("MyNimbleDevice")) { ESP_LOGE(TAG_MAIN, "Failed to init BLE Manager"); return; }

    // --- 5. Register Services and Characteristics ---
    std::vector<std::tuple<std::string, uint8_t, BLEManager::CharWriteCallback>> service1_chars;

    uint8_t readProps = (uint8_t)BLEManager::CharProperty::READ;
    uint8_t writeProps = (uint8_t)BLEManager::CharProperty::WRITE;
    uint8_t notifyProps = (uint8_t)BLEManager::CharProperty::NOTIFY | (uint8_t)BLEManager::CharProperty::READ; // Notify + Read

    // Characteristic 1: Manufacturer Name (Read Only)
    service1_chars.emplace_back(CHAR_MANUFACTURER, readProps, nullptr);
    // Characteristic 2: Serial Number (Read Only - value set later)
    service1_chars.emplace_back(CHAR_SERIAL_NUMBER, readProps, nullptr);
    // Characteristic 3: Custom Writable (Write Only with callback)
    service1_chars.emplace_back(CHAR_CUSTOM_WRITE, writeProps, handle_custom_write);
     // Characteristic 4: Custom Notify (Read + Notify)
    service1_chars.emplace_back(CHAR_NOTIFY, notifyProps, nullptr); // No write callback needed


    if (!bleMgr.RegisterService(SERVICE_UUID, service1_chars)) {
         ESP_LOGE(TAG_MAIN, "Failed to register service %s", SERVICE_UUID);
    }

    // --- 6. Set Initial Characteristic Values ---
    std::string manufacturer = "MyCompany";
    std::string serial = "SN-00001";
    std::string notify_init = "Initial";
    bleMgr.SetCharacteristicValue(CHAR_MANUFACTURER, manufacturer.c_str(), manufacturer.length());
    bleMgr.SetCharacteristicValue(CHAR_SERIAL_NUMBER, serial.c_str(), serial.length());
    bleMgr.SetCharacteristicValue(CHAR_NOTIFY, notify_init.c_str(), notify_init.length());


    // --- 7. Set Connection/Disconnection Callbacks ---
    bleMgr.SetConnectionCallback(on_connect);
    bleMgr.SetDisconnectionCallback(on_disconnect);

    // --- 8. Start Advertising ---
    ESP_LOGI(TAG_MAIN, "Starting BLE Advertising...");
    if (!bleMgr.StartAdvertising()) {
        ESP_LOGE(TAG_MAIN, "Failed to start advertising");
    } else {
        ESP_LOGI(TAG_MAIN, "Advertising started successfully.");
    }

    // --- Application Loop (Keep main task alive) ---
    ESP_LOGI(TAG_MAIN, "Application setup complete. Entering idle loop.");
    uint32_t counter = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000)); // Delay 10 seconds

        // Periodically update the notify characteristic if connected
        if (bleMgr.GetState() == BLEManager::BLEState::CONNECTED) {
           std::string uptime_str = "Tick:" + std::to_string(counter++);
           ESP_LOGI(TAG_MAIN, "Updating notify characteristic: %s", uptime_str.c_str());
           bleMgr.SetCharacteristicValue(CHAR_NOTIFY, uptime_str.c_str(), uptime_str.length(), true); // Update & Notify
        } else if (bleMgr.GetState() == BLEManager::BLEState::DISCONNECTED) {
            // If disconnected, maybe restart advertising (if not done in callback)
            ESP_LOGI(TAG_MAIN, "Device disconnected, ensuring advertising is running.");
             bleMgr.StartAdvertising(); // Safe to call even if already advertising
        }
    }
}

#pragma once

#include <cstdint> // For standard integer types

// --- ESP-IDF / FreeRTOS Specific Includes ---
// Only include headers needed for COMMON definitions accessible by users.
// FreeRTOS base types might be needed for constants like portMAX_DELAY.
// If we want to fully hide FreeRTOS, define constants numerically.
// Let's define them numerically to avoid FreeRTOS includes here.
// #include "freertos/FreeRTOS.h" // Avoid if possible in common header
// --- End Specific Includes ---


namespace OSAL {

/**
 * @brief Represents an infinite timeout duration.
 */
constexpr uint32_t kWaitForever = 0xFFFFFFFF; // UINT32_MAX

/**
 * @brief Represents a zero timeout (non-blocking).
 */
constexpr uint32_t kNoWait = 0;

/**
 * @brief Status codes returned by OSAL functions.
 */
enum class OSALStatus {
    kSuccess = 0,                ///< Operation completed successfully.
    kErrorGeneral = -1,          ///< An unspecified error occurred.
    kErrorTimeout = -2,          ///< Operation timed out.
    kErrorInvalidParameter = -3, ///< An invalid parameter was provided.
    kErrorNoMemory = -4,         ///< Not enough memory to complete the operation.
    kErrorNotFound = -5,         ///< Resource (e.g., handle) not found or invalid.
    kErrorBusy = -6,             ///< Resource is busy (e.g., mutex try_lock failed).
    kErrorQueueFull = -7,        ///< The queue is full.
    kErrorQueueEmpty = -8,       ///< The queue is empty.
    kErrorNotSupported = -9,     ///< The operation is not supported.
    kErrorInvalidState = -10,    ///< Operation called in an invalid state (e.g., join on unstarted thread).
};

/**
 * @brief Defines abstract types for heap memory regions.
 *
 * These types are mapped to platform-specific capabilities (like ESP-IDF's
 * MALLOC_CAP flags) in the implementation.
 */
enum class HeapType {
    kDefault,     ///< General purpose memory, OS/platform default (e.g., DRAM).
    kInternalFast,///< Prefer internal, fast memory (e.g., SRAM).
    kDma,         ///< Memory suitable for Hardware DMA engines.
    kExternalRam, ///< Prefer external RAM if available (e.g., PSRAM).
    kIRamExec     ///< Instruction RAM suitable for placing executable code.
};

/**
 * @brief Converts milliseconds to RTOS ticks.
 *
 * @warning This function's implementation is platform-specific (found in the
 * corresponding .cpp file) and relies on the underlying RTOS tick rate
 * (e.g., `pdMS_TO_TICKS` for FreeRTOS).
 *
 * @param ms Duration in milliseconds. Handles `kWaitForever` and `kNoWait`.
 * @return uint32_t Equivalent duration in RTOS ticks.
 */
uint32_t MsToTicks(uint32_t ms);

} // namespace OSAL

--------------------------------------------------------------------------------------------------------------

  #include "osal_common.hpp"

// --- ESP-IDF / FreeRTOS Specific Includes ---
#include "freertos/FreeRTOS.h" // Needed for portMAX_DELAY and pdMS_TO_TICKS
// --- End Specific Includes ---


namespace OSAL {

// Platform-specific implementation (ESP-IDF/FreeRTOS)
uint32_t MsToTicks(uint32_t ms) {
    if (ms == kWaitForever) {
        return portMAX_DELAY;
    }
    if (ms == kNoWait) {
        return 0;
    }
    // pdMS_TO_TICKS handles potential overflow and rounds up appropriately.
    // Ensure this macro is available from the included FreeRTOS headers.
    return pdMS_TO_TICKS(ms);
}

} // namespace OSAL

--------------------------------------------------------------------------------------------------------------
//osal_mutex.h
  #pragma once

#include "osal_common.hpp" // For OSALStatus, timeouts
#include <cstdint>         // For uint32_t

// Forward declaration
struct MutexImpl;

namespace OSAL {

/**
 * @brief Provides a mutual exclusion (Mutex) synchronization primitive.
 *
 * Wraps the underlying RTOS mutex mechanism. Ensures that only one thread
 * can own the mutex at any given time. Uses RAII via LockGuard for safer usage.
 */
class Mutex {
public:
    /**
     * @brief Constructs a Mutex object and creates the underlying RTOS mutex.
     */
    Mutex();

    /**
     * @brief Destroys the Mutex object and releases the underlying RTOS mutex.
     */
    ~Mutex();

    /**
     * @brief Acquires the mutex, blocking indefinitely if necessary.
     * @return OSALStatus::kSuccess on success, or an error code.
     */
    OSALStatus Lock();

    /**
     * @brief Acquires the mutex, blocking up to a specified timeout.
     * @param timeout_ms Maximum time to wait in milliseconds.
     * @return OSALStatus::kSuccess if acquired, OSALStatus::kErrorTimeout if
     * timeout occurred, or another error code.
     */
    OSALStatus Lock(uint32_t timeout_ms);

    /**
     * @brief Attempts to acquire the mutex without blocking.
     * @return OSALStatus::kSuccess if acquired immediately, OSALStatus::kErrorBusy
     * if already locked, or another error code.
     */
    OSALStatus TryLock();

    /**
     * @brief Releases the mutex.
     * @warning Must only be called by the thread that currently holds the lock.
     * @return OSALStatus::kSuccess on success, or an error code (e.g., if not owned).
     */
    OSALStatus Unlock();

    // --- Deleted Methods ---
    // Mutexes represent unique resources and shouldn't be copied or moved.
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex(Mutex&&) = delete;
    Mutex& operator=(Mutex&&) = delete;

private:
    MutexImpl* impl_ = nullptr; ///< Opaque pointer to implementation details.

    // Friend declaration to allow LockGuard access to Lock/Unlock if needed
    // directly, though usually LockGuard calls public methods.
    friend class LockGuard;
};


/**
 * @brief Provides a scoped lock (RAII) for an OSAL::Mutex.
 *
 * Acquires the mutex upon construction and automatically releases it
 * upon destruction (when the LockGuard object goes out of scope).
 * This helps prevent accidental failures to unlock mutexes.
 */
class LockGuard {
public:
    /**
     * @brief Constructs a LockGuard, attempting to acquire the given mutex.
     * Blocks indefinitely until the mutex is acquired.
     * @param mutex The OSAL::Mutex to lock.
     * @note Consider adding a constructor variant with timeout or checking status.
     */
    explicit LockGuard(Mutex& mutex);

    /**
     * @brief Destroys the LockGuard, automatically releasing the mutex.
     */
    ~LockGuard();

    // --- Deleted Methods ---
    // Scoped locks manage a specific lock instance and shouldn't be copied/moved.
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
    LockGuard(LockGuard&&) = delete;
    LockGuard& operator=(LockGuard&&) = delete;

private:
    Mutex& mutex_ref_;     ///< Reference to the mutex being managed.
    bool acquired_ = false; ///< Flag indicating if the lock was acquired.
};

} // namespace OSAL


//osal_mutex.cc
#include "osal_mutex.hpp"

// --- ESP-IDF / FreeRTOS Specific Includes ---
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h" // Mutexes are implemented using semaphores
#include "esp_log.h"
// --- End Specific Includes ---

#include <new> // For std::nothrow

namespace {
const char* kLogTag = "OSAL_Mutex";
} // namespace

/**
 * @brief Structure holding the implementation details for OSAL::Mutex.
 */
struct MutexImpl {
    SemaphoreHandle_t handle_ = nullptr; ///< FreeRTOS semaphore handle used as mutex.
};

namespace OSAL {

// --- Mutex Implementation ---

Mutex::Mutex() : impl_(new (std::nothrow) MutexImpl) {
    if (!impl_) {
        ESP_LOGE(kLogTag, "Failed to allocate memory for MutexImpl");
        return;
    }
    impl_->handle_ = xSemaphoreCreateMutex();
    if (!impl_->handle_) {
        ESP_LOGE(kLogTag, "Failed to create FreeRTOS mutex semaphore.");
        delete impl_;
        impl_ = nullptr;
    } else {
        ESP_LOGD(kLogTag, "Mutex created.");
    }
}

Mutex::~Mutex() {
    if (impl_) {
        if (impl_->handle_) {
            vSemaphoreDelete(impl_->handle_);
            ESP_LOGD(kLogTag, "Mutex deleted.");
        }
        delete impl_;
    }
}

OSALStatus Mutex::Lock() {
    return Lock(kWaitForever); // Delegate to timed lock with infinite wait
}

OSALStatus Mutex::Lock(uint32_t timeout_ms) {
    if (!impl_ || !impl_->handle_) {
        return OSALStatus::kErrorNotFound;
    }

    TickType_t ticks_to_wait = MsToTicks(timeout_ms);
    if (xSemaphoreTake(impl_->handle_, ticks_to_wait) == pdTRUE) {
        return OSALStatus::kSuccess;
    } else {
        // If timeout was 0, it means TryLock failed because it was busy
        if (timeout_ms == kNoWait) {
             return OSALStatus::kErrorBusy;
        }
        return OSALStatus::kErrorTimeout;
    }
}

OSALStatus Mutex::TryLock() {
    return Lock(kNoWait); // Delegate to timed lock with zero wait
}

OSALStatus Mutex::Unlock() {
    if (!impl_ || !impl_->handle_) {
        return OSALStatus::kErrorNotFound;
    }

    // xSemaphoreGive should only be called by the owner. FreeRTOS mutexes
    // usually track ownership (if configUSE_MUTEXES is 1).
    if (xSemaphoreGive(impl_->handle_) == pdTRUE) {
        return OSALStatus::kSuccess;
    } else {
        // Failure typically means the calling task didn't own the mutex.
        ESP_LOGE(kLogTag, "Unlock failed - likely called by non-owner thread.");
        return OSALStatus::kErrorGeneral; // Or a more specific "not owner" error
    }
}


// --- LockGuard Implementation ---

LockGuard::LockGuard(Mutex& mutex) : mutex_ref_(mutex), acquired_(false) {
    // Attempt to lock indefinitely. Handle potential errors if needed.
    if (mutex_ref_.Lock() == OSALStatus::kSuccess) {
        acquired_ = true;
    } else {
        // Failed to acquire the lock - this shouldn't happen with infinite
        // timeout unless the mutex is invalid or deleted concurrently.
        ESP_LOGE(kLogTag, "LockGuard failed to acquire mutex indefinitely!");
        // Consider throwing an exception or entering a fatal error state.
    }
}

LockGuard::~LockGuard() {
    if (acquired_) {
        // Ignore return status of Unlock? Usually desired in RAII destructors.
        // Log if Unlock fails?
        OSALStatus status = mutex_ref_.Unlock();
        if (status != OSALStatus::kSuccess) {
             ESP_LOGW(kLogTag, "LockGuard failed to unlock mutex in destructor (Status: %d)", static_cast<int>(status));
        }
    }
}

} // namespace OSAL
---------------------------------------------------------------------------------------------------------------------------------

  //osal_semaphore
  #pragma once

#include "osal_common.hpp" // For OSALStatus, timeouts
#include <cstdint>         // For uint32_t

// Forward declaration
struct SemaphoreImpl;

namespace OSAL {

/**
 * @brief Provides a counting or binary semaphore synchronization primitive.
 *
 * Wraps the underlying RTOS semaphore. Semaphores manage access to a fixed
 * number of shared resources or can be used for signaling between tasks/ISRs.
 */
class Semaphore {
public:
    /**
     * @brief Constructs a Semaphore object.
     *
     * Creates either a binary semaphore (if max_count is 1) or a counting
     * semaphore.
     *
     * @param max_count The maximum count for a counting semaphore, or 1 for
     * binary.
     * @param initial_count The initial count value (must be <= max_count).
     * For binary semaphores, 0 means initially taken, 1 means initially given.
     */
    Semaphore(uint32_t max_count = 1, uint32_t initial_count = 0);

    /**
     * @brief Destroys the Semaphore object and releases the underlying RTOS semaphore.
     */
    ~Semaphore();

    /**
     * @brief Acquires (takes or waits for) the semaphore.
     *
     * Decrements the semaphore count. If the count is zero, the caller blocks
     * until the semaphore is given or the timeout expires.
     *
     * @param timeout_ms Maximum time to wait in milliseconds. Use `kWaitForever`
     * to wait indefinitely.
     * @return OSALStatus::kSuccess if acquired, OSALStatus::kErrorTimeout if
     * timeout occurred, or another error code.
     */
    OSALStatus Take(uint32_t timeout_ms = kWaitForever);

    /**
     * @brief Attempts to acquire the semaphore without blocking.
     * @return OSALStatus::kSuccess if acquired immediately, OSALStatus::kErrorBusy
     * if semaphore count was zero, or another error code.
     */
    OSALStatus TryTake();

    /**
     * @brief Releases (gives or signals) the semaphore.
     *
     * Increments the semaphore count. If tasks are blocked waiting for the
     * semaphore, one task will be unblocked. Can be called from an ISR.
     *
     * @return OSALStatus::kSuccess on success, or an error code (e.g., count
     * would exceed max_count for counting semaphores).
     */
    OSALStatus Give();

    /**
     * @brief Gets the current count of the semaphore.
     * @note Primarily useful for counting semaphores. For binary semaphores,
     * this typically returns 0 or 1.
     * @return The current semaphore count, or 0 if the semaphore is invalid.
     */
    uint32_t GetCount() const;


    // --- Deleted Methods ---
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;
    Semaphore(Semaphore&&) = delete;
    Semaphore& operator=(Semaphore&&) = delete;

private:
    SemaphoreImpl* impl_ = nullptr; ///< Opaque pointer to implementation details.
};

} // namespace OSAL

//osal_semaphore.cc
#include "osal_semaphore.hpp"

// --- ESP-IDF / FreeRTOS Specific Includes ---
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/portable.h" // For xPortInIsrContext (might be included by others)
#include "esp_log.h"
// --- End Specific Includes ---

#include <new> // For std::nothrow

namespace {
const char* kLogTag = "OSAL_Semaphore";
} // namespace

/**
 * @brief Structure holding the implementation details for OSAL::Semaphore.
 */
struct SemaphoreImpl {
    SemaphoreHandle_t handle_ = nullptr; ///< FreeRTOS semaphore handle.
};

namespace OSAL {

Semaphore::Semaphore(uint32_t max_count, uint32_t initial_count)
    : impl_(new (std::nothrow) SemaphoreImpl)
{
    if (!impl_) {
        ESP_LOGE(kLogTag, "Failed to allocate memory for SemaphoreImpl");
        return;
    }

    if (max_count == 0) {
         ESP_LOGE(kLogTag, "Semaphore max_count cannot be zero.");
         delete impl_;
         impl_ = nullptr;
         return;
    }
    if (initial_count > max_count) {
         ESP_LOGW(kLogTag, "Semaphore initial_count (%u) > max_count (%u). Clamping.", initial_count, max_count);
         initial_count = max_count;
    }

    if (max_count == 1) {
        // Create a binary semaphore
        impl_->handle_ = xSemaphoreCreateBinary();
        if (impl_->handle_ && initial_count == 1) {
            // Binary semaphores are created 'taken'. Give it if initial count is 1.
            xSemaphoreGive(impl_->handle_);
        }
    } else {
        // Create a counting semaphore
        impl_->handle_ = xSemaphoreCreateCounting(max_count, initial_count);
    }

    if (!impl_->handle_) {
        ESP_LOGE(kLogTag, "Failed to create FreeRTOS semaphore.");
        delete impl_;
        impl_ = nullptr;
    } else {
        ESP_LOGD(kLogTag, "Semaphore created (max=%u, initial=%u).", max_count, initial_count);
    }
}

Semaphore::~Semaphore() {
    if (impl_) {
        if (impl_->handle_) {
            vSemaphoreDelete(impl_->handle_);
             ESP_LOGD(kLogTag, "Semaphore deleted.");
        }
        delete impl_;
    }
}

OSALStatus Semaphore::Take(uint32_t timeout_ms) {
    if (!impl_ || !impl_->handle_) {
        return OSALStatus::kErrorNotFound;
    }

    // Cannot wait from an ISR
    if (xPortInIsrContext() && timeout_ms != kNoWait) {
         ESP_LOGE(kLogTag, "Cannot Take() with blocking timeout from ISR context.");
         return OSALStatus::kErrorInvalidContext; // Need to add this status code
    }

    TickType_t ticks_to_wait = MsToTicks(timeout_ms);
    if (xSemaphoreTake(impl_->handle_, ticks_to_wait) == pdTRUE) {
        return OSALStatus::kSuccess;
    } else {
         if (timeout_ms == kNoWait) {
             return OSALStatus::kErrorBusy; // Indicate it wasn't available immediately
         }
        return OSALStatus::kErrorTimeout;
    }
}

OSALStatus Semaphore::TryTake() {
    return Take(kNoWait);
}

OSALStatus Semaphore::Give() {
    if (!impl_ || !impl_->handle_) {
        return OSALStatus::kErrorNotFound;
    }

    BaseType_t result;
    if (xPortInIsrContext()) {
        BaseType_t task_woken = pdFALSE;
        result = xSemaphoreGiveFromISR(impl_->handle_, &task_woken);
        // Request a context switch if a higher priority task was woken.
        // FreeRTOS on ESP-IDF handles this via portYIELD_FROM_ISR internally in most cases.
        // Check ESP-IDF docs if specific portYIELD needed. Usually not.
        // if (task_woken == pdTRUE) { portYIELD_FROM_ISR(); } // Example if needed
    } else {
        result = xSemaphoreGive(impl_->handle_);
    }

    if (result == pdTRUE) {
        return OSALStatus::kSuccess;
    } else {
        // Failure usually means counting semaphore is already at max count.
        ESP_LOGW(kLogTag, "Give failed - likely semaphore at max count.");
        return OSALStatus::kErrorGeneral; // Or kErrorSemaphoreFull ?
    }
}

uint32_t Semaphore::GetCount() const {
    if (!impl_ || !impl_->handle_) {
        return 0;
    }
    // uxSemaphoreGetCount is safe to call from ISRs or tasks.
    return static_cast<uint32_t>(uxSemaphoreGetCount(impl_->handle_));
}

} // namespace OSAL


--------------------------------------------------------------------------------------------------
  //osal_queue

  #pragma once

#include "osal_common.hpp" // For OSALStatus, timeouts
#include <cstdint>         // For uint32_t
#include <cstddef>         // For size_t
#include <type_traits>     // For static_assert, is_trivially_copyable

// Forward declaration
template <typename T> struct QueueImpl;

namespace OSAL {

/**
 * @brief Provides a thread-safe message queue (FIFO).
 *
 * Wraps the underlying RTOS queue mechanism. Allows passing data items of a
 * specific type `T` between tasks and potentially between tasks and ISRs.
 *
 * @tparam T The type of data items stored in the queue. Must be trivially
 * copyable (e.g., basic types, structs/classes without complex constructors,
 * pointers).
 */
template <typename T>
class Queue {
    // Ensure T is suitable for direct memory copying by FreeRTOS queue functions.
    static_assert(std::is_trivially_copyable<T>::value,
                  "Queue item type T must be trivially copyable.");
public:
    /**
     * @brief Constructs a Queue object.
     * @param max_items The maximum number of items the queue can hold.
     */
    explicit Queue(size_t max_items);

    /**
     * @brief Destroys the Queue object and releases the underlying RTOS queue.
     */
    ~Queue();

    /**
     * @brief Sends (posts) an item to the back of the queue.
     *
     * Copies the item into the queue. Blocks if the queue is full, up to the
     * specified timeout. Can be called from an ISR if timeout_ms is kNoWait.
     *
     * @param item The item to send (passed by const reference, then copied).
     * @param timeout_ms Maximum time to wait in milliseconds if the queue is full.
     * Use `kWaitForever` to wait indefinitely. Use `kNoWait` for non-blocking
     * (required if calling from ISR).
     * @return
     * - `OSALStatus::kSuccess` if the item was successfully sent.
     * - `OSALStatus::kErrorQueueFull` if the queue was full and timeout was `kNoWait`.
     * - `OSALStatus::kErrorTimeout` if timeout occurred while waiting.
     * - Other error codes for invalid state or parameters.
     */
    OSALStatus Send(const T& item, uint32_t timeout_ms = kWaitForever);

    /**
     * @brief Sends (posts) an item to the front of the queue (higher priority).
     *
     * Copies the item into the queue. Blocks if the queue is full, up to the
     * specified timeout. Can be called from an ISR if timeout_ms is kNoWait.
     *
     * @param item The item to send urgently (passed by const reference, then copied).
     * @param timeout_ms Maximum time to wait in milliseconds if the queue is full.
     * Use `kWaitForever` to wait indefinitely. Use `kNoWait` for non-blocking
     * (required if calling from ISR).
     * @return OSALStatus status code, similar to Send().
     */
    OSALStatus SendUrgent(const T& item, uint32_t timeout_ms = kWaitForever);


    /**
     * @brief Receives (retrieves) an item from the front of the queue.
     *
     * Copies the item from the queue into the provided reference `item_out`.
     * Blocks if the queue is empty, up to the specified timeout.
     * Cannot be called from an ISR with a non-zero timeout.
     *
     * @param item_out Reference to store the received item.
     * @param timeout_ms Maximum time to wait in milliseconds if the queue is empty.
     * Use `kWaitForever` to wait indefinitely. Use `kNoWait` for non-blocking.
     * @return
     * - `OSALStatus::kSuccess` if an item was successfully received.
     * - `OSALStatus::kErrorQueueEmpty` if the queue was empty and timeout was `kNoWait`.
     * - `OSALStatus::kErrorTimeout` if timeout occurred while waiting.
     * - Other error codes for invalid state or parameters.
     */
    OSALStatus Receive(T& item_out, uint32_t timeout_ms = kWaitForever);

    /**
     * @brief Peeks at the item at the front of the queue without removing it.
     *
     * Copies the item from the queue into the provided reference `item_out`.
     * Blocks if the queue is empty, up to the specified timeout.
     * Less commonly used from ISRs.
     *
     * @param item_out Reference to store the peeked item.
     * @param timeout_ms Maximum time to wait in milliseconds if the queue is empty.
     * @return OSALStatus status code, similar to Receive().
     */
    OSALStatus Peek(T& item_out, uint32_t timeout_ms = kWaitForever);

    /**
     * @brief Gets the number of items currently in the queue.
     * @return Number of items waiting, or 0 if invalid.
     */
    size_t GetCount() const;

    /**
     * @brief Gets the number of empty slots remaining in the queue.
     * @return Number of available slots, or 0 if invalid.
     */
    size_t GetSpace() const;

    /**
     * @brief Checks if the queue is currently full.
     * @return true if full, false otherwise.
     */
    bool IsFull() const;

    /**
     * @brief Checks if the queue is currently empty.
     * @return true if empty, false otherwise.
     */
    bool IsEmpty() const;

    /**
     * @brief Resets the queue to an empty state.
     * Any items currently in the queue are discarded.
     * @return OSALStatus::kSuccess on success.
     */
    OSALStatus Reset();


    // --- Deleted Methods ---
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    Queue(Queue&&) = delete;
    Queue& operator=(Queue&&) = delete;

private:
    QueueImpl<T>* impl_ = nullptr; ///< Opaque pointer to implementation details.
};

} // namespace OSAL

// Include the implementation for the template class.
// This is a common pattern for template classes using Pimpl, although it slightly
// breaks the perfect header/source separation. Alternatively, explicitly
// instantiate templates needed in a .cpp file. Keeping impl in header for simplicity.
#include "osal_queue_impl.hpp"


// osal_queue_impl.h
#pragma once

// --- ESP-IDF / FreeRTOS Specific Includes ---
// Included here because this file is included by osal_queue.hpp
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/portable.h" // For xPortInIsrContext
#include "esp_log.h"
// --- End Specific Includes ---

#include <new> // For std::nothrow

// Still within the OSAL namespace
namespace OSAL {

namespace {
// Cannot use anonymous namespace easily with templates included in headers
// Use a static const char* within the implementation file scope if possible,
// or define it where needed. Let's use a local const char* inside methods.
// const char* kLogTag = "OSAL_Queue"; // Define locally if needed
} // namespace


/**
 * @brief Structure holding the implementation details for OSAL::Queue.
 * Defined within the _impl.hpp file as it's needed by the template definition.
 * @tparam T The type of data items stored in the queue.
 */
template <typename T>
struct QueueImpl {
    QueueHandle_t handle_ = nullptr; ///< FreeRTOS queue handle.
    size_t max_items_ = 0;         ///< Capacity of the queue.
};


// --- Template Implementation ---

template <typename T>
Queue<T>::Queue(size_t max_items) : impl_(new (std::nothrow) QueueImpl<T>) {
    const char* log_tag = "OSAL_Queue"; // Local log tag
    if (!impl_) {
        ESP_LOGE(log_tag, "Failed to allocate memory for QueueImpl");
        return;
    }
    impl_->max_items_ = max_items;
    impl_->handle_ = xQueueCreate(max_items, sizeof(T));
    if (!impl_->handle_) {
        ESP_LOGE(log_tag, "Failed to create FreeRTOS queue (size: %u, items: %u).",
                 static_cast<unsigned int>(sizeof(T)),
                 static_cast<unsigned int>(max_items));
        delete impl_;
        impl_ = nullptr;
    } else {
         ESP_LOGD(log_tag, "Queue created (size: %u, items: %u).",
                 static_cast<unsigned int>(sizeof(T)),
                 static_cast<unsigned int>(max_items));
    }
}

template <typename T>
Queue<T>::~Queue() {
    const char* log_tag = "OSAL_Queue";
    if (impl_) {
        if (impl_->handle_) {
            vQueueDelete(impl_->handle_);
            ESP_LOGD(log_tag, "Queue deleted.");
        }
        delete impl_;
    }
}

template <typename T>
OSALStatus Queue<T>::Send(const T& item, uint32_t timeout_ms) {
    const char* log_tag = "OSAL_Queue";
    if (!impl_ || !impl_->handle_) {
        return OSALStatus::kErrorNotFound;
    }

    TickType_t ticks_to_wait = MsToTicks(timeout_ms);
    BaseType_t result;

    if (xPortInIsrContext()) {
        if (timeout_ms != kNoWait) {
            ESP_LOGE(log_tag, "Cannot Send() with blocking timeout from ISR context.");
            return OSALStatus::kErrorInvalidContext;
        }
        BaseType_t task_woken = pdFALSE;
        result = xQueueSendToBackFromISR(impl_->handle_, &item, &task_woken);
        // Potential yield if needed (usually handled by ESP-IDF FreeRTOS port)
        // if (task_woken == pdTRUE) { portYIELD_FROM_ISR(); }
    } else {
        result = xQueueSendToBack(impl_->handle_, &item, ticks_to_wait);
    }

    if (result == pdTRUE) {
        return OSALStatus::kSuccess;
    } else if (result == errQUEUE_FULL) {
        // This specific error code is only returned if timeout was 0 (kNoWait)
        return OSALStatus::kErrorQueueFull;
    } else {
        // Any other failure with a non-zero timeout implies timeout
        return OSALStatus::kErrorTimeout;
    }
}

template <typename T>
OSALStatus Queue<T>::SendUrgent(const T& item, uint32_t timeout_ms) {
     const char* log_tag = "OSAL_Queue";
    if (!impl_ || !impl_->handle_) {
        return OSALStatus::kErrorNotFound;
    }

    TickType_t ticks_to_wait = MsToTicks(timeout_ms);
    BaseType_t result;

    if (xPortInIsrContext()) {
        if (timeout_ms != kNoWait) {
            ESP_LOGE(log_tag, "Cannot SendUrgent() with blocking timeout from ISR context.");
            return OSALStatus::kErrorInvalidContext;
        }
        BaseType_t task_woken = pdFALSE;
        result = xQueueSendToFrontFromISR(impl_->handle_, &item, &task_woken);
        // Potential yield
        // if (task_woken == pdTRUE) { portYIELD_FROM_ISR(); }
    } else {
        result = xQueueSendToFront(impl_->handle_, &item, ticks_to_wait);
    }

    if (result == pdTRUE) {
        return OSALStatus::kSuccess;
    } else if (result == errQUEUE_FULL) {
        return OSALStatus::kErrorQueueFull;
    } else {
        return OSALStatus::kErrorTimeout;
    }
}


template <typename T>
OSALStatus Queue<T>::Receive(T& item_out, uint32_t timeout_ms) {
    const char* log_tag = "OSAL_Queue";
    if (!impl_ || !impl_->handle_) {
        return OSALStatus::kErrorNotFound;
    }

    // Cannot wait from an ISR
    if (xPortInIsrContext() && timeout_ms != kNoWait) {
         ESP_LOGE(log_tag, "Cannot Receive() with blocking timeout from ISR context.");
         return OSALStatus::kErrorInvalidContext;
    }

    TickType_t ticks_to_wait = MsToTicks(timeout_ms);
    BaseType_t result;

     if (xPortInIsrContext()) {
         BaseType_t task_woken = pdFALSE; // Not relevant for receive
         result = xQueueReceiveFromISR(impl_->handle_, &item_out, &task_woken);
         // No yield needed from receive ISR typically
     } else {
         result = xQueueReceive(impl_->handle_, &item_out, ticks_to_wait);
     }


    if (result == pdTRUE) {
        return OSALStatus::kSuccess;
    } else { // errQUEUE_EMPTY is implied if pdTRUE is not returned
         if (timeout_ms == kNoWait) {
            return OSALStatus::kErrorQueueEmpty;
         }
        return OSALStatus::kErrorTimeout;
    }
}

template <typename T>
OSALStatus Queue<T>::Peek(T& item_out, uint32_t timeout_ms) {
    const char* log_tag = "OSAL_Queue";
     if (!impl_ || !impl_->handle_) {
        return OSALStatus::kErrorNotFound;
    }

    // Cannot wait from an ISR
    if (xPortInIsrContext() && timeout_ms != kNoWait) {
         ESP_LOGE(log_tag, "Cannot Peek() with blocking timeout from ISR context.");
         return OSALStatus::kErrorInvalidContext;
    }

    TickType_t ticks_to_wait = MsToTicks(timeout_ms);
    BaseType_t result;

     if (xPortInIsrContext()) {
         // Peek from ISR exists but less common, needs task woken handling if used
         // For simplicity, let's disallow blocking peek from ISR
         if (timeout_ms != kNoWait) return OSALStatus::kErrorNotSupported;
         result = xQueuePeekFromISR(impl_->handle_, &item_out);
     } else {
         result = xQueuePeek(impl_->handle_, &item_out, ticks_to_wait);
     }

    if (result == pdTRUE) {
        return OSALStatus::kSuccess;
    } else {
         if (timeout_ms == kNoWait) {
            return OSALStatus::kErrorQueueEmpty;
         }
        return OSALStatus::kErrorTimeout;
    }
}


template <typename T>
size_t Queue<T>::GetCount() const {
    if (!impl_ || !impl_->handle_) {
        return 0;
    }
    // uxQueueMessagesWaiting is safe from task or ISR.
    return static_cast<size_t>(uxQueueMessagesWaiting(impl_->handle_));
}

template <typename T>
size_t Queue<T>::GetSpace() const {
     if (!impl_ || !impl_->handle_) {
        return 0;
    }
     // uxQueueSpacesAvailable is safe from task or ISR.
    return static_cast<size_t>(uxQueueSpacesAvailable(impl_->handle_));
}

template <typename T>
bool Queue<T>::IsFull() const {
    // Check space directly rather than comparing count to capacity
    // to avoid potential race conditions if called concurrently with send/receive.
    return GetSpace() == 0;
}

template <typename T>
bool Queue<T>::IsEmpty() const {
    return GetCount() == 0;
}

template <typename T>
OSALStatus Queue<T>::Reset() {
    if (!impl_ || !impl_->handle_) {
        return OSALStatus::kErrorNotFound;
    }
    // xQueueReset is safe from task or ISR (though use from ISR is rare).
    if(xQueueReset(impl_->handle_) == pdPASS) {
         return OSALStatus::kSuccess;
    } else {
        // Should not fail unless handle is invalid
        return OSALStatus::kErrorGeneral;
    }
}

} // namespace OSAL

-----------------------------------------------------------------------------------------
  //event group
  #pragma once

#include "osal_common.hpp" // For OSALStatus, timeouts
#include <cstdint>         // For uint32_t

// Forward declaration
struct EventGroupImpl;

namespace OSAL {

/**
 * @brief Type definition for event group bits. Typically 24 bits available in FreeRTOS.
 */
using EventBits_t = uint32_t;

/**
 * @brief Provides an event group synchronization mechanism.
 *
 * Allows tasks to wait for combinations of events (represented by bits)
 * set by other tasks or ISRs.
 */
class EventGroup {
public:
    /**
     * @brief Constructs an EventGroup object.
     */
    EventGroup();

    /**
     * @brief Destroys the EventGroup object.
     */
    ~EventGroup();

    /**
     * @brief Sets specified bits in the event group.
     * Can be called from tasks or ISRs.
     * @param bits_to_set The bits to set (ORed mask).
     * @return OSALStatus::kSuccess on success.
     */
    OSALStatus SetBits(EventBits_t bits_to_set);

    /**
     * @brief Clears specified bits in the event group.
     * Should generally only be called from tasks, not ISRs.
     * @param bits_to_clear The bits to clear (ORed mask).
     * @return OSALStatus::kSuccess on success.
     */
    OSALStatus ClearBits(EventBits_t bits_to_clear);

    /**
     * @brief Waits for a combination of bits to be set.
     *
     * Blocks the calling task until the specified bits are set or timeout occurs.
     * Cannot be called from an ISR.
     *
     * @param bits_to_wait_for The bits to wait for (ORed mask).
     * @param bits_out [out] The actual bits value *before* clearing (if clear_on_exit)
     * upon successful return or timeout.
     * @param clear_on_exit If true, the waited bits are cleared atomically on exit.
     * @param wait_for_all If true, waits for ALL bits in bits_to_wait_for to be set.
     * If false, waits for ANY bit in bits_to_wait_for to be set.
     * @param timeout_ms Maximum time to wait in milliseconds.
     * @return
     * - `OSALStatus::kSuccess` if the condition was met.
     * - `OSALStatus::kErrorTimeout` if timeout occurred.
     * - Other error codes.
     */
    OSALStatus WaitBits(EventBits_t bits_to_wait_for,
                        EventBits_t& bits_out,
                        bool clear_on_exit = true,
                        bool wait_for_all = true,
                        uint32_t timeout_ms = kWaitForever);

    /**
     * @brief Gets the current value of the event bits.
     * Can be called from tasks or ISRs. Does not block or modify bits.
     * @return The current event bits value, or 0 if invalid.
     */
    EventBits_t GetBits();


    // --- Deleted Methods ---
    EventGroup(const EventGroup&) = delete;
    EventGroup& operator=(const EventGroup&) = delete;
    EventGroup(EventGroup&&) = delete;
    EventGroup& operator=(EventGroup&&) = delete;

private:
    EventGroupImpl* impl_ = nullptr; ///< Opaque pointer to implementation details.
};

} // namespace OSAL
//event group.cc
#include "osal_event_group.hpp"

// --- ESP-IDF / FreeRTOS Specific Includes ---
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/portable.h" // For xPortInIsrContext
#include "esp_log.h"
// --- End Specific Includes ---

#include <new> // For std::nothrow

namespace {
const char* kLogTag = "OSAL_EventGroup";
} // namespace

/**
 * @brief Structure holding the implementation details for OSAL::EventGroup.
 */
struct EventGroupImpl {
    EventGroupHandle_t handle_ = nullptr; ///< FreeRTOS event group handle.
};

namespace OSAL {

EventGroup::EventGroup() : impl_(new (std::nothrow) EventGroupImpl) {
    if (!impl_) {
        ESP_LOGE(kLogTag, "Failed to allocate memory for EventGroupImpl");
        return;
    }
    impl_->handle_ = xEventGroupCreate();
    if (!impl_->handle_) {
        ESP_LOGE(kLogTag, "Failed to create FreeRTOS event group.");
        delete impl_;
        impl_ = nullptr;
    } else {
        ESP_LOGD(kLogTag, "EventGroup created.");
    }
}

EventGroup::~EventGroup() {
    if (impl_) {
        if (impl_->handle_) {
            vEventGroupDelete(impl_->handle_);
            ESP_LOGD(kLogTag, "EventGroup deleted.");
        }
        delete impl_;
    }
}

OSALStatus EventGroup::SetBits(EventBits_t bits_to_set) {
    if (!impl_ || !impl_->handle_) {
        return OSALStatus::kErrorNotFound;
    }

    EventBits_t result_bits; // Not always useful depending on FreeRTOS version
    if (xPortInIsrContext()) {
        BaseType_t task_woken = pdFALSE;
        // Note: xEventGroupSetBitsFromISR returns pdPASS/pdFAIL, not the bits.
        BaseType_t result = xEventGroupSetBitsFromISR(impl_->handle_, bits_to_set, &task_woken);
        // Potential yield
        // if (task_woken == pdTRUE) { portYIELD_FROM_ISR(); }
        return (result == pdPASS) ? OSALStatus::kSuccess : OSALStatus::kErrorGeneral;
    } else {
        result_bits = xEventGroupSetBits(impl_->handle_, bits_to_set);
        // In many FreeRTOS versions, SetBits returns the bits *before* they were set.
        // Success is generally assumed unless the handle is invalid.
        // We could check if (result_bits & bits_to_set) == bits_to_set after call
        // if we need strong confirmation, but generally kSuccess is returned.
        return OSALStatus::kSuccess;
    }
}

OSALStatus EventGroup::ClearBits(EventBits_t bits_to_clear) {
    if (!impl_ || !impl_->handle_) {
        return OSALStatus::kErrorNotFound;
    }

    // ClearBits from ISR exists but is less common and potentially problematic
    // if the ISR logic isn't carefully designed. Disallow for simplicity/safety.
    if (xPortInIsrContext()) {
         ESP_LOGE(kLogTag, "ClearBits() from ISR context is not supported by this OSAL wrapper.");
         return OSALStatus::kErrorNotSupported;
    }

    // xEventGroupClearBits returns the bits *before* they were cleared.
    EventBits_t result = xEventGroupClearBits(impl_->handle_, bits_to_clear);
    (void)result; // Unused for status check
    return OSALStatus::kSuccess;
}

OSALStatus EventGroup::WaitBits(EventBits_t bits_to_wait_for,
                                EventBits_t& bits_out,
                                bool clear_on_exit,
                                bool wait_for_all,
                                uint32_t timeout_ms)
{
    if (!impl_ || !impl_->handle_) {
        return OSALStatus::kErrorNotFound;
    }

    // Cannot wait from ISR
    if (xPortInIsrContext()) {
        ESP_LOGE(kLogTag, "WaitBits() cannot be called from ISR context.");
        return OSALStatus::kErrorInvalidContext;
    }

    TickType_t ticks_to_wait = MsToTicks(timeout_ms);
    EventBits_t result_bits = xEventGroupWaitBits(
        impl_->handle_,
        bits_to_wait_for,
        clear_on_exit ? pdTRUE : pdFALSE,
        wait_for_all ? pdTRUE : pdFALSE,
        ticks_to_wait);

    // Store the returned bits regardless of success/timeout
    bits_out = result_bits;

    // Check if the wait condition was actually met based on the returned bits.
    // xEventGroupWaitBits returns the bits present *when* it returned.
    bool condition_met = false;
    if (wait_for_all) {
        condition_met = ((result_bits & bits_to_wait_for) == bits_to_wait_for);
    } else { // Wait for ANY bit
        condition_met = ((result_bits & bits_to_wait_for) != 0);
    }

    // Also check if timeout might have occurred (only possible if condition not met)
    // FreeRTOS indicates timeout by not having the wait condition bits set in the result.
    if (condition_met) {
        return OSALStatus::kSuccess;
    } else {
        // If condition wasn't met, it must have been a timeout (or invalid params etc.)
        // We assume timeout if kSuccess wasn't achieved.
         ESP_LOGD(kLogTag, "WaitBits timed out or failed. Waited for: 0x%X, Got: 0x%X",
                 (unsigned int)bits_to_wait_for, (unsigned int)result_bits);
        return OSALStatus::kErrorTimeout;
    }
}

EventBits_t EventGroup::GetBits() {
    if (!impl_ || !impl_->handle_) {
        return 0;
    }

    if (xPortInIsrContext()) {
        return xEventGroupGetBitsFromISR(impl_->handle_);
    } else {
        return xEventGroupGetBits(impl_->handle_);
    }
}

} // namespace OSAL
-----------------------------------------------------------------------------------------------

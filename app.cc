// --- FileManager.h ---
#pragma once
#include <iostream> // For basic output, replace with actual logging/debug

class FileManager {
public:
    FileManager() = default;
    bool init() {
        std::cout << "FileManager Initializing..." << std::endl;
        // Initialize file system, mount partitions, etc.
        return true; // Return false on failure
    }
    // Add file operation methods here...
    void readFile(const char* path) { /* ... */ }
    void writeFile(const char* path, const char* data) { /* ... */ }
};

// --- EventManager.h ---
#pragma once
#include <iostream>

class EventManager {
public:
    EventManager() = default;
    bool init() {
        std::cout << "EventManager Initializing..." << std::endl;
        // Initialize event queues, topics, etc.
        return true;
    }
    // Add event posting/subscribing methods here...
    void postEvent(int eventId, void* data) { /* ... */ }
};

// --- ConfigStore.h ---
#pragma once
#include <iostream>

class ConfigStore {
public:
    ConfigStore() = default;
    bool init() {
        std::cout << "ConfigStore Initializing..." << std::endl;
        // Load configuration from NVM/flash
        return true;
    }
    // Add config get/set methods here...
    int getConfigValue(const char* key) { return 0; /* ... */ }
};

// --- AppContext.h ---
#pragma once
#include "FileManager.h"
#include "EventManager.h"
#include "ConfigStore.h"

class AppContext {
public:
    AppContext() = default; // Members initialized by their own defaults

    // Public access to shared components
    FileManager& getFileManager() { return fileManager_; }
    EventManager& getEventManager() { return eventManager_; }
    ConfigStore& getConfigStore() { return configStore_; }

    bool init() {
        std::cout << "AppContext Initializing..." << std::endl;
        if (!configStore_.init()) return false;
        if (!fileManager_.init()) return false;
        if (!eventManager_.init()) return false;
        std::cout << "AppContext Initialized Successfully." << std::endl;
        return true;
    }

private:
    // Owns the shared components
    FileManager fileManager_;
    EventManager eventManager_;
    ConfigStore configStore_;

    // Disable copy/move semantics if this context is unique
    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;
    AppContext(AppContext&&) = delete;
    AppContext& operator=(AppContext&&) = delete;
};

---------------------------------------------------------------------------------------------------------

  // --- DeviceManager.h ---
#pragma once
#include "AppContext.h"
#include "FreeRTOS.h" // Use correct FreeRTOS header paths
#include "task.h"
#include <iostream>

class DeviceManager {
public:
    // Constructor receives the shared AppContext
    DeviceManager(AppContext& context) : appContext_(context), taskHandle_(nullptr) {}

    bool init() {
        std::cout << "DeviceManager Initializing..." << std::endl;
        // Initialize hardware devices, peripherals, etc.
        // Example: Use appContext_.getConfigStore().getConfigValue("sensor_i2c_addr");

        // Create the FreeRTOS task for this manager
        BaseType_t status = xTaskCreate(
            taskFunction,        // Static function as task entry point
            "DeviceMgrTask",     // Task name
            configMINIMAL_STACK_SIZE * 2, // Stack size (adjust as needed)
            this,                // Pass pointer to this instance as parameter
            tskIDLE_PRIORITY + 2,// Task priority (adjust as needed)
            &taskHandle_         // Store task handle
        );

        if (status != pdPASS) {
            std::cerr << "ERROR: Failed to create DeviceManager task!" << std::endl;
            return false;
        }
        std::cout << "DeviceManager Initialized and Task Created." << std::endl;
        return true;
    }

    // This is the method run *by* the FreeRTOS task
    void runTask() {
        std::cout << "DeviceManager Task Started." << std::endl;
        // Use appContext_ components here, e.g.:
        // appContext_.getEventManager().postEvent(...);
        // appContext_.getFileManager().readFile(...);

        while (true) {
            // Main task loop logic: read sensors, manage peripherals, etc.
            std::cout << "[DeviceManager Task] Running..." << std::endl;
            readSensors();
            manageActuators();

            // Block for a period
            vTaskDelay(pdMS_TO_TICKS(1000)); // Delay 1 second
        }
    }

private:
    AppContext& appContext_; // Reference to the shared context
    TaskHandle_t taskHandle_; // Handle for the FreeRTOS task

    // Static function required by FreeRTOS xTaskCreate
    static void taskFunction(void* parameter) {
        // Cast the parameter back to a DeviceManager instance pointer
        DeviceManager* instance = static_cast<DeviceManager*>(parameter);
        // Call the instance's run method
        if (instance) {
            instance->runTask();
        }
        // Task should not return, but if it does, delete it
        vTaskDelete(NULL);
    }

    // Private helper methods
    void readSensors() { /* ... read sensor data ... */ }
    void manageActuators() { /* ... control actuators ... */ }

    // Disable copy/move
    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;
};


// --- ConnectivityManager.h ---
#pragma once
#include "AppContext.h"
#include "FreeRTOS.h"
#include "task.h"
#include <iostream>

class ConnectivityManager {
public:
    ConnectivityManager(AppContext& context) : appContext_(context), taskHandle_(nullptr) {}

    bool init() {
        std::cout << "ConnectivityManager Initializing..." << std::endl;
        // Initialize network stack (WiFi, Ethernet, BLE, etc.)
        // Use appContext_ here if needed for config (e.g., WiFi credentials)
        // appContext_.getConfigStore().getConfigValue("wifi_ssid");

        BaseType_t status = xTaskCreate(
            taskFunction,
            "ConnMgrTask",
            configMINIMAL_STACK_SIZE * 4, // Needs more stack? Adjust.
            this,
            tskIDLE_PRIORITY + 1, // Lower priority than DeviceManager? Adjust.
            &taskHandle_
        );

        if (status != pdPASS) {
            std::cerr << "ERROR: Failed to create ConnectivityManager task!" << std::endl;
            return false;
        }
        std::cout << "ConnectivityManager Initialized and Task Created." << std::endl;
        return true;
    }

    void runTask() {
        std::cout << "ConnectivityManager Task Started." << std::endl;
        // Use appContext_ here, e.g., to report events:
        // appContext_.getEventManager().postEvent(NETWORK_CONNECTED, nullptr);

        while (true) {
            // Main task loop: manage connections, send/receive data (MQTT, HTTP), etc.
            std::cout << "[ConnectivityManager Task] Running..." << std::endl;
            maintainConnection();
            processNetworkData();

            vTaskDelay(pdMS_TO_TICKS(500)); // Delay 0.5 seconds
        }
    }

private:
    AppContext& appContext_;
    TaskHandle_t taskHandle_;

    static void taskFunction(void* parameter) {
        ConnectivityManager* instance = static_cast<ConnectivityManager*>(parameter);
        if (instance) {
            instance->runTask();
        }
        vTaskDelete(NULL);
    }

    void maintainConnection() { /* ... check WiFi/network status, reconnect if needed ... */ }
    void processNetworkData() { /* ... handle MQTT messages, HTTP responses, etc. ... */ }

    // Disable copy/move
    ConnectivityManager(const ConnectivityManager&) = delete;
    ConnectivityManager& operator=(const ConnectivityManager&) = delete;
};



--------------------------------------------------------------------------------------------------------------


  // --- Application.h ---
#pragma once
#include "AppContext.h"
#include "DeviceManager.h"
#include "ConnectivityManager.h"
#include "FreeRTOS.h"
#include "task.h"
#include <iostream>

class Application {
public:
    // Constructor initializes members in the correct order
    // AppContext must be constructed before managers that depend on it
    Application() :
        appContext_(), // Default construct AppContext first
        deviceManager_(appContext_), // Pass context to DeviceManager
        connectivityManager_(appContext_) // Pass context to ConnectivityManager
    {}

    // Initialize all components
    bool init() {
        std::cout << "Application Initializing..." << std::endl;

        // 1. Initialize the shared context and its components
        if (!appContext_.init()) {
            std::cerr << "ERROR: Failed to initialize AppContext!" << std::endl;
            return false;
        }

        // 2. Initialize Device Manager (this will also create its task)
        if (!deviceManager_.init()) {
             std::cerr << "ERROR: Failed to initialize DeviceManager!" << std::endl;
            return false;
        }

        // 3. Initialize Connectivity Manager (this will also create its task)
        if (!connectivityManager_.init()) {
             std::cerr << "ERROR: Failed to initialize ConnectivityManager!" << std::endl;
            return false;
        }

        std::cout << "Application Initialized Successfully." << std::endl;
        return true;
    }

    // Start the application execution (typically starts the scheduler)
    void run() {
        std::cout << "Application Running. Starting FreeRTOS Scheduler..." << std::endl;
        // After init(), all tasks are created. Start the scheduler.
        // Control will not return from this call unless there's an error
        // or scheduler is explicitly stopped.
        vTaskStartScheduler();

        // Code here will generally not be reached if scheduler starts successfully
         std::cerr << "ERROR: Scheduler exited unexpectedly!" << std::endl;
         // Handle error state - perhaps loop forever or reset
         while(1);
    }

private:
    AppContext appContext_; // Owns the shared context
    DeviceManager deviceManager_; // Owns the device manager
    ConnectivityManager connectivityManager_; // Owns the connectivity manager

    // Disable copy/move
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
};

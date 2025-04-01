// my_app/common/app_context.h
#ifndef MY_APP_COMMON_APP_CONTEXT_H_
#define MY_APP_COMMON_APP_CONTEXT_H_

#include "my_app/common/config_store.h" // Include full types needed by value members
#include "my_app/common/event_manager.h"
#include "my_app/common/file_manager.h"

namespace my_app {

// Forward declarations are generally preferred in headers if only pointers/references are used,
// but since these are value members, full includes are necessary here.

/**
 * @class AppContext
 * @brief Container for shared application-wide resources and services.
 */
class AppContext {
 public:
  AppContext() = default;

  /**
   * @brief Initializes the context and all its owned components.
   * @return true if all components initialize successfully, false otherwise.
   */
  bool Init();

  // Provide const and non-const accessors as appropriate.
  // If the components themselves might be modified via the reference,
  // return non-const references. If access is read-only, return const references.

  /** @brief Provides access to the FileManager instance. */
  FileManager& file_manager() { return file_manager_; }
  /** @brief Provides const access to the FileManager instance. */
  const FileManager& file_manager() const { return file_manager_; }

  /** @brief Provides access to the EventManager instance. */
  EventManager& event_manager() { return event_manager_; }
  /** @brief Provides const access to the EventManager instance. */
  const EventManager& event_manager() const { return event_manager_; }

  /** @brief Provides access to the ConfigStore instance. */
  ConfigStore& config_store() { return config_store_; }
  /** @brief Provides const access to the ConfigStore instance. */
  const ConfigStore& config_store() const { return config_store_; }

  // Disallow copy and move operations.
  AppContext(const AppContext&) = delete;
  AppContext& operator=(const AppContext&) = delete;

 private:
  // Member variable names use lower_snake_case_
  FileManager file_manager_;
  EventManager event_manager_;
  ConfigStore config_store_;
};

} // namespace my_app

#endif // MY_APP_COMMON_APP_CONTEXT_H_

// my_app/common/app_context.cpp
#include "my_app/common/app_context.h"

#include <iostream> // For demonstration logging

namespace my_app {

bool AppContext::Init() {
  std::cout << "AppContext Initializing..." << std::endl;

  // Initialize components in a sensible order (e.g., config first)
  if (!config_store_.Init()) {
    std::cerr << "ERROR: AppContext failed to init ConfigStore!" << std::endl;
    return false;
  }
  if (!file_manager_.Init()) {
    std::cerr << "ERROR: AppContext failed to init FileManager!" << std::endl;
    return false;
  }
  if (!event_manager_.Init()) {
    std::cerr << "ERROR: AppContext failed to init EventManager!" << std::endl;
    return false;
  }

  std::cout << "AppContext Initialized Successfully." << std::endl;
  return true;
}

} // namespace my_app


----------------------------------------------------------------------------------------

    // my_app/device/device_manager.h
#ifndef MY_APP_DEVICE_DEVICE_MANAGER_H_
#define MY_APP_DEVICE_DEVICE_MANAGER_H_

// FreeRTOS headers (use correct paths for your build system)
#include "FreeRTOS.h"
#include "task.h"

namespace my_app {

// Forward declare dependencies
class AppContext;

/**
 * @class DeviceManager
 * @brief Manages hardware devices and peripherals (sensors, actuators).
 */
class DeviceManager {
 public:
  /**
   * @brief Constructor requiring application context. Marked explicit.
   * @param context Reference to the shared AppContext.
   */
  explicit DeviceManager(AppContext& context);

  /**
   * @brief Initializes the device manager, peripherals, and its FreeRTOS task.
   * @return true if initialization is successful, false otherwise.
   */
  bool Init();

  /**
   * @brief The main operational loop for the device manager task. Public for
   * potential testing access, but normally only called by TaskFunction.
   * @warning Contains an infinite loop. Should only be run by FreeRTOS.
   */
  void RunTask();

  // Disallow copy and move operations.
  DeviceManager(const DeviceManager&) = delete;
  DeviceManager& operator=(const DeviceManager&) = delete;

 private:
  /**
   * @brief Static entry function for the FreeRTOS task.
   * @param parameter Pointer to the DeviceManager instance.
   */
  static void TaskFunction(void* parameter);

  /** @brief Helper method to perform sensor reading logic. */
  void ReadSensors();

  /** @brief Helper method to manage actuator control logic. */
  void ManageActuators();

  // Member variable names use lower_snake_case_
  AppContext& app_context_; // Non-owning reference to shared context
  TaskHandle_t task_handle_ = nullptr; // FreeRTOS task handle
};

} // namespace my_app

#endif // MY_APP_DEVICE_DEVICE_MANAGER_H_

-----------------------------------------------------------------------------

    // my_app/device/device_manager.cpp
#include "my_app/device/device_manager.h"

#include <iomanip>  // For std::hex/std::dec
#include <iostream> // For demonstration logging

// Include full definitions needed for implementation
#include "my_app/common/app_context.h"
#include "my_app/common/config_store.h" // For GetConfigValue example
#include "my_app/common/event_manager.h" // For PostEvent example

namespace my_app {

// Define task parameters using Google Style constants
constexpr configSTACK_DEPTH_TYPE kDeviceManagerTaskStackSize = configMINIMAL_STACK_SIZE * 2;
constexpr UBaseType_t kDeviceManagerTaskPriority = tskIDLE_PRIORITY + 2;
constexpr TickType_t kDeviceManagerTaskDelayTicks = pdMS_TO_TICKS(1000);

// Define event IDs (consider a shared header/enum class)
constexpr int kEventIdDeviceReady = 100;

DeviceManager::DeviceManager(AppContext& context)
    : app_context_(context), task_handle_(nullptr) {
  std::cout << "DeviceManager Created." << std::endl;
}

bool DeviceManager::Init() {
  std::cout << "DeviceManager Initializing..." << std::endl;

  // Example: Access config store via context during initialization
  int sensor_addr = app_context_.config_store().GetConfigValue("sensor_i2c_addr");
  std::cout << "DeviceManager: Using sensor address: 0x" << std::hex
            << sensor_addr << std::dec << std::endl;

  // TODO(user): Initialize actual hardware peripherals here

  std::cout << "DeviceManager: Creating FreeRTOS task..." << std::endl;
  BaseType_t status = xTaskCreate(
      DeviceManager::TaskFunction, // Static task function
      "DeviceMgr",                 // Task name (shorter is often better)
      kDeviceManagerTaskStackSize, // Stack size
      this,                        // Pass instance pointer as parameter
      kDeviceManagerTaskPriority,  // Task priority
      &task_handle_                // Store task handle
  );

  if (status != pdPASS) {
    task_handle_ = nullptr; // Ensure handle is null on failure
    std::cerr << "ERROR: Failed to create DeviceManager task! Status: " << status
              << std::endl;
    // Consider using app_context_.event_manager().PostEvent(...) to signal failure
    return false;
  }

  std::cout << "DeviceManager Initialized and Task Created Successfully." << std::endl;
  return true;
}

void DeviceManager::RunTask() {
  std::cout << "DeviceManager Task Started. Entering main loop." << std::endl;

  // Example: Use EventManager from context within the task
  app_context_.event_manager().PostEvent(kEventIdDeviceReady, this);

  while (true) {
    // Perform periodic device management tasks
    std::cout << "[DeviceManager Task] Running cycle..." << std::endl;
    ReadSensors();
    ManageActuators();

    // Block the task for the specified delay
    vTaskDelay(kDeviceManagerTaskDelayTicks);
  }
  // This part should ideally not be reached.
}

// Static Task Wrapper - Implementation
void DeviceManager::TaskFunction(void* parameter) {
  std::cout << "DeviceManager static TaskFunction starting..." << std::endl;
  // Cast the parameter back to the DeviceManager instance
  DeviceManager* instance = static_cast<DeviceManager*>(parameter);

  if (instance != nullptr) {
    // Call the instance's run method, which contains the main loop
    instance->RunTask();
  } else {
    std::cerr << "ERROR: DeviceManager TaskFunction received nullptr parameter!"
              << std::endl;
  }

  // If RunTask somehow returns (it shouldn't), delete the task
  std::cerr << "ERROR: DeviceManager RunTask exited! Deleting task." << std::endl;
  vTaskDelete(nullptr); // nullptr means delete the calling task
}

void DeviceManager::ReadSensors() {
  // TODO(user): Implement sensor reading logic (I2C, SPI, ADC etc.)
  // std::cout << "[DeviceManager Task] Reading sensors..." << std::endl;
}

void DeviceManager::ManageActuators() {
  // TODO(user): Implement actuator control logic (GPIO, PWM etc.)
  // std::cout << "[DeviceManager Task] Managing actuators..." << std::endl;
}

} // namespace my_app

-------------------------------------------------------------------------------------------

    // my_app/network/connectivity_manager.h
#ifndef MY_APP_NETWORK_CONNECTIVITY_MANAGER_H_
#define MY_APP_NETWORK_CONNECTIVITY_MANAGER_H_

#include "my_app/network/network_protocol_handler.h" // Owns by value

// FreeRTOS headers
#include "FreeRTOS.h"
#include "task.h"

namespace my_app {

// Forward declarations
class AppContext;
class EventManager; // Needed for protocol handler constructor injection

/**
 * @class ConnectivityManager
 * @brief Manages network connectivity and uses a NetworkProtocolHandler.
 */
class ConnectivityManager {
 public:
  /**
   * @brief Constructor requiring application context. Marked explicit.
   * @param context Reference to the shared AppContext.
   */
  explicit ConnectivityManager(AppContext& context);

  /**
   * @brief Initializes the connectivity stack, protocol handler, and task.
   * @return true if initialization is successful, false otherwise.
   */
  bool Init();

  /**
   * @brief Main operational loop for the connectivity manager task.
   * @warning Contains an infinite loop. Should only be run by FreeRTOS.
   */
  void RunTask();

  // Disallow copy and move operations.
  ConnectivityManager(const ConnectivityManager&) = delete;
  ConnectivityManager& operator=(const ConnectivityManager&) = delete;

 private:
  /**
   * @brief Static entry function for the FreeRTOS task.
   * @param parameter Pointer to the ConnectivityManager instance.
   */
  static void TaskFunction(void* parameter);

  /** @brief Helper method to maintain the network connection state. */
  void MaintainConnection();

  /** @brief Helper method to process network data via the protocol handler. */
  void ProcessNetworkData();

  AppContext& app_context_;          // Non-owning reference
  TaskHandle_t task_handle_ = nullptr;
  NetworkProtocolHandler protocol_handler_; // Owns the protocol handler instance
};

} // namespace my_app

#endif // MY_APP_NETWORK_CONNECTIVITY_MANAGER_H_

------------------------------------------------------------------------------------------

    // my_app/network/connectivity_manager.cpp
#include "my_app/network/connectivity_manager.h"

#include <iostream> // For demonstration logging

// Include full definitions needed
#include "my_app/common/app_context.h"
#include "my_app/common/event_manager.h" // Needed for protocol handler injection source

namespace my_app {

// Define task parameters
constexpr configSTACK_DEPTH_TYPE kConnManagerTaskStackSize = configMINIMAL_STACK_SIZE * 4;
constexpr UBaseType_t kConnManagerTaskPriority = tskIDLE_PRIORITY + 1;
constexpr TickType_t kConnManagerTaskDelayTicks = pdMS_TO_TICKS(500);

// Define event IDs
constexpr int kEventIdNetworkInitDone = 200;
constexpr int kEventIdNetworkDisconnected = 202;

// Constructor initializes members in initializer list (Google style prefers this)
ConnectivityManager::ConnectivityManager(AppContext& context)
    : app_context_(context),
      task_handle_(nullptr),
      // Inject only the EventManager from AppContext into the handler
      protocol_handler_(context.event_manager()) {
  std::cout << "ConnectivityManager Created." << std::endl;
}

bool ConnectivityManager::Init() {
  std::cout << "ConnectivityManager Initializing..." << std::endl;

  // TODO(user): Get config (e.g., WiFi creds) from app_context_.config_store()
  // TODO(user): Initialize network hardware/stack (e.g., WiFi.begin())

  // Initialize the protocol handler (if necessary after network stack init)
  protocol_handler_.InitializeConnection();

  std::cout << "ConnectivityManager: Creating FreeRTOS task..." << std::endl;
  BaseType_t status = xTaskCreate(
      ConnectivityManager::TaskFunction, // Static task function
      "ConnMgr",                         // Task name
      kConnManagerTaskStackSize,         // Stack size
      this,                              // Pass instance pointer
      kConnManagerTaskPriority,          // Task priority
      &task_handle_                      // Store task handle
  );

  if (status != pdPASS) {
    task_handle_ = nullptr;
    std::cerr << "ERROR: Failed to create ConnectivityManager task! Status: "
              << status << std::endl;
    return false;
  }

  std::cout << "ConnectivityManager Initialized and Task Created Successfully."
            << std::endl;
  return true;
}

void ConnectivityManager::RunTask() {
  std::cout << "ConnectivityManager Task Started. Entering main loop." << std::endl;

  // Example: Signal network readiness (or attempt)
  app_context_.event_manager().PostEvent(kEventIdNetworkInitDone, this);

  while (true) {
    std::cout << "[ConnectivityManager Task] Running cycle..." << std::endl;

    // Perform periodic connectivity tasks
    MaintainConnection();
    ProcessNetworkData();

    // Block the task
    vTaskDelay(kConnManagerTaskDelayTicks);
  }
  // Should not be reached.
}

// Static Task Wrapper - Implementation
void ConnectivityManager::TaskFunction(void* parameter) {
  std::cout << "ConnectivityManager static TaskFunction starting..." << std::endl;
  ConnectivityManager* instance = static_cast<ConnectivityManager*>(parameter);
  if (instance != nullptr) {
    instance->RunTask();
  } else {
    std::cerr << "ERROR: ConnectivityManager TaskFunction received nullptr parameter!"
              << std::endl;
  }

  // If RunTask returns, clean up the task
  std::cerr << "ERROR: ConnectivityManager RunTask exited! Deleting task." << std::endl;
  vTaskDelete(nullptr);
}

void ConnectivityManager::MaintainConnection() {
  // TODO(user): Implement connection management (check status, reconnect etc.)
  // std::cout << "[ConnectivityManager Task] Maintaining connection..." << std::endl;
  // bool is_connected = CheckNetworkStatus(); // Placeholder
  // if (!is_connected) {
  //   app_context_.event_manager().PostEvent(kEventIdNetworkDisconnected, nullptr);
  //   AttemptReconnect(); // Placeholder
  // }
}

void ConnectivityManager::ProcessNetworkData() {
  // TODO(user): Implement data sending/receiving logic (sockets, MQTT loop etc.)
  std::cout << "[ConnectivityManager Task] Processing network data..." << std::endl;

  // Example: Simulate receiving data and passing it to the handler
  // char* incoming_buffer = ReadFromSocket(); // Placeholder
  const char* incoming_buffer = "Simulated:Sensor=Value"; // Placeholder
  if (incoming_buffer != nullptr /* && data_available */) {
    protocol_handler_.HandleIncomingPacket(incoming_buffer);
  }

  // Example: Send queued outgoing data
  // SendOutgoingPackets(); // Placeholder
}

} // namespace my_app

--------------------------------------------------------------------------------------------------

    // my_app/app/application.h
#ifndef MY_APP_APP_APPLICATION_H_
#define MY_APP_APP_APPLICATION_H_

#include "my_app/common/app_context.h"
#include "my_app/device/device_manager.h"
#include "my_app/network/connectivity_manager.h"

// FreeRTOS headers only needed for vTaskStartScheduler declaration
#include "FreeRTOS.h"
#include "task.h"

namespace my_app {

/**
 * @class Application
 * @brief Main application class owning and orchestrating major components.
 */
class Application {
 public:
  /**
   * @brief Constructor. Initializes application components.
   */
  Application();

  /**
   * @brief Initializes all core components of the application.
   * @return true if all initializations are successful, false otherwise.
   */
  bool Init();

  /**
   * @brief Starts the application's main execution by starting FreeRTOS.
   * @note This function typically does not return.
   */
  void Run(); // Or StartScheduler() might be a clearer name

  // Disallow copy and move operations.
  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

 private:
  // Owns the core components
  AppContext app_context_;
  DeviceManager device_manager_;
  ConnectivityManager connectivity_manager_;
};

} // namespace my_app

#endif // MY_APP_APP_APPLICATION_H_

-------------------------------------------------------------------------------------------

    // my_app/app/application.cpp
#include "my_app/app/application.h"

#include <iostream> // For demonstration logging

namespace my_app {

// Constructor initializes members in the initializer list
Application::Application()
    : app_context_(),                       // Default construct first
      device_manager_(app_context_),      // Pass context reference
      connectivity_manager_(app_context_) { // Pass context reference
  std::cout << "Application Object Created." << std::endl;
}

bool Application::Init() {
  std::cout << "Application Initializing..." << std::endl;

  // 1. Initialize shared context
  if (!app_context_.Init()) {
    std::cerr << "FATAL ERROR: Failed to initialize AppContext!" << std::endl;
    return false; // Critical failure
  }

  // 2. Initialize Device Manager (creates its task)
  if (!device_manager_.Init()) {
    std::cerr << "FATAL ERROR: Failed to initialize DeviceManager!" << std::endl;
    return false;
  }

  // 3. Initialize Connectivity Manager (creates its task)
  if (!connectivity_manager_.Init()) {
    std::cerr << "FATAL ERROR: Failed to initialize ConnectivityManager!" << std::endl;
    return false;
  }

  std::cout << "Application Initialized Successfully." << std::endl;
  return true;
}

void Application::Run() {
  std::cout << "Application Running. Starting FreeRTOS Scheduler..." << std::endl;
  std::cout << "--------------------------------------------------" << std::endl;

  // The scheduler takes control from here.
  vTaskStartScheduler();

  // --- Code below should not execute if scheduler starts ---
  std::cerr << "--------------------------------------------------" << std::endl;
  std::cerr << "FATAL ERROR: vTaskStartScheduler returned!" << std::endl;
  std::cerr << "Insufficient FreeRTOS heap likely cause." << std::endl;
  std::cerr << "System Halted." << std::endl;
  std::cout << "--------------------------------------------------" << std::endl;

  // Halt the system
  volatile int i = 0; // Prevent optimization
  while (true) {
    i++;
    // TODO(user): Add proper halt mechanism (e.g., error LED, reset)
  }
}

} // namespace my_app

----------------------------------------------------------------------------------------------

    // my_app/main.cpp
#include <iostream> // For boot messages

#include "my_app/app/application.h" // Include the main application header

// Global application instance (common pattern for embedded systems)
// Ensure proper initialization order if static initializers have dependencies.
// Here, Application's constructor handles internal dependencies correctly.
my_app::Application g_application; // Use namespace

/**
 * @brief Main function - the application's entry point.
 */
int main() {
  // --- Early Boot Sequence (usually handled by BSP/startup code) ---
  std::cout << "--------------------------------------------------" << std::endl;
  std::cout << "System Booting - main() entered." << std::endl;
  // Note: Using __DATE__/__TIME__ might not be ideal for reproducible builds
  std::cout << "Build Time (approx): " << __DATE__ << " " << __TIME__ << std::endl;
  std::cout << "--------------------------------------------------" << std::endl;

  // --- Application Initialization ---
  if (g_application.Init()) {
    // --- Start Application Execution ---
    g_application.Run(); // Starts the scheduler, should not return
  } else {
    // --- Initialization Failed ---
    std::cerr << "--------------------------------------------------" << std::endl;
    std::cerr << "FATAL: Application initialization failed in main()!" << std::endl;
    std::cerr << "System Halted." << std::endl;
    std::cout << "--------------------------------------------------" << std::endl;
  }

  // --- Halt on Failure or if Run() returns ---
  volatile int i = 0;
  while (true) {
    i++;
     // TODO(user): Implement proper halt state (error LED, reset etc.)
  }

  return 1; // Should be unreachable
}

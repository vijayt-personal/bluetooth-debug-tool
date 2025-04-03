#include <stdio.h>
#include <stdlib.h> // For atoi, strtol
#include "esp_log.h"
#include "esp_err.h"

// Include necessary RapidJSON headers
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"

static const char *TAG = "JSON_SCHEDULE_PARSE";

// Function to parse the specific JSON structure
esp_err_t parse_schedule_json(const char* json) {
    if (json == nullptr) {
        ESP_LOGE(TAG, "Input JSON string is null.");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Parsing JSON: %s", json);

    // 1. Create a Document object and parse the input string
    rapidjson::Document document;
    document.Parse(json); // Using non-destructive parse

    // 2. Check for parsing errors
    if (document.HasParseError()) {
        ESP_LOGE(TAG, "JSON parse error at offset %zu: %s",
                 document.GetErrorOffset(),
                 rapidjson::GetParseError_En(document.GetParseError()));
        return ESP_FAIL;
    }

    // 3. Check if the root is an object
    if (!document.IsObject()) {
        ESP_LOGE(TAG, "JSON root is not an object.");
        return ESP_FAIL;
    }

    // 4. Access the "slot" member
    int slot_number = -1; // Default value if not found or invalid
    if (document.HasMember("slot") && document["slot"].IsUint()) { // Or IsInt()
        slot_number = document["slot"].GetUint(); // Or GetInt()
        ESP_LOGI(TAG, "Found slot: %d", slot_number);
    } else {
        ESP_LOGW(TAG, "\"slot\" member missing or not an unsigned integer.");
        // Decide if this is a fatal error or if you can continue
        // return ESP_FAIL;
    }

    // 5. Access the "schedule" array
    if (document.HasMember("schedule") && document["schedule"].IsArray()) {
        const rapidjson::Value& scheduleArray = document["schedule"]; // Get reference to the array
        ESP_LOGI(TAG, "Found schedule array with %d items.", scheduleArray.Size());

        // 6. Iterate through the schedule array
        for (rapidjson::SizeType i = 0; i < scheduleArray.Size(); ++i) {
            ESP_LOGI(TAG, "Processing schedule item %d:", i);

            // Check if the current item is an object
            if (!scheduleArray[i].IsObject()) {
                ESP_LOGW(TAG, "Schedule item %d is not an object.", i);
                continue; // Skip to the next item
            }

            const rapidjson::Value& item = scheduleArray[i]; // Reference to the current object

            // Access "st" (start time)
            if (item.HasMember("st") && item["st"].IsString()) {
                const char* st_str = item["st"].GetString();
                int start_time = atoi(st_str); // Convert string to integer
                // For more robust conversion, consider strtol with error checking
                ESP_LOGI(TAG, "  st: %s (as int: %d)", st_str, start_time);
            } else {
                ESP_LOGW(TAG, "  'st' missing or not a string in item %d.", i);
            }

            // Access "et" (end time)
            if (item.HasMember("et") && item["et"].IsString()) {
                const char* et_str = item["et"].GetString();
                int end_time = atoi(et_str); // Convert string to integer
                ESP_LOGI(TAG, "  et: %s (as int: %d)", et_str, end_time);
            } else {
                ESP_LOGW(TAG, "  'et' missing or not a string in item %d.", i);
            }

            // Access "m" (mode)
            if (item.HasMember("m") && item["m"].IsString()) {
                const char* mode = item["m"].GetString();
                ESP_LOGI(TAG, "  m: %s", mode);
            } else {
                ESP_LOGW(TAG, "  'm' missing or not a string in item %d.", i);
            }

            // Access "rc" (return code / hex value)
            if (item.HasMember("rc") && item["rc"].IsString()) {
                const char* rc_str = item["rc"].GetString();
                // Convert hex string (e.g., "0x21") to integer
                long rc_val = strtol(rc_str, NULL, 0); // Base 0 auto-detects 0x prefix
                ESP_LOGI(TAG, "  rc: %s (as long: %ld / 0x%lX)", rc_str, rc_val, rc_val);
            } else {
                ESP_LOGW(TAG, "  'rc' missing or not a string in item %d.", i);
            }
        } // End of loop through schedule array

    } else {
        ESP_LOGW(TAG, "\"schedule\" member missing or not an array.");
        // Decide if this is fatal
        // return ESP_FAIL;
    }

    ESP_LOGI(TAG, "JSON parsing complete.");
    return ESP_OK;
}

// --- Example Usage ---
extern "C" void app_main(void)
{
    // The JSON string to parse
    const char* input_json = R"({
        "slot": 10,
        "schedule": [
            {
                "st": "500",
                "et": "600",
                "m": "LO",
                "rc": "0x21"
            },
            {
                "st": "1200",
                "et": "1430",
                "m": "HI",
                "rc": "0xA5"
            },
            {
                "st": "1500",
                "et": "1510",
                "m": "LO"
                // Missing "rc" in this one
            }
        ]
    })"; // C++ Raw string literal is convenient here

     const char* invalid_json = R"({"slot": 10, "schedule"[})"; // Example invalid JSON

    esp_err_t result;

    ESP_LOGI(TAG, "*** Parsing Valid JSON ***");
    result = parse_schedule_json(input_json);
    ESP_LOGI(TAG, "Valid JSON Parse Result: %s", esp_err_to_name(result));

    ESP_LOGI(TAG, "\n*** Parsing Invalid JSON ***");
    result = parse_schedule_json(invalid_json);
    ESP_LOGI(TAG, "Invalid JSON Parse Result: %s", esp_err_to_name(result));

    ESP_LOGI(TAG, "\n*** Parsing Null JSON ***");
    result = parse_schedule_json(nullptr);
    ESP_LOGI(TAG, "Null JSON Parse Result: %s", esp_err_to_name(result));

    // ... rest of your application ...
}
--------------------------------------------------------------------------------------------------------

  #include <stdio.h>   // For snprintf (though we might not need it now)
#include "esp_log.h" // For ESP-IDF logging

// Include necessary RapidJSON headers
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"

static const char *TAG = "JSON_NUMBERS_CREATE";

// Function to create the schedule JSON storing st, et, rc as numbers
void create_and_print_schedule_json_as_numbers() {
    ESP_LOGI(TAG, "Creating schedule JSON document with numbers...");

    // --- 1. Define variables holding the source data ---
    int current_slot = 10;

    // Data for first schedule item
    int start_time_1 = 500;
    int end_time_1 = 600;
    const char* mode_1 = "LO";
    int rc_1 = 0x21; // 33 decimal - will be stored as 33

    // Data for second schedule item (example with a float)
    // Let's pretend 'st' could be a float for demonstration
    float start_time_2 = 1200.5f;
    int end_time_2 = 1430;
    const char* mode_2 = "HI";
    int rc_2 = 0xA5; // 165 decimal - will be stored as 165

    // --- 2. Create the Document & Allocator ---
    rapidjson::Document document;
    auto& allocator = document.GetAllocator();

    // --- 3. Set Root Object ---
    document.SetObject();

    // --- 4. Add members from variables ---
    document.AddMember("slot", current_slot, allocator); // Add integer directly

    // --- 5. Create the schedule array ---
    rapidjson::Value scheduleArray(rapidjson::kArrayType);

    // --- 6. Process and add the first schedule item ---
    {
        rapidjson::Value item1(rapidjson::kObjectType);

        // Add numerical values directly
        item1.AddMember("st", start_time_1, allocator); // Adds JSON integer 500
        item1.AddMember("et", end_time_1, allocator);   // Adds JSON integer 600

        // Add mode (string) - needs copying
        item1.AddMember("m", rapidjson::Value(mode_1, allocator).Move(), allocator);

        // Add rc (numerical value) - will be stored as 33
        item1.AddMember("rc", rc_1, allocator);         // Adds JSON integer 33

        // Add the item to the array
        scheduleArray.PushBack(item1.Move(), allocator);
    }

    // --- 7. Process and add the second schedule item ---
    {
        rapidjson::Value item2(rapidjson::kObjectType);

        // Add numerical values directly
        item2.AddMember("st", start_time_2, allocator); // Adds JSON float 1200.5
        item2.AddMember("et", end_time_2, allocator);   // Adds JSON integer 1430

        // Add mode (string) - needs copying
        item2.AddMember("m", rapidjson::Value(mode_2, allocator).Move(), allocator);

        // Add rc (numerical value) - will be stored as 165
        item2.AddMember("rc", rc_2, allocator);         // Adds JSON integer 165

        scheduleArray.PushBack(item2.Move(), allocator);
    }

    // --- 8. Add the schedule array to the root document ---
    document.AddMember("schedule", scheduleArray.Move(), allocator);

    // --- 9. Serialize the Document to a String ---
    rapidjson::StringBuffer stringBuffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(stringBuffer);
    // Optional: Set precision for floating-point numbers if needed
    // writer.SetMaxDecimalPlaces(3);
    document.Accept(writer);

    // --- 10. Get and print the resulting string ---
    const char* jsonString = stringBuffer.GetString();
    ESP_LOGI(TAG, "Generated Schedule JSON with numbers:\n%s", jsonString);

    /* Expected output would look like:
    {
        "slot": 10,
        "schedule": [
            {
                "st": 500,
                "et": 600,
                "m": "LO",
                "rc": 33
            },
            {
                "st": 1200.5,
                "et": 1430,
                "m": "HI",
                "rc": 165
            }
        ]
    }
    */

}

// --- Example Usage ---
extern "C" void app_main(void)
{
    // ... other setup ...

    create_and_print_schedule_json_as_numbers();

    // ... rest of your app ...
}

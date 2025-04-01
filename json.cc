#include "json_wrapper.hpp"

#include <cstring>   // For strlen, memcpy with SetString
#include <limits>    // For numeric limits check (optional)
#include <utility>   // For std::move

// --- Anonymous Namespace for Internal Helpers ---
namespace {

// Create basic cJSON types. Return nullptr on allocation failure.
cJSON* CreateCJsonValue(bool value) {
  return value ? cJSON_CreateTrue() : cJSON_CreateFalse();
}
cJSON* CreateCJsonValue(double value) { return cJSON_CreateNumber(value); }
cJSON* CreateCJsonValue(const char* value) { return cJSON_CreateString(value); }
cJSON* CreateCJsonObject() { return cJSON_CreateObject(); }
cJSON* CreateCJsonArray() { return cJSON_CreateArray(); }

// Helper to add/replace item in object. Returns added/replaced node or nullptr.
// Takes ownership of 'item' only on success.
cJSON* SetOrAddCJsonItemToObject(cJSON* object, const char* key, cJSON* item) {
  if (!object || !cJSON_IsObject(object) || !key || !item) {
    cJSON_Delete(item);  // Clean up item if invalid input or target
    return nullptr;
  }
  cJSON* result_node = nullptr;
  if (cJSON_HasObjectItem(object, key)) {
    // Replace existing item
    if (!cJSON_ReplaceItemInObject(object, key, item)) {
      cJSON_Delete(item);  // Clean up item if replacement failed
      result_node = nullptr;
    } else {
      result_node = item;  // Replacement succeeded, ownership transferred
    }
  } else {
    // Add new item
    if (!cJSON_AddItemToObject(object, key, item)) {
      cJSON_Delete(item);  // Clean up item if adding failed
      result_node = nullptr;
    } else {
      result_node = item;  // Adding succeeded, ownership transferred
    }
  }
  return result_node;
}

// Helper to add item to array. Returns added node or nullptr.
// Takes ownership of 'item' only on success.
cJSON* AddCJsonItemToArray(cJSON* array, cJSON* item) {
  if (!array || !cJSON_IsArray(array) || !item) {
    cJSON_Delete(item);  // Clean up item if invalid input or target
    return nullptr;
  }
  if (!cJSON_AddItemToArray(array, item)) {
    cJSON_Delete(item);  // Clean up item if adding failed
    return nullptr;
  }
  // On success, cJSON takes ownership, return the pointer to the added node
  return item;
}

}  // namespace

// --- JsonVariant Implementation ---

bool JsonVariant::AsBool(bool default_val) const {
  if (!IsValid()) return default_val;
  if (IsBool()) return cJSON_IsTrue(node_);
  if (IsNumber()) return node_->valuedouble != 0.0;
  return default_val;
}

double JsonVariant::AsDouble(double default_val) const {
  if (!IsValid()) return default_val;
  if (IsNumber()) return node_->valuedouble;
  if (IsBool()) return cJSON_IsTrue(node_) ? 1.0 : 0.0;
  return default_val;
}

int JsonVariant::AsInt(int default_val) const {
  if (!IsValid()) return default_val;
  // Prioritize int representation if available (safer for large numbers)
  if (IsNumber() && node_->valueint == node_->valuedouble) {
     return node_->valueint;
  }
  // Otherwise, convert from double
  double val = AsDouble(static_cast<double>(default_val));
  // Add range checks using std::numeric_limits if necessary
  return static_cast<int>(std::lround(val));
}

int64_t JsonVariant::AsInt64(int64_t default_val) const {
   if (!IsValid()) return default_val;
   // Prioritize int representation if available and within safe double range
   if (IsNumber() && static_cast<double>(node_->valueint) == node_->valuedouble) {
     // If valueint accurately represents valuedouble, use it.
     // This handles cases where double might lose precision for large integers
     // but valueint still holds the correct integer value if it was set directly.
     // Note: cJSON might not always populate valueint reliably depending on creation.
     // A more robust solution might require string conversion for large ints.
     return static_cast<int64_t>(node_->valueint); // Cast might still be needed if valueint is just int
   }
  // Otherwise, convert from double
  double val = AsDouble(static_cast<double>(default_val));
  // Add range checks using std::numeric_limits if necessary
  return static_cast<int64_t>(std::llround(val));
}

std::string JsonVariant::AsString(const std::string& default_val) const {
  if (!IsValid() || !IsString() || node_->valuestring == nullptr) {
    return default_val;
  }
  return std::string(node_->valuestring);
}

bool JsonVariant::HasMember(const char* key) const {
  if (!IsValid() || !IsObject() || !key) {
    return false;
  }
  return cJSON_HasObjectItem(node_, key);
}

JsonVariant JsonVariant::operator[](const char* key) const {
  if (!IsValid() || !IsObject() || !key) {
    return JsonVariant();  // Invalid variant
  }
  return JsonVariant(cJSON_GetObjectItemCaseSensitive(node_, key));
}

int JsonVariant::GetSize() const {
  if (!IsValid() || !IsArray()) {
    return 0;
  }
  return cJSON_GetArraySize(node_);
}

JsonVariant JsonVariant::operator[](int index) const {
  if (!IsValid() || !IsArray()) {
    return JsonVariant();  // Invalid variant
  }
  return JsonVariant(cJSON_GetArrayItem(node_, index));
}

// --- JsonVariant Modification (Values) ---

bool JsonVariant::SetBool(bool value) {
  if (!IsValid() || !IsBool()) return false;
  node_->type = value ? cJSON_True : cJSON_False;
  return true;
}

bool JsonVariant::SetDouble(double value) {
  if (!IsValid() || !IsNumber()) return false;
  node_->valuedouble = value;
  // Also update valueint representation for consistency, mindful of truncation
  node_->valueint = static_cast<int>(value);
  return true;
}

bool JsonVariant::SetString(const char* value) {
  if (!IsValid() || !IsString()) return false;

  // Free existing string if necessary, using cJSON's allocator context
  if (node_->valuestring != nullptr) {
    cJSON_free(node_->valuestring);
    node_->valuestring = nullptr; // Avoid double free if allocation fails
  }

  if (value == nullptr) {
    return true; // Setting to null string is okay
  }

  size_t len = strlen(value) + 1;
  char* new_string = static_cast<char*>(cJSON_malloc(len));
  if (!new_string) {
    return false; // Allocation failed
  }
  memcpy(new_string, value, len);
  node_->valuestring = new_string;
  return true;
}

// --- JsonVariant Modification (Structure - Primitives) ---

JsonVariant JsonVariant::AddItem(const JsonVariant& item) {
  if (!IsValid() || !IsArray() || !item.IsValid()) return JsonVariant();
  cJSON* item_copy = cJSON_Duplicate(item.node_, true);
  if (!item_copy) return JsonVariant();
  return JsonVariant(AddCJsonItemToArray(node_, item_copy));
}

JsonVariant JsonVariant::AddItem(bool value) {
  return JsonVariant(AddCJsonItemToArray(node_, CreateCJsonValue(value)));
}
JsonVariant JsonVariant::AddItem(double value) {
  return JsonVariant(AddCJsonItemToArray(node_, CreateCJsonValue(value)));
}
JsonVariant JsonVariant::AddItem(const char* value) {
  return JsonVariant(AddCJsonItemToArray(node_, CreateCJsonValue(value)));
}

JsonVariant JsonVariant::AddMember(const char* key, const JsonVariant& item) {
  if (!IsValid() || !IsObject() || !key || !item.IsValid()) return JsonVariant();
  cJSON* item_copy = cJSON_Duplicate(item.node_, true);
  if (!item_copy) return JsonVariant();
  return JsonVariant(SetOrAddCJsonItemToObject(node_, key, item_copy));
}

JsonVariant JsonVariant::AddMember(const char* key, bool value) {
  return JsonVariant(SetOrAddCJsonItemToObject(node_, key, CreateCJsonValue(value)));
}
JsonVariant JsonVariant::AddMember(const char* key, double value) {
  return JsonVariant(SetOrAddCJsonItemToObject(node_, key, CreateCJsonValue(value)));
}
JsonVariant JsonVariant::AddMember(const char* key, const char* value) {
  return JsonVariant(SetOrAddCJsonItemToObject(node_, key, CreateCJsonValue(value)));
}

// --- JsonVariant Modification (Structure - Objects/Arrays) ---

JsonVariant JsonVariant::AddObjectMember(const char* key) {
    return JsonVariant(SetOrAddCJsonItemToObject(node_, key, CreateCJsonObject()));
}

JsonVariant JsonVariant::AddArrayMember(const char* key) {
     return JsonVariant(SetOrAddCJsonItemToObject(node_, key, CreateCJsonArray()));
}

JsonVariant JsonVariant::AddObjectItem() {
     return JsonVariant(AddCJsonItemToArray(node_, CreateCJsonObject()));
}

JsonVariant JsonVariant::AddArrayItem() {
     return JsonVariant(AddCJsonItemToArray(node_, CreateCJsonArray()));
}


// --- JsonDocument Implementation ---

JsonDocument::JsonDocument(const std::string& json_string) {
  Parse(json_string.c_str());
}

JsonDocument::JsonDocument(const char* json_string) { Parse(json_string); }

JsonDocument::JsonDocument(JsonDocument&& other) noexcept : root_(other.root_) {
  other.root_ = nullptr;
}

JsonDocument& JsonDocument::operator=(JsonDocument&& other) noexcept {
  if (this != &other) {
    Clear();
    root_ = other.root_;
    other.root_ = nullptr;
  }
  return *this;
}

JsonDocument::~JsonDocument() { Clear(); }

void JsonDocument::Clear() {
  if (root_ != nullptr) {
    cJSON_Delete(root_);
    root_ = nullptr;
  }
}

bool JsonDocument::Parse(const std::string& json_string) {
  return Parse(json_string.c_str());
}

bool JsonDocument::Parse(const char* json_string) {
  Clear();
  if (json_string == nullptr) {
    return false; // Cannot parse null
  }
  root_ = cJSON_Parse(json_string);
  // Optional: Check cJSON_GetErrorPtr() for detailed error info if root_ is null
  return root_ != nullptr;
}

std::string JsonDocument::Serialize(bool pretty) const {
  if (!root_) {
    return "null";
  }
  char* json_c_str = pretty ? cJSON_Print(root_) : cJSON_PrintUnformatted(root_);
  if (!json_c_str) {
    return ""; // Serialization failed (likely OOM)
  }
  std::string result(json_c_str);
  cJSON_free(json_c_str); // Use cJSON's default free
  return result;
}

JsonVariant JsonDocument::GetRoot() const { return JsonVariant(root_); }

bool JsonDocument::CreateObject() {
  Clear();
  root_ = cJSON_CreateObject();
  return root_ != nullptr;
}

bool JsonDocument::CreateArray() {
  Clear();
  root_ = cJSON_CreateArray();
  return root_ != nullptr;
}

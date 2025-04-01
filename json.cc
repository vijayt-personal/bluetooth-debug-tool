#include "json_wrapper.hpp"

#include <cstring>   // For strlen, memcpy with SetString
#include <limits>    // For numeric limits check (optional)
#include <utility>   // For std::move

// --- Anonymous Namespace for Internal Helpers ---
namespace {

// Helper to create basic cJSON types, returning nullptr on failure
cJSON* CreateCJsonValue(bool value) {
  return value ? cJSON_CreateTrue() : cJSON_CreateFalse();
}
cJSON* CreateCJsonValue(double value) { return cJSON_CreateNumber(value); }
cJSON* CreateCJsonValue(const char* value) { return cJSON_CreateString(value); }

// Helper to replace or add item in object/array
// Returns the newly added/replaced item node, or nullptr on failure
// Takes ownership of 'item' only on success.
cJSON* SetOrAddCJsonItemToObject(cJSON* object, const char* key, cJSON* item) {
  if (!object || !cJSON_IsObject(object) || !key || !item) {
    cJSON_Delete(item);  // Clean up item if it wasn't added/valid input
    return nullptr;
  }
  cJSON* result_node = nullptr;
  if (cJSON_HasObjectItem(object, key)) {
    if (!cJSON_ReplaceItemInObject(object, key, item)) {
      cJSON_Delete(item);  // Clean up item if replacement failed
      result_node = nullptr;
    } else {
      result_node = item; // Replacement succeeded, item ownership transferred
    }
  } else {
    if (!cJSON_AddItemToObject(object, key, item)) {
      cJSON_Delete(item);  // Clean up item if adding failed
      result_node = nullptr;
    } else {
       result_node = item; // Adding succeeded, item ownership transferred
    }
  }
  return result_node; // Return the node now owned by the object, or nullptr
}

// Helper to add item to array
// Returns the newly added item node, or nullptr on failure
// Takes ownership of 'item' only on success.
cJSON* AddCJsonItemToArray(cJSON* array, cJSON* item) {
  if (!array || !cJSON_IsArray(array) || !item) {
    cJSON_Delete(item);  // Clean up item if it wasn't added/valid input
    return nullptr;
  }
  if (!cJSON_AddItemToArray(array, item)) {
    cJSON_Delete(item);  // Clean up item if adding failed
    return nullptr;
  }
  // On success, cJSON takes ownership of item, so we return the pointer
  return item;
}

}  // namespace

// --- JsonVariant Implementation ---

bool JsonVariant::AsBool(bool default_val) const {
  if (!IsValid()) {
    // Log or handle error as desired - returning default here
    return default_val;
  }
  if (IsBool()) {
    return cJSON_IsTrue(node_);
  }
  // Allow number-to-bool conversion (0 is false, non-zero is true)
  if (IsNumber()) {
    return node_->valuedouble != 0.0;
  }
  // Type mismatch
  return default_val;
}

double JsonVariant::AsDouble(double default_val) const {
  if (!IsValid()) {
    return default_val;
  }
  if (IsNumber()) {
    return node_->valuedouble;
  }
  // Allow bool-to-number conversion (true is 1.0, false is 0.0)
  if (IsBool()) {
    return cJSON_IsTrue(node_) ? 1.0 : 0.0;
  }
  // Type mismatch
  return default_val;
}

int JsonVariant::AsInt(int default_val) const {
  double val = AsDouble(static_cast<double>(default_val));
  // Add checks for std::numeric_limits<int> if strict range checking needed
  // Be cautious about rounding vs truncation depending on requirements
  return static_cast<int>(std::lround(val));  // Round to nearest integer
}

int64_t JsonVariant::AsInt64(int64_t default_val) const {
  double val = AsDouble(static_cast<double>(default_val));
  // Add checks for std::numeric_limits<int64_t> if strict range checking needed
  return static_cast<int64_t>(std::llround(val));  // Round to nearest long long
}

std::string JsonVariant::AsString(const std::string& default_val) const {
  if (!IsValid() || !IsString()) {
    return default_val;
  }
  // Ensure valuestring is not null before creating string
  return std::string(node_->valuestring ? node_->valuestring : "");
}

bool JsonVariant::HasMember(const char* key) const {
  if (!IsValid() || !IsObject() || !key) {
    return false;
  }
  return cJSON_HasObjectItem(node_, key);
}

JsonVariant JsonVariant::operator[](const char* key) const {
  if (!IsValid() || !IsObject() || !key) {
    return JsonVariant();  // Return invalid variant
  }
  // cJSON handles the case where the key doesn't exist by returning NULL
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
    return JsonVariant();  // Return invalid variant
  }
  // cJSON_GetArrayItem handles bounds checking (returns NULL if out of bounds)
  return JsonVariant(cJSON_GetArrayItem(node_, index));
}

// --- JsonVariant Modification Implementation ---

bool JsonVariant::SetBool(bool value) {
  if (!IsValid() || !IsBool()) {
    return false; // Can only set if already a bool
  }
  // cJSON represents bools by type field
  node_->type = value ? cJSON_True : cJSON_False;
  return true;
}

bool JsonVariant::SetDouble(double value) {
  if (!IsValid() || !IsNumber()) {
    return false; // Can only set if already a number
  }
  node_->valuedouble = value;
  // Ensure cJSON's internal integer representation is updated if necessary
  // (usually handled internally, but good practice if direct manipulation occurs)
  node_->valueint = static_cast<int>(value); // Note: Potential truncation
  return true;
}

bool JsonVariant::SetString(const char* value) {
  if (!IsValid() || !IsString()) {
    return false; // Can only set if already a string
  }

  // Free existing string (cJSON uses its configured free function)
  if (node_->valuestring != nullptr) {
     cJSON_free(node_->valuestring);
     node_->valuestring = nullptr;
  }

  // Allocate and copy the new string
  if (value == nullptr) {
     return true; // Setting to null string is valid
  }

  size_t len = strlen(value) + 1;
  // Use cJSON's configured malloc function
  char* new_string = static_cast<char*>(cJSON_malloc(len));
  if (!new_string) {
    // Allocation failed
    return false;
  }
  memcpy(new_string, value, len);
  node_->valuestring = new_string;
  return true;
}

// Add item to array
JsonVariant JsonVariant::AddItem(const JsonVariant& item) {
  if (!IsValid() || !IsArray() || !item.IsValid()) {
    return JsonVariant(); // Invalid source/target or item
  }
  // Duplicate the item as cJSON takes ownership upon adding
  cJSON* item_copy = cJSON_Duplicate(item.node_, true /* deep copy */);
  if (!item_copy) {
    return JsonVariant(); // Duplication failed (e.g., OOM)
  }
  // AddCJsonItemToArray handles adding and returns the added node or nullptr
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

// Add member to object
JsonVariant JsonVariant::AddMember(const char* key, const JsonVariant& item) {
  if (!IsValid() || !IsObject() || !key || !item.IsValid()) {
     return JsonVariant(); // Invalid source/target or item
  }
  // Duplicate the item as cJSON takes ownership upon adding/replacing
  cJSON* item_copy = cJSON_Duplicate(item.node_, true /* deep copy */);
   if (!item_copy) {
    return JsonVariant(); // Duplication failed
  }
  // SetOrAddCJsonItemToObject handles adding/replacing and returns node or nullptr
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

// --- JsonDocument Implementation ---

JsonDocument::JsonDocument(const std::string& json_string) {
  Parse(json_string.c_str());
}

JsonDocument::JsonDocument(const char* json_string) { Parse(json_string); }

// Move constructor
JsonDocument::JsonDocument(JsonDocument&& other) noexcept : root_(other.root_) {
  other.root_ = nullptr;  // Prevent double deletion by the moved-from object
}

// Move assignment
JsonDocument& JsonDocument::operator=(JsonDocument&& other) noexcept {
  if (this != &other) {
    Clear();              // Delete existing content
    root_ = other.root_;
    other.root_ = nullptr;  // Prevent double deletion
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
  Clear();  // Clear existing content before parsing
  if (json_string == nullptr) {
    // Or handle error differently (throw exception, set error state)
    return false;
  }

  root_ = cJSON_Parse(json_string);

  if (root_ == nullptr) {
    // Optional: Could store cJSON_GetErrorPtr() result if more detail needed
    // const char* error_ptr = cJSON_GetErrorPtr();
    return false; // Parsing failed
  }
  return true; // Parsing succeeded
}

std::string JsonDocument::Serialize(bool pretty) const {
  if (!root_) {
    return "null";  // Represent empty document as "null" string
  }

  char* json_c_str = pretty ? cJSON_Print(root_) : cJSON_PrintUnformatted(root_);
  if (!json_c_str) {
    // Serialization failed (likely memory allocation)
    return ""; // Return empty string on failure
  }

  std::string result(json_c_str);
  cJSON_free(json_c_str);  // Use cJSON's free function
  return result;
}

JsonVariant JsonDocument::GetRoot() const {
  return JsonVariant(root_);  // Creates a variant view of the root
}

bool JsonDocument::CreateObject() {
  Clear();
  root_ = cJSON_CreateObject();
  return root_ != nullptr; // Success if allocation succeeded
}

bool JsonDocument::CreateArray() {
  Clear();
  root_ = cJSON_CreateArray();
  return root_ != nullptr; // Success if allocation succeeded
}

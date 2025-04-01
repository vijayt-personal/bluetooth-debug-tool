#ifndef ROLLING_AVERAGE_H_
#define ROLLING_AVERAGE_H_

#include <cstddef>     // For size_t
#include <functional>  // For std::function
#include <limits>      // For std::numeric_limits
#include <array>       // For std::array declaration
#include <cmath>       // For NAN (Not-A-Number)

/**
 * @brief Callback function type for threshold breaches.
 * @param value The float value that breached the threshold.
 * @param is_min True if the minimum threshold was breached, false if the maximum.
 */
using ThresholdCallback = std::function<void(float value, bool is_min)>;

/**
 * @class RollingAverage
 * @brief Calculates a rolling average for float values over a fixed-size window.
 * (Updated to allow optional waiting for full window and changing window size)
 */
class RollingAverage {
 public:
  static constexpr size_t kMaxWindowSize = 25;

  RollingAverage(size_t window_size,
                 float min_threshold = std::numeric_limits<float>::lowest(),
                 float max_threshold = std::numeric_limits<float>::max(),
                 ThresholdCallback threshold_callback = nullptr);

  void AddSample(float sample);

  /**
   * @brief Gets the current rolling average.
   * @param require_full_window If true, returns NAN unless the window is full.
   * If false, returns average based on available samples (>=1).
   * @return float The calculated rolling average, 0.0f if no samples yet, or NAN if
   * require_full_window is true and window is not full.
   */
  float GetAverage(bool require_full_window = false) const;

  /**
   * @brief Gets the minimum value in the current window.
   * @param require_full_window If true, returns NAN unless the window is full.
   * If false, returns min based on available samples (>=1).
   * @return float Min value, numeric_limits<float>::max() if no samples, or NAN if
   * require_full_window is true and window is not full.
   */
  float GetMin(bool require_full_window = false) const;

  /**
   * @brief Gets the maximum value in the current window.
   * @param require_full_window If true, returns NAN unless the window is full.
   * If false, returns max based on available samples (>=1).
   * @return float Max value, numeric_limits<float>::lowest() if no samples, or NAN if
   * require_full_window is true and window is not full.
   */
  float GetMax(bool require_full_window = false) const;

  bool IsDataAvailable() const;

  void Clear();

  /**
   * @brief Changes the window size for subsequent calculations.
   * @warning This will CLEAR all currently stored samples and reset statistics.
   * @param new_size The new window size (must be > 0 and <= kMaxWindowSize).
   */
  void SetWindowSize(size_t new_size);

  void SetMinThreshold(float threshold);
  void SetMaxThreshold(float threshold);
  void EnableMinThreshold(bool enable);
  void EnableMaxThreshold(bool enable);
  void SetThresholdCallback(ThresholdCallback callback);
  size_t GetWindowSize() const;
  size_t GetSampleCount() const;

 private:
  void RecalculateStats();

  // Configuration
  // Removed const to allow SetWindowSize
  size_t window_size_actual_;
  float min_threshold_;
  float max_threshold_;
  bool min_threshold_enabled_;
  bool max_threshold_enabled_;
  ThresholdCallback threshold_callback_;

  // State
  std::array<float, kMaxWindowSize> samples_;
  size_t current_sample_count_;
  size_t next_sample_index_;
  double current_sum_;
  float current_average_;
  float current_min_;
  float current_max_;
  bool data_available_;
};

#endif // ROLLING_AVERAGE_H_


--------------------------------------------------------------------------------
  #include "rolling_average.h"

#include <cassert>    // For assert
#include <algorithm>  // For std::min/max
#include <cmath>      // For std::isnan, std::isinf, NAN
#include <limits>     // Included again for clarity where limits are used

// --- Constructor ---
RollingAverage::RollingAverage(size_t window_size, float min_threshold,
                             float max_threshold,
                             ThresholdCallback threshold_callback)
    : window_size_actual_(window_size), // Initialized here
      current_sample_count_(0),
      next_sample_index_(0),
      current_sum_(0.0),
      current_average_(0.0f),
      current_min_(std::numeric_limits<float>::max()),
      current_max_(std::numeric_limits<float>::lowest()),
      data_available_(false),
      min_threshold_(min_threshold),
      max_threshold_(max_threshold),
      min_threshold_enabled_(min_threshold != std::numeric_limits<float>::lowest()),
      max_threshold_enabled_(max_threshold != std::numeric_limits<float>::max()),
      threshold_callback_(threshold_callback)
       {
  assert(window_size > 0 && "Window size must be greater than 0");
  assert(window_size <= kMaxWindowSize && "Window size exceeds kMaxWindowSize");
  samples_.fill(0.0f);
}

// --- Public Methods ---

void RollingAverage::AddSample(float sample) {
  if (std::isnan(sample) || std::isinf(sample)) {
      return; // Ignore invalid samples
  }
  if (current_sample_count_ == window_size_actual_) {
    current_sum_ -= static_cast<double>(samples_[next_sample_index_]);
  } else {
    current_sample_count_++;
  }
  samples_[next_sample_index_] = sample;
  current_sum_ += static_cast<double>(sample);
  next_sample_index_ = (next_sample_index_ + 1) % window_size_actual_;
  RecalculateStats();
  data_available_ = true;

  if (threshold_callback_) {
      if (min_threshold_enabled_ && sample < min_threshold_) {
          threshold_callback_(sample, true);
      }
      if (max_threshold_enabled_ && sample > max_threshold_) {
          threshold_callback_(sample, false);
      }
  }
}

float RollingAverage::GetAverage(bool require_full_window /*= false*/) const {
    if (!data_available_) {
        // Return 0.0f or NAN? NAN is less ambiguous for "no data yet"
        return NAN;
    }
    // Check if a full window is required but not available
    if (require_full_window && (current_sample_count_ < window_size_actual_)) {
        return NAN; // Indicate not ready
    }
    // Otherwise, return the calculated average based on available samples
    return current_average_;
}

float RollingAverage::GetMin(bool require_full_window /*= false*/) const {
    if (!data_available_) {
        // Return standard "no data" value or NAN? Sticking with limits for min/max default
        return std::numeric_limits<float>::max();
    }
    if (require_full_window && (current_sample_count_ < window_size_actual_)) {
        return NAN; // Indicate not ready
    }
    return current_min_;
}

float RollingAverage::GetMax(bool require_full_window /*= false*/) const {
    if (!data_available_) {
        // Return standard "no data" value or NAN?
        return std::numeric_limits<float>::lowest();
    }
    if (require_full_window && (current_sample_count_ < window_size_actual_)) {
        return NAN; // Indicate not ready
    }
    return current_max_;
}


bool RollingAverage::IsDataAvailable() const {
  return data_available_;
}

void RollingAverage::Clear() {
  current_sample_count_ = 0;
  next_sample_index_ = 0;
  current_sum_ = 0.0;
  current_average_ = 0.0f;
  current_min_ = std::numeric_limits<float>::max();
  current_max_ = std::numeric_limits<float>::lowest();
  data_available_ = false;
  // samples_.fill(0.0f); // Optional
}

void RollingAverage::SetWindowSize(size_t new_size) {
    assert(new_size > 0 && "Window size must be greater than 0");
    assert(new_size <= kMaxWindowSize && "New window size exceeds kMaxWindowSize");

    if (new_size != window_size_actual_) {
        // Clear existing data as the basis changes
        Clear();
        // Set the new size
        window_size_actual_ = new_size;
        // Note: next_sample_index_ is already 0 from Clear()
    }
}


void RollingAverage::SetMinThreshold(float threshold) {
    min_threshold_ = threshold;
    min_threshold_enabled_ = true;
}

void RollingAverage::SetMaxThreshold(float threshold) {
    max_threshold_ = threshold;
    max_threshold_enabled_ = true;
}

void RollingAverage::EnableMinThreshold(bool enable) {
    min_threshold_enabled_ = enable;
}

void RollingAverage::EnableMaxThreshold(bool enable) {
    max_threshold_enabled_ = enable;
}

void RollingAverage::SetThresholdCallback(ThresholdCallback callback) {
    threshold_callback_ = callback;
}

size_t RollingAverage::GetWindowSize() const {
    return window_size_actual_;
}

size_t RollingAverage::GetSampleCount() const {
    return current_sample_count_;
}

// --- Private Methods ---

void RollingAverage::RecalculateStats() {
  // Assumes current_sample_count_ > 0
  current_average_ = static_cast<float>(current_sum_ / current_sample_count_);

  float min_val = std::numeric_limits<float>::max();
  float max_val = std::numeric_limits<float>::lowest();
  size_t start_index = (current_sample_count_ == window_size_actual_) ? next_sample_index_ : 0;

  for (size_t i = 0; i < current_sample_count_; ++i) {
    size_t current_index = (start_index + i) % window_size_actual_;
    min_val = std::min(min_val, samples_[current_index]);
    max_val = std::max(max_val, samples_[current_index]);
  }
  current_min_ = min_val;
  current_max_ = max_val;
}
------------------------------------------------------------------------------------------------------

  #include "rolling_average.h"
#include <iostream>
#include <vector>
#include <iomanip>
#include <cmath> // For isnan

// ... (handle_threshold callback function as before) ...

int main() {
    // Start with window size 3
    RollingAverage temp_average(3);
    std::cout << "Initial window size: " << temp_average.GetWindowSize() << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    std::vector<float> samples_to_add = {10.0f, 12.0f, 11.0f, 14.0f, 15.0f};

    for (size_t i = 0; i < samples_to_add.size(); ++i) {
        float current_sample = samples_to_add[i];
        std::cout << "\n--- Adding sample #" << (i + 1) << ": " << current_sample << " ---" << std::endl;
        std::cout << "  (Current window size: " << temp_average.GetWindowSize()
                  << ", Samples so far: " << temp_average.GetSampleCount() << ")" << std::endl;

        temp_average.AddSample(current_sample);

        // Get average based on available samples
        float avg_available = temp_average.GetAverage(false); // require_full_window = false (default)
        if (!std::isnan(avg_available)) {
            std::cout << "  Avg (Available): " << avg_available << std::endl;
        } else {
            std::cout << "  Avg (Available): Not available yet (NAN)" << std::endl;
        }

        // Get average ONLY if window is full
        float avg_full = temp_average.GetAverage(true); // require_full_window = true
        if (!std::isnan(avg_full)) {
            std::cout << "  Avg (Full Window): " << avg_full << std::endl;
        } else {
            std::cout << "  Avg (Full Window): Window not full yet (NAN)" << std::endl;
        }
         std::cout << "  Min (Available):   " << temp_average.GetMin(false) << std::endl;
         std::cout << "  Max (Available):   " << temp_average.GetMax(false) << std::endl;
    }

    std::cout << "\n--- Changing window size to 5 ---" << std::endl;
    temp_average.SetWindowSize(5); // This calls Clear() internally
    std::cout << "  New window size: " << temp_average.GetWindowSize() << std::endl;
    std::cout << "  Sample count after resize: " << temp_average.GetSampleCount() << std::endl;
    if (!temp_average.IsDataAvailable()){
        std::cout << "  DataAvailable is false after resize/clear." << std::endl;
        float avg_after_resize = temp_average.GetAverage(false);
         if (std::isnan(avg_after_resize)) { // Check for NAN as GetAverage now returns NAN if !data_available_
             std::cout << "  Average is NAN after resize/clear." << std::endl;
         }
    }

     // Add more samples with the new window size
     std::vector<float> more_samples = {20.0f, 21.0f, 22.0f, 23.0f, 24.0f, 25.0f};
      for (size_t i = 0; i < more_samples.size(); ++i) {
          float current_sample = more_samples[i];
          std::cout << "\n--- Adding sample #" << (samples_to_add.size() + i + 1) << ": " << current_sample << " ---" << std::endl;
           std::cout << "  (Current window size: " << temp_average.GetWindowSize()
                  << ", Samples so far: " << temp_average.GetSampleCount() << ")" << std::endl;
          temp_average.AddSample(current_sample);
          float avg_available = temp_average.GetAverage(false);
          float avg_full = temp_average.GetAverage(true);
          std::cout << "  Avg (Available): " << (std::isnan(avg_available) ? "NAN" : std::to_string(avg_available)) << std::endl;
          std::cout << "  Avg (Full Window): " << (std::isnan(avg_full) ? "NAN" : std::to_string(avg_full)) << std::endl;
      }


    return 0;
}

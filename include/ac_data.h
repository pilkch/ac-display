#pragma once

#include <mutex>

class cACData {
public:
  cACData();

  uint8_t gear;
  float accelerator_0_to_1;
  float brake_0_to_1;
  float clutch_0_to_1;
  float rpm;
  float speed_kmh;
  uint32_t lap_time_ms;
  uint32_t last_lap_ms;
  uint32_t best_lap_ms;
  uint32_t lap_count;
};

// Mutex and data
// Lock the mutex, use the data, and unlock the mutex
extern std::mutex mutex_ac_data;
extern cACData ac_data;

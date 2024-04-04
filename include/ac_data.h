#pragma once

#include <mutex>

class cACData {
public:
  cACData();

  // Config is not expected to change very frequently
  // NOTE: Assetto Corsa doesn't provide any of these values so we have to make them up, I think AC expects you to be on the same machine and look it up in that car's config file?
  float config_rpm_red_line;
  float config_rpm_maximum;
  float config_speedometer_red_line_kph;
  float config_speedometer_maximum_kph;

  // Update date changes frequently
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

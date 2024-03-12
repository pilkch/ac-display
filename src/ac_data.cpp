#include "ac_data.h"

cACData::cACData() :
  gear(1), // Neutral
  accelerator_0_to_1(0.0f),
  brake_0_to_1(0.0f),
  clutch_0_to_1(0.0f),
  rpm(0.0f),
  speed_kmh(0.0f),
  lap_time_ms(0),
  last_lap_ms(0),
  best_lap_ms(0),
  lap_count(0)
{
}

std::mutex mutex_ac_data;
cACData ac_data;

#include <cmath>

#include <functional>
#include <iostream>
#include <thread>

#include "ac_data.h"
#include "debug_sine_wave_update_thread.h"
#include "util.h"

namespace {

// Just a regular sort of idle RPM
const float fRPMIdle = 800.0f;

float GetRPMShiftPoint()
{
  std::lock_guard<std::mutex> lock(mutex_ac_data);
  return ac_data.config_rpm_red_line;
}

}

namespace acdisplay {

class cDebugSineWaveUpdateThread {
public:
  void MainLoop();
};

void cDebugSineWaveUpdateThread::MainLoop()
{
  std::cout<<"cDebugSineWaveUpdateThread::MainLoop"<<std::endl;

  const float fRPMShiftPoint = GetRPMShiftPoint();

  const uint64_t start = util::GetTimeMS();

  while (true) {
    // Test with a sin wave
    // This is a bit hacky, basically we just want a sin wave that cycles smoothly between idle RPM to the shift point RPM
    const uint64_t delta = util::GetTimeMS() - start;
    const float e = 0.001f * float(delta);
    const float range = (fRPMShiftPoint - fRPMIdle);
    const float g = 0.5f * sinf(e);
    const float h = fRPMIdle + (0.5f * range) + (range * g);
    const int rpm = util::clamp(int(h), int(fRPMIdle), int(fRPMIdle + range));
    //std::cout<<"delta: "<<delta<<", e: "<<e<<", g: "<<g<<", h: "<<h<<", rpm: "<<rpm<<std::endl;
    const float speed_kph = 150.0f + (100.0f * sinf(0.5f * e));
    //std::cout<<"rpm: "<<rpm<<", speed: "<<speed_kph<<std::endl;

    // Update the shared rpm value
    {
      std::lock_guard<std::mutex> lock(mutex_ac_data);
      ac_data.rpm = rpm;
      ac_data.speed_kmh = speed_kph;
    }

    util::msleep(50);
  }
}

// Not the most elegant method, but it works
int DebugSineWaveUpdateRunThreadFunction(void* pData)
{
  if (pData == nullptr) {
    return 1;
  }

  cDebugSineWaveUpdateThread* pThis = static_cast<cDebugSineWaveUpdateThread*>(pData);
  if (pThis == nullptr) {
    return 1;
  }

  std::cout<<"DebugSineWaveUpdateRunThreadFunction Calling MainLoop"<<std::endl;
  pThis->MainLoop();
  std::cout<<"DebugSineWaveUpdateRunThreadFunction MainLoop returned"<<std::endl;

  return 0;
}

bool DebugStartSineWaveUpdateThread()
{
  std::cout<<"DebugStartSineWaveUpdateThread"<<std::endl;

  // Ok we have successfully connected and subscribed so now we can start the thread to read updates
  cDebugSineWaveUpdateThread* pDebugSineWaveUpdateThread = new cDebugSineWaveUpdateThread;
  if (pDebugSineWaveUpdateThread == nullptr) {
    std::cerr<<"DebugStartSineWaveUpdateThread Error creating DebugSineWaveUpdate thread, returning false"<<std::endl;
    return false;
  }

  // Start the thread
  // NOTE: We never release this, it is ugly, but we don't shut down gracefully. We could create a regular object, then give the thread a signal to stop, then have the thread exit gracefully
  std::thread* pThread = new std::thread(std::bind(&DebugSineWaveUpdateRunThreadFunction, pDebugSineWaveUpdateThread));
  (void)pThread;

  return true;
}

}

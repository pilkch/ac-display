#include <fstream>
#include <iostream>
#include <string>

#include "ac_display.h"
#include "util.h"
#include "web_server.h"

// Enable this to turn on a debug mode where we don't read from the AC UDP socket, and instead just cycle the RPM and speed up and down for testing purposes
//#define DEBUG_SINE_WAVE

#ifdef DEBUG_SINE_WAVE
#include "debug_sine_wave_update_thread.h"
#else
#include "acudp_thread.h"
#endif

namespace acdisplay {

bool RunServer(const application::cSettings& settings)
{
  std::cout<<"Running server"<<std::endl;

#ifndef DEBUG_SINE_WAVE
  // Start the ACUDP thread
  if (!StartACUDPThread(settings.GetACUDPHost(), settings.GetACUDPPort())) {
    std::cout<<"Error connecting to "<<util::ToString(settings.GetACUDPHost())<<":"<<settings.GetACUDPPort()<<std::endl;
    return false;
  }
#else
  // Start the SineWaveUpdate thread for debugging
  if (!DebugStartSineWaveUpdateThread()) {
    std::cout<<"Error creating SineWaveUpdateThread"<<std::endl;
    return false;
  }
#endif

  // Now run the web server
  const bool result = RunWebServer(settings.GetHTTPSHost(), settings.GetHTTPSPort(), settings.GetHTTPSPrivateKey(), settings.GetHTTPSPublicCert());

  std::cout<<"Shutting down server"<<std::endl;

  return result;
}

}

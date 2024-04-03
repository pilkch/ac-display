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
    std::cerr<<"Error connecting to "<<util::ToString(settings.GetACUDPHost())<<":"<<settings.GetACUDPPort()<<std::endl;
    return false;
  }
#else
  // Start the SineWaveUpdate thread for debugging
  if (!DebugStartSineWaveUpdateThread()) {
    std::cerr<<"Error creating SineWaveUpdateThread"<<std::endl;
    return false;
  }
#endif

  // Now run the web server
  cWebServerManager web_server_manager;
  if (!web_server_manager.Create(settings.GetHTTPSHost(), settings.GetHTTPSPort(), settings.GetHTTPSPrivateKey(), settings.GetHTTPSPublicCert())) {
    std::cerr<<"Error creating web server"<<std::endl;
    return false;
  }

  if (settings.GetRunningInContainer()) {
    while (true) {
      util::msleep(500);
    }
  } else {
    std::cout<<"Press enter to shutdown the server"<<std::endl;
    (void)getc(stdin);
  }

  std::cout<<"Shutting down server"<<std::endl;
  if (!web_server_manager.Destroy()) {
    std::cerr<<"Error destroying web server"<<std::endl;
    return false;
  }

  std::cout<<"Server has been shutdown"<<std::endl;
  return true;
}

}

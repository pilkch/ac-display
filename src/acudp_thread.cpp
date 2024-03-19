#include <functional>
#include <iostream>
#include <string>
#include <thread>

#include "ac_data.h"
#include "acudp_thread.h"
#include "util.h"

namespace {

void print_handshake_response(const acudp_setup_response& response)
{
  std::cout<<"Response:"<<std::endl;
  std::cout<<"  "<<response.car_name<<std::endl;
  std::cout<<"  "<<response.driver_name<<std::endl;
  std::cout<<"  "<<response.identifier<<std::endl;
  std::cout<<"  "<<response.version<<std::endl;
  std::cout<<"  "<<response.track_name<<std::endl;
  std::cout<<"  "<<response.track_config<<std::endl;
}

void print_car_info(const acudp_car_t& car)
{
  std::cout<<"Car info:"<<std::endl;
  std::cout<<"  identifier: "<<car.identifier<<std::endl;
  std::cout<<"  size: "<<car.size<<std::endl;
  std::cout<<"  speed_kmh: "<<car.speed_kmh<<std::endl;
  std::cout<<"  lap_time: "<<car.lap_time<<std::endl;
  std::cout<<"  car_position_normalized: "<<car.car_position_normalized<<std::endl;
  std::cout<<"  lap_count: "<<car.lap_count<<std::endl;
  std::cout<<"  engine_rpm: "<<car.engine_rpm<<std::endl;
  std::cout<<"  gear: "<<car.gear<<std::endl;
  std::cout<<"  gas: "<<car.gas<<std::endl;
  std::cout<<"  brake: "<<car.brake<<std::endl;
  std::cout<<"  clutch: "<<car.clutch<<std::endl;
}

}

namespace acdisplay {

class cACUDPThread {
public:
  cACUDPThread(const util::cIPAddress& ip_address, uint16_t port);

  bool HandshakeAndSubscribe();

  void MainLoop();

private:
  acudp::ACUDP acudp;
};

cACUDPThread::cACUDPThread(const util::cIPAddress& ip_address, uint16_t port) :
  acudp(util::ToString(ip_address).c_str(), port)
{
}

bool cACUDPThread::HandshakeAndSubscribe()
{
  // Connect to server and perform handshake
  std::cout<<"cACUDPThread::HandshakeAndSubscribe Sending handshake"<<std::endl;
  acudp_setup_response response = acudp.send_handshake();

  print_handshake_response(response);

  // Subscribe to car info events
  acudp.subscribe(acudp::SubscribeMode::update);

  return true;
}

void cACUDPThread::MainLoop()
{
  std::cout<<"cACUDPThread::MainLoop"<<std::endl;

  while (true) {
    auto car = acudp.read_update_event();

    //print_car_info(car);

    // Update the shared rpm value
    std::lock_guard<std::mutex> lock(mutex_ac_data);
    ac_data.gear = car.gear;
    ac_data.accelerator_0_to_1 = car.gas;
    ac_data.brake_0_to_1 = car.brake;
    ac_data.clutch_0_to_1 = car.clutch;
    ac_data.rpm = car.engine_rpm;
    ac_data.speed_kmh = car.speed_kmh;
    ac_data.lap_time_ms = car.lap_time;
    ac_data.last_lap_ms = car.last_lap;
    ac_data.best_lap_ms = car.best_lap;
    ac_data.lap_count = car.lap_count;
  }
}

// Not the most elegant method, but it works
int RunThreadFunction(void* pData)
{
  if (pData == nullptr) {
    return 1;
  }

  cACUDPThread* pThis = static_cast<cACUDPThread*>(pData);
  if (pThis == nullptr) {
    return 1;
  }

  std::cout<<"RunThreadFunction Calling MainLoop"<<std::endl;
  pThis->MainLoop();
  std::cout<<"RunThreadFunction MainLoop returned"<<std::endl;

  return 0;
}

bool StartACUDPThread(const util::cIPAddress& ip_address, uint16_t port)
{
  std::cout<<"StartACUDPThread Connecting to server "<<util::ToString(ip_address)<<":"<<port<<std::endl;

  // Ok we have successfully connected and subscribed so now we can start the thread to read updates
  cACUDPThread* pACUDPThread = new cACUDPThread(ip_address, port);
  if (pACUDPThread == nullptr) {
    std::cerr<<"StartACUDPThread Error creating ACUDP thread, returning false"<<std::endl;
    return false;
  }

  if (!pACUDPThread->HandshakeAndSubscribe()) {
    std::cerr<<"Error handshaking and subscribing"<<std::endl;
    return false;
  }

  // Start the thread
  // NOTE: We never release this, it is ugly, but we don't shut down gracefully. We could create a regular object, then give the thread a signal to stop, then have the thread exit gracefully
  std::thread* pThread = new std::thread(std::bind(&RunThreadFunction, pACUDPThread));
  (void)pThread;

  return true;
}

}

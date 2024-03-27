#include <memory.h>

#include <functional>
#include <iostream>
#include <span>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <acudp.hpp>

#include "util.h"

namespace {

enum class POLL_READ_RESULT {
  ERROR,
  DATA_READY,
  TIMED_OUT
};

class poll_read {
public:
  explicit poll_read(int fd);

  POLL_READ_RESULT poll(uint32_t timeout_ms);

private:
  struct pollfd fds;
};

poll_read::poll_read(int fd)
{
  // Monitor the fd for input
  fds.fd = fd;
  fds.events = POLLIN;
  fds.revents = 0;
}

POLL_READ_RESULT poll_read::poll(uint32_t timeout_ms)
{
  fds.revents = 0;

  const int result = ::poll(&fds, 1, timeout_ms);
  if (result < 0) {
    return POLL_READ_RESULT::ERROR;
  } else if (result > 0) {
    if ((fds.revents & POLLIN) != 0) {
      // Zero it out so we can reuse it for the next call to poll
      fds.revents = 0;
      return POLL_READ_RESULT::DATA_READY;
    }
  }

  return POLL_READ_RESULT::TIMED_OUT;
}


const std::string host("127.0.0.1");
const uint16_t port = 14327;

class UDPSocket {
public:
  UDPSocket() :
    sockfd(-1)
  {
  }

  bool Open()
  {
    Close();

    sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd <= 0) {
      return false;
    }
  
    // Allow reusing the socket (Enables us to bind to the UDP port multiple times)
    const int value = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));

    sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET; // ipv4
    inet_pton(AF_INET, host.c_str(), &(server_address.sin_addr.s_addr));
    server_address.sin_port = htons(port);

    const int result = bind(sockfd, (struct sockaddr*)&server_address, sizeof(struct sockaddr));
    if (result < 0) {
      return false;
    }

    return (sockfd >= 0);
  }

  void Close()
  {
    if (sockfd != -1) {
      ::close(sockfd);
      sockfd = -1;
    }
  }

  constexpr int GetFD() const
  {
    return sockfd;
  }

  ssize_t ReceiveFrom(std::span<char> buffer)
  {
    //sockaddr_in source;
    //source.sin_family = AF_INET;
    //source.sin_port = htons(port);
    //source.sin_addr.s_addr = inet_addr(host.c_str());

    const int result = ::recvfrom(sockfd, buffer.data(), buffer.size(), 0, nullptr, nullptr);
    return result;
  }

  bool SendTo(std::span<char const> const buffer)
  {
    sockaddr_in destination;
    memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET; // ipv4
    inet_pton(AF_INET, host.c_str(), &(destination.sin_addr.s_addr));
    destination.sin_port = htons(port);

    const int result = ::sendto(sockfd, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&destination), sizeof(destination));
    return (result == int(buffer.size()));
  }

private:
  int sockfd;
};


std::atomic<bool> is_server_ready = false;

void ACUDPServerThread(std::span<char const> const fuzz_data)
{
  UDPSocket s;

  if (!s.Open()) {
    std::cerr<<"ACUDPServerThread Error opening socket"<<std::endl;
    return;
  }

  poll_read p(s.GetFD());

  is_server_ready = true;

  const uint32_t timeout_ms = 1000;
  const POLL_READ_RESULT result = p.poll(timeout_ms);
  if (result == POLL_READ_RESULT::DATA_READY) {
    std::cout<<"ACUDPServerThread Data is ready"<<std::endl;

    acudp_setup_t handshake;
    memset(&handshake, 0, sizeof(handshake));
    const ssize_t nbytes_read = s.ReceiveFrom(std::span<char>{(char*)&handshake, sizeof(handshake)});
    std::cout<<"ACUDPServerThread Read "<<nbytes_read<<" bytes"<<std::endl;
    std::cout<<"identifier: "<<handshake.identifier<<", version: "<<handshake.version<<", operation_id: "<<handshake.operation_id<<std::endl;

    acudp_setup_response_t response;
    memset(&response, 0, sizeof(response));

    strcpy(response.car_name, "Porsche 959");
    strcpy(response.driver_name, "Miss Daisy");
    response.identifier = 1;
    response.version = 1;
    strcpy(response.track_name, "Brands Hatch");
    strcpy(response.track_config, "brands_hatch.ini");

    while (true) {
      util::msleep(500);

      std::cout<<"ACUDPServerThread Writing response"<<std::endl;
      s.SendTo(std::span<char const>{(const char*)&response, sizeof(response)});
    }

    std::cout<<"ACUDPServerThread Writing fuzz data"<<std::endl;
    s.SendTo(fuzz_data);
  } else if (result == POLL_READ_RESULT::TIMED_OUT) {
    std::cout<<"ACUDPServerThread Timed out"<<std::endl;
  } else {
    std::cout<<"ACUDPServerThread Error"<<std::endl;
  }

  std::cout<<"ACUDPServerThread returning"<<std::endl;
}

void ACUDPClientThread()
{
  acudp::ACUDP acudp(host.c_str(), port);

  std::cout<<"a"<<std::endl;
  acudp.send_handshake();
  std::cout<<"b"<<std::endl;

  // Subscribe to car info events
  acudp.subscribe(acudp::SubscribeMode::update);
  std::cout<<"c"<<std::endl;

  acudp.read_update_event();
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t data_length)
{
  const char* data_as_char = (const char*)data;
  std::thread t(&ACUDPServerThread, std::span{data_as_char, data_length});

  // Wait for the server to be ready
  while (!is_server_ready) {
    util::msleep(20);
  }

  std::thread thread_acudp_client(&ACUDPClientThread);

  std::cout<<"Joining first thread"<<std::endl;
  t.join();

  // Wait for the client to crash
  util::msleep(1000);

  std::cout<<"Joining second thread"<<std::endl;
  thread_acudp_client.join();

  std::cout<<"returning 0"<<std::endl;

  return 0;
}

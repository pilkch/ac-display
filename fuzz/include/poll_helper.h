#pragma once

#include <poll.h>

enum class POLL_READ_RESULT {
  ERROR,
  DATA_READY,
  TIMED_OUT
};

class poll_read {
public:
  explicit poll_read(int fd);

  POLL_READ_RESULT poll(int timeout_ms);

private:
  struct pollfd fds;
};

inline poll_read::poll_read(int fd)
{
  // Monitor the fd for input
  fds.fd = fd;
  fds.events = POLLIN;
  fds.revents = 0;
}

inline POLL_READ_RESULT poll_read::poll(int timeout_ms)
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

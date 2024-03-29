#pragma once

#include <optional>

namespace curl {

class cCurlHelper {
public:
  cCurlHelper();
  ~cCurlHelper();
};


bool PerformRequest(std::string_view url, std::string_view user_agent, const std::optional<std::string>& self_signed_certificate_path, std::ostringstream& output);

}

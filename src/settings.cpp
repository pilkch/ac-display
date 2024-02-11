#include <climits>
#include <cstring>

#include <limits>
#include <iostream>
#include <filesystem>

#include <pwd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <json-c/json.h>

#include "json.h"
#include "settings.h"
#include "util.h"

namespace application {

bool cSettings::LoadFromFile(const std::string& sFilePath)
{
  Clear();

  const size_t nMaxFileSizeBytes = 20 * 1024;
  std::string contents;
  if (!util::ReadFileIntoString(sFilePath, nMaxFileSizeBytes, contents)) {
    std::cerr<<"File \""<<sFilePath<<"\" not found"<<std::endl;
    return false;
  }

  util::cJSONDocument document(json_tokener_parse(contents.c_str()));
  if (!document.IsValid()) {
    std::cerr<<"Invalid JSON config \""<<sFilePath<<"\""<<std::endl;
    return false;
  }

  // Parse the JSON tree

  // Parse "settings"
  json_object_object_foreach(document.Get(), settings_key, settings_val) {
    enum json_type type_settings = json_object_get_type(settings_val);
    if ((type_settings != json_type_object) || (strcmp(settings_key, "settings") != 0)) {
      std::cerr<<"settings object not found"<<std::endl;
      return false;
    }

    // Parse address
    {
      struct json_object* address_obj = json_object_object_get(settings_val, "address");
      if (address_obj == nullptr) {
        std::cerr<<"address not found"<<std::endl;
        return false;
      }

      enum json_type address_type = json_object_get_type(address_obj);
      if (address_type != json_type_string) {
        std::cerr<<"address is not a string"<<std::endl;
        return false;
      }

      const char* value = json_object_get_string(address_obj);
      if (value == nullptr) {
        std::cerr<<"address is not valid"<<std::endl;
        return false;
      }

      util::ParseAddress(value, address);
    }

    // Parse port
    {
      struct json_object* port_obj = json_object_object_get(settings_val, "port");
      if (port_obj == nullptr) {
        std::cerr<<"port not found"<<std::endl;
        return false;
      }

      enum json_type port_type = json_object_get_type(port_obj);
      if (port_type != json_type_int) {
        std::cerr<<"port is not an int"<<std::endl;
        return false;
      }

      const int value = json_object_get_int(port_obj);
      if ((value <= 0) || (value > USHRT_MAX)) {
        std::cerr<<"port is not valid"<<std::endl;
        return false;
      }

      port = value;
    }
  }

  return IsValid();
}

bool cSettings::IsValid() const
{
  return (address.IsValid() && (port != 0));
}

void cSettings::Clear()
{
  address.Clear();
  port = 0;
}

}

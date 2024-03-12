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

namespace {

bool JSONParseString(struct json_object* json, const std::string& name, std::string& out_value)
{
  out_value.clear();

  struct json_object* obj = json_object_object_get(json, name.c_str());
  if (obj == nullptr) {
    std::cerr<<name<<" not found"<<std::endl;
    return false;
  }

  enum json_type type = json_object_get_type(obj);
  if (type != json_type_string) {
    std::cerr<<name<<" is not a string"<<std::endl;
    return false;
  }

  const char* value = json_object_get_string(obj);
  if (value == nullptr) {
    std::cerr<<name<<" is not valid"<<std::endl;
    return false;
  }

  out_value = value;

  return true;
}

bool JSONParseUint16(struct json_object* json, const std::string& name, uint16_t& out_value)
{
  out_value = 0;

  struct json_object* obj = json_object_object_get(json, name.c_str());
  if (obj == nullptr) {
    std::cerr<<name<<" not found"<<std::endl;
    return false;
  }

  enum json_type type = json_object_get_type(obj);
  if (type != json_type_int) {
    std::cerr<<name<<" is not an int"<<std::endl;
    return false;
  }

  const int value = json_object_get_int(obj);
  if ((value <= 0) || (value > USHRT_MAX)) {
    std::cerr<<name<<" is not valid"<<std::endl;
    return false;
  }

  out_value = value;
  return true;
}

}

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

    // Parse acudp address
    {
      std::string value;
      if (!JSONParseString(settings_val, "acudp_host", value)) {
        return false;
      }

      util::ParseAddress(value, acudp_host);
    }

    // Parse acudp port
    {
      uint16_t value = 0;
      if (!JSONParseUint16(settings_val, "acudp_port", value)) {
        return false;
      }

      acudp_port = value;
    }

    // Parse https address
    {
      std::string value;
      if (!JSONParseString(settings_val, "https_host", value)) {
        return false;
      }

      util::ParseAddress(value, https_host);
    }

    // Parse https port
    {
      uint16_t value = 0;
      if (!JSONParseUint16(settings_val, "https_port", value)) {
        return false;
      }

      https_port = value;
    }

    // Parse https private key and certificate
    if (!JSONParseString(settings_val, "https_private_key", https_private_key)) {
      return false;
    }

    if (!JSONParseString(settings_val, "https_public_cert", https_public_cert)) {
      return false;
    }
  }

  return IsValid();
}

bool cSettings::IsValid() const
{
  return (
    acudp_host.IsValid() && (acudp_port != 0) &&
    https_host.IsValid() && (https_port != 0) &&
    !https_private_key.empty() && !https_public_cert.empty()
  );
}

void cSettings::Clear()
{
  acudp_host.Clear();
  acudp_port = 0;
  https_host.Clear();
  https_port = 0;
  https_private_key.clear();
  https_public_cert.clear();
}

}

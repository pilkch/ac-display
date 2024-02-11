#pragma once

#include <string_view>

namespace util {

std::string GetHomeFolder();
std::string GetConfigFolder(std::string_view sApplicationNameLower);
bool TestFileExists(const std::string& sFilePath);

bool ReadFileIntoString(const std::string& sFilePath, size_t nMaxFileSizeBytes, std::string& out_contents);

}

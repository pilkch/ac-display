#pragma once

#include <cstdint>

#include <string_view>

namespace util {

template <class T>
inline constexpr T clamp(const T& i, const T& lower, const T& upper)
{
  return (i < lower) ? lower : (i > upper) ? upper : i;
}

// msleep(): Sleep for the requested number of milliseconds
int msleep(long msec);

// Get the time since epoch in milliseconds
uint64_t GetTimeMS();

std::string GetHomeFolder();
std::string GetConfigFolder(std::string_view sApplicationNameLower);
bool TestFileExists(const std::string& sFilePath);
bool TestFolderExists(const std::string& sFolderPath);

bool ReadFileIntoString(const std::string& sFilePath, size_t nMaxFileSizeBytes, std::string& out_contents);

}

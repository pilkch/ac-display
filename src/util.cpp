#include <cerrno>
#include <ctime>

#include <iomanip>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>

#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace util {

std::string GetHomeFolder()
{
  const char* szHomeFolder = getenv("HOME");
  if (szHomeFolder != nullptr) return szHomeFolder;

  struct passwd* pPasswd = getpwuid(getuid());
  if (pPasswd != nullptr) return pPasswd->pw_dir;

  return "";
}

std::string GetConfigFolder(std::string_view sApplicationNameLower)
{
  const std::string sHomeFolder = GetHomeFolder();
  if (sHomeFolder.empty()) return "";

  return sHomeFolder + "/.config/" + std::string(sApplicationNameLower);
}

bool TestFileExists(const std::string& sFilePath)
{
  struct stat s;
  return (stat(sFilePath.c_str(), &s) >= 0);
}

size_t GetFileSizeBytes(const std::string& sFilePath)
{
  struct stat s;
  if (stat(sFilePath.c_str(), &s) < 0) return 0;

  return s.st_size;
}

bool ReadFileIntoString(const std::string& sFilePath, size_t nMaxFileSizeBytes, std::string& contents)
{
  if (!TestFileExists(sFilePath)) {
    std::cerr<<"File \""<<sFilePath<<"\" not found"<<std::endl;
    return false;
  }

  const size_t nFileSizeBytes = GetFileSizeBytes(sFilePath);
  if (nFileSizeBytes == 0) {
    std::cerr<<"Empty file \""<<sFilePath<<"\""<<std::endl;
    return false;
  } else if (nFileSizeBytes > nMaxFileSizeBytes) {
    std::cerr<<"File \""<<sFilePath<<"\" is too large"<<std::endl;
    return false;
  }

  std::ifstream f(sFilePath);

  contents.reserve(nFileSizeBytes);

  contents.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());

  return true;
}

}

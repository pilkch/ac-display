#include <iostream>

#include <sysexits.h>

#include "settings.h"
#include "ac_display.h"
#include "version.h"

namespace application {

void PrintUsage()
{
  std::cout<<"Usage: ./ac-display [OPTION]"<<std::endl;
  std::cout<<std::endl;
  std::cout<<"  -v, --version   Print the version and exit"<<std::endl;
}

void PrintVersion()
{
  std::cout<<"ac-display version "<<version<<std::endl;
}

}

int main(int argc, char* argv[])
{
  if (argc == 2) {
    const std::string argument(argv[1]);
    if ((argument == "-v") || (argument == "--version")) {
      application::PrintVersion();
      return EXIT_SUCCESS;
    } else {
      // Unknown argument, print the usage and exit
      application::PrintUsage();
      return EX_USAGE;
    }
  } else if (argc != 1) {
    // Incorrect number of arguments, print the usage and exit
    application::PrintUsage();
    return EX_USAGE;
  }


  // Parse the configuration file
  application::cSettings settings;
  if (!settings.LoadFromFile("./configuration.json")) {
    std::cerr<<"Error parsing configuration.json"<<std::endl;
    return EXIT_FAILURE;
  }

  const bool result = acdisplay::RunServer(settings);

  return (result ? EXIT_SUCCESS : EXIT_FAILURE);
}

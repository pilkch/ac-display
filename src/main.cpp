#include <iostream>

#include <sysexits.h>

#include "ac_data.h"
#include "ac_display.h"
#include "settings.h"
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


  {
    // Update the car configuration
    // NOTE: Assetto Corsa doesn't provide any of these values so we have to make them up, I think AC expects you to be on the same machine and look it up in that car's config file?
    // TODO: It might be nicer to put this in a car config file? Or allow the user to set it on the web page itself?
    std::lock_guard<std::mutex> lock(mutex_ac_data);
    ac_data.config_rpm_red_line = 6000.0f;
    ac_data.config_rpm_maximum = 7500.0f;
    ac_data.config_speedometer_red_line_kph = 250.0f;
    ac_data.config_speedometer_maximum_kph = 300.0f;
  }

  const bool result = acdisplay::RunServer(settings);

  return (result ? EXIT_SUCCESS : EXIT_FAILURE);
}

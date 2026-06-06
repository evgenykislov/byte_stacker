#include "settings.h"

#include <fstream>
#include <iostream>

#include "parser.h"

const std::string kLogOutlinkPacketPrefix = "log.outlink=";
const std::string kSettingsHelp = R"(
log.outlink=true|false - enable logging of all outlink packets with timemark. Default: false
)";


void DefaultSettings(Settings& cfg) { cfg.LogOutlinkPacket = false; }

bool LoadSettings(std::filesystem::path cfg_file, Settings& cfg) {
  std::ifstream f(cfg_file);

  if (!f) {
    std::cerr << "Failed open cfg file '" << cfg_file << "'" << std::endl;
    return false;
  }

  std::string line;
  std::string v;
  while (std::getline(f, line)) {
    if (CheckPrefix(kLogOutlinkPacketPrefix, line, v)) {
      if (!ParseBool(v, cfg.LogOutlinkPacket)) {
        std::cerr << "Config: bad value '" << line << "'" << std::endl;
        return false;
      }
    }
  }

  return true;
}

void PrintSettingsHelp() { std::cout << kSettingsHelp << std::endl; }

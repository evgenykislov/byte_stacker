#include "settings.h"

#include <fstream>
#include <iostream>

#include "parser.h"
#include "trace.h"

const std::string kLogOutlinkPacketPrefix = "log.outlink=";
const std::string kLogTrunkPacketPrefix = "log.trunk=";
const std::string kLogFormatErrorPrefix = "log.formaterror=";
const std::string kLogResendPrefix = "log.resend=";

const std::string kSettingsHelp = R"(
log.outlink=true|false - enable logging of all outlink packets with timemark. Default: false
log.trunk=true|false - enable logging of valid trunk packets with timemark. Default: false
log.formaterror=true|false - enable logging of data format errors. Default: false
log.resend=true|false - enable logging of packet resending. Default: false
)";

void DefaultOutputLog(std::string message) {
  std::cout << timemark(true) << ": " << message << std::endl;
}


void DefaultSettings(Settings& cfg) { new (&cfg) Settings(); }

bool LoadSettings(std::filesystem::path cfg_file, Settings& cfg) {
  DefaultSettings(cfg);

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
    } else if (CheckPrefix(kLogTrunkPacketPrefix, line, v)) {
      if (!ParseBool(v, cfg.LogTrunkPacket)) {
        std::cerr << "Config: bad value '" << line << "'" << std::endl;
        return false;
      }
    } else if (CheckPrefix(kLogFormatErrorPrefix, line, v)) {
      if (!ParseBool(v, cfg.LogFormatError)) {
        std::cerr << "Config: bad value '" << line << "'" << std::endl;
        return false;
      }
    } else if (CheckPrefix(kLogResendPrefix, line, v)) {
      if (!ParseBool(v, cfg.LogResendPacket)) {
        std::cerr << "Config: bad value '" << line << "'" << std::endl;
        return false;
      }
    }
  }


  return true;
}

void PrintSettingsHelp() { std::cout << kSettingsHelp << std::endl; }

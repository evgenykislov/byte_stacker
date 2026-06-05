#include "settings.h"

void DefaultSettings(Settings& cfg) {}

bool LoadSettings(std::filesystem::path cfg_file, Settings& cfg) {
  return false;
}

void PrintSettingsHelp() {}

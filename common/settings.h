#ifndef SETTINGS_BYTE_STACKER
#define SETTINGS_BYTE_STACKER

#include <filesystem>

/*! Структура с настройками, заданными из командной строки или др. заполнено */
struct Settings {
  bool LogOutlinkPacket; //!< Логировать время посылки/приёма любого пакета с наружными соединениями
  bool LogTrunkPacket; //!< Логировать время пакетов транка
};


bool LoadSettings(std::filesystem::path cfg_file, Settings& cfg);

#endif

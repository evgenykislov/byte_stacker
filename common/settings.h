#ifndef SETTINGS_BYTE_STACKER
#define SETTINGS_BYTE_STACKER

#include <filesystem>

extern void DefaultOutputLog(std::string message);

/*! Структура с настройками, заданными из командной строки или др. заполнено */
struct Settings {
  bool LogOutlinkPacket;  //!< Логировать время посылки/приёма любого пакета с
                          //!< наружными соединениями
  bool LogTrunkPacket;  //!< Логировать время пакетов транка

  // TODO Descr
  // Не может быть nullptr
  void (*OutputLog)(std::string message);

  Settings() {
    LogOutlinkPacket = false;
    LogTrunkPacket = false;
    OutputLog = DefaultOutputLog;
  }

  virtual ~Settings() {}

  Settings(const Settings&) = delete;
  Settings(Settings&&) = delete;
  Settings& operator=(const Settings&) = delete;
  Settings& operator=(Settings&&) = delete;
};


/*! Заполнение дефолтными настройками конфигурационной структуры
\param cfg возвращаемая структура с дефолтными данными*/
void DefaultSettings(Settings& cfg);

/*! Загрузка настроек из конфигурационного файла. Если в файле есть ошибки, то
возвращается признак ошибки
\param cfg_file имя конфигурационного файла
\param cfg возвращаемая структура с загруженными данными
\return признак успешного разбора конфигурационного файла */
bool LoadSettings(std::filesystem::path cfg_file, Settings& cfg);

/*! Выводит описание всех параметров конфигурационного файла вместе с дефолтными
 * значениями */
void PrintSettingsHelp();

#endif

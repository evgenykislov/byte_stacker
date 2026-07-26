/*! \file Запуск клиентской части как консольного приложения */

#include "byte_stacker_in.h"

#include "parser.h"
#include "settings.h"

namespace bai = boost::asio::ip;

const std::string kLocalPrefix = "--local";
const std::string kTrunkPrefix = "--trunk=";
const std::string kSettingsPrefix = "--settings=";


void PrintHelp() {
  std::cout << "\
Utility byte_stacker_in\n\
Usage:\n\
byte_stacker_in --local1=ip:port [--local2=ip:port ...]\n\
    --trunk=ip:port1,port2... [--settings=file-name]\n\
\n\
Options:\n\
  --settings speficify file name with settings\n\
  ";
}


int main(int argc, char** argv) {
  if (argc <= 1) {
    PrintHelp();
    return 1;
  }

  std::map<unsigned int, bai::tcp::endpoint>
      lps;  //!< Локальные точки для приёма подключений
  std::vector<bai::udp::endpoint> trp;  //!< Транковые точки для запроса данных
  Settings cfg;  //!< Настройки программы из конфигурационного файла
  DefaultSettings(cfg);

  // Разбор аргументов командной строки
  for (int i = 1; i < argc; ++i) {
    std::string a(argv[i]);
    std::string v;

    if (a.starts_with(kLocalPrefix)) {
      bai::tcp::endpoint ep;
      unsigned int id;
      if (ParsePoint(a.substr(kLocalPrefix.size()), id, ep)) {
        lps[id] = ep;
      } else {
        return 2;
      }
    } else if (a.starts_with(kTrunkPrefix)) {
      if (!ParseTrunkPoint(a.substr(kTrunkPrefix.size()), trp)) {
        return 2;
      }
    } else if (CheckPrefix(kSettingsPrefix, a, v)) {
      std::filesystem::path p(v);
      if (!LoadSettings(std::filesystem::path(v), cfg)) {
        DefaultSettings(cfg);
        std::wcerr
            << "WARNING: settings file contains some errors. Use default values"
            << std::endl;
      }
    } else {
      std::cerr << "ERROR: Unknown argument '" << a << "'" << std::endl;
      return 2;
    }
  }

  if (lps.empty()) {
    std::wcerr << "WARNING: There are no local point" << std::endl;
    return 3;
  }

  if (trp.empty()) {
    std::wcerr << "WARNING: There are no trunk point" << std::endl;
    return 3;
  }

  return RunClient(lps, trp, cfg);
}

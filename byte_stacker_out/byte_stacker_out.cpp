// byte_stacker_out.cpp : Source file for your target.
//

#include "byte_stacker_out.h"

#include <iostream>
#include <map>

#include "parser.h"
#include "trunklink.h"

namespace bai = boost::asio::ip;

const std::string kExternalPrefix = "--external";
const std::string kTrunkPrefix = "--trunk=";


void PrintHelp() {
  std::cout << "byte_stacker_out" << std::endl;
  std::cout << "byte_stacker_out --external1=ip:port [--external2=ip:port ...] "
               "--trunk=ip:port1,port2..."
            << std::endl;
  // TODO Добавить описание
}


int main(int argc, char** argv) {
  if (argc <= 1) {
    PrintHelp();
    return 1;
  }

  std::map<PointID, bai::tcp::endpoint> eps;  //!< Внешние точки коннекта
  std::vector<bai::udp::endpoint> trp;  //!< Транковые точки для обмена данными

  for (int i = 1; i < argc; ++i) {
    std::string a(argv[i]);

    if (a.starts_with(kExternalPrefix)) {
      bai::tcp::endpoint ep;
      PointID id;
      if (ParsePoint(a.substr(kExternalPrefix.size()), id, ep)) {
        eps[id] = ep;
      } else {
        return 2;
      }
    } else if (a.starts_with(kTrunkPrefix)) {
      if (!ParseTrunkPoint(a, trp)) {
        return 2;
      }
    }
  }

  if (eps.empty()) {
    std::wcerr << "Внимание: Не задано ни одной точки выхода" << std::endl;
    return 3;
  }

  if (trp.empty()) {
    std::wcerr << "Внимание: Не задано ни одной точки передачи" << std::endl;
    return 3;
  }


  // Проверка транковых соединений
  // TODO

  // Получаем соединения
  // TODO
  try {
    boost::asio::io_context ctx(4 /* TODO */);
    TrunkServer trs(ctx /*, trp */);

    boost::asio::signal_set signals(ctx, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { ctx.stop(); });
    /*
        for (auto& p : lps) {
          boost::asio::co_spawn(
              ctx, ListenLocalPoint(trc, p.first, p.second),
       boost::asio::detached);
        }
    */
    ctx.run();
  } catch (std::exception& err) {
    std::printf("Exception: %s\n", err.what());
  }

  return 0;
}

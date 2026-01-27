// byte_stacker_out.cpp : Source file for your target.
//

#include "byte_stacker_out.h"

#include <iostream>
#include <map>

#include "outlink.h"
#include "parser.h"
#include "trunklink.h"

namespace bai = boost::asio::ip;

const std::string kExternalPrefix = "--external";
const std::string kTrunkPrefix = "--trunk=";
const size_t kPoolSize = 4;
const int kInformationInterval = 10000;


void PrintHelp() {
  std::cout << "byte_stacker_out" << std::endl;
  std::cout << "byte_stacker_out --external1=ip:port [--external2=ip:port ...] "
               "--trunk=ip:port1,port2..."
            << std::endl;
}


int main(int argc, char** argv) {
  if (argc <= 1) {
    PrintHelp();
    return 1;
  }

  std::map<PointID, AddressPortPoint> eps;  //!< Внешние точки коннекта
  std::vector<bai::udp::endpoint> trp;  //!< Транковые точки для обмена данными

  for (int i = 1; i < argc; ++i) {
    std::string a(argv[i]);

    if (a.starts_with(kExternalPrefix)) {
      PointID id;
      AddressPortPoint ep;
      if (ParsePoint(
              a.substr(kExternalPrefix.size()), id, ep.Address, ep.Port)) {
        eps[id] = ep;
      } else {
        return 2;
      }
    } else if (a.starts_with(kTrunkPrefix)) {
      if (!ParseTrunkPoint(a.substr(kTrunkPrefix.size()), trp)) {
        return 2;
      }
    }
  }

  if (eps.empty()) {
    std::wcerr << "Needs to specify some external points" << std::endl;
    return 3;
  }

  if (trp.empty()) {
    std::wcerr << "Needs to specify some trunk points" << std::endl;
    return 3;
  }


  try {
    boost::asio::io_context ctx;
    TrunkServer trs(
        ctx, trp, [&eps, &ctx](PointID point) -> std::shared_ptr<OutLink> {
          auto it = eps.find(point);
          if (it == eps.end()) {
            return nullptr;
          }

          try {
            return std::make_shared<OutLink>(
                ctx, it->second.Address, it->second.Port);
          } catch (...) {
          }
          return nullptr;
        });

    std::atomic_bool stop_flag = false;
    boost::asio::signal_set signals(ctx, SIGINT, SIGTERM);
    signals.async_wait([&ctx, &stop_flag](auto, auto) {
      stop_flag = true;
      ctx.stop();
    });
    /*
        for (auto& p : lps) {
          boost::asio::co_spawn(
              ctx, ListenLocalPoint(trc, p.first, p.second),
       boost::asio::detached);
        }
    */

    // Запустим потоки обработки сети
    std::vector<std::thread> pool;
    for (size_t i = 0; i < kPoolSize; ++i) {
      std::thread t([&ctx](){
        ctx.run();
      });
      pool.push_back(std::move(t));
    }

    // Вывод полезной информации
    while (!stop_flag) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kInformationInterval));

      auto stat = trs.GetStat();
      std::printf("-----\nOut: %u kByte, In: %u kByte, Cnt: %zu\n", (unsigned int)(stat.StreamToOutLinks / 1024),
        (unsigned int)(stat.StreamFromOutLinks / 1024), stat.ConnectAmount);
    }

    // Остановим все потоки
    for (auto& item : pool) {
      if (item.joinable()) {
        item.join();
      }
    }
  } catch (std::exception& err) {
    std::printf("Exception: %s\n", err.what());
  }

  return 0;
}

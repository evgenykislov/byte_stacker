// byte_stacker_in.cpp : Defines the entry point for the application.
//

#include "byte_stacker_in.h"

#include <map>
#include <vector>
#include <utility>

#include <boost/asio.hpp>

#include "inlink.h"
#include "outlink.h"
#include "parser.h"
#include "trunklink.h"

namespace bai = boost::asio::ip;
namespace this_coro = boost::asio::this_coro;

const std::string kLocalPrefix = "--local";
const std::string kTrunkPrefix = "--trunk=";
const size_t kPoolSize = 4;
const int kInformationInterval = 10000;


void PrintHelp() {
  std::cout << "byte_stacker_in" << std::endl;
  std::cout << "byte_stacker_in --local1=ip:port [--local2=ip:port ...] "
               "--trunk=ip:port1,port2..."
            << std::endl;
}


/*! Регистрируем новое соединение с подключенным сокетом
\param trc клиент транковой связи
\param id идентификатор точки подключения (может быть несколько подключений для
одной и той-же точки)
\param socket подключенный tcp сокет новоко соединения */
void RegisterConnect(TrunkClient& trc, PointID id, bai::tcp::socket&& socket) {
  ConnectID cnt;
  assert(cnt.is_nil());

  try {
    auto ol = OutLink::CreateOutLink(std::move(socket));
    trc.AddConnect(id, ol);
  } catch (std::exception&) {
    // Незарегистрировали. Просто выходим
  }
}


/*! Функция слушает одну локальную точку, устанавливает соединения через неё.
Функция асинхронная через сопрограммы boost:asio
\param tpc клиент транковой связи (фактически глобальный экземпляр)
\param id идентификатор точки
\param point точка для установки соединений
\return объект-ожидание для работы в среде asio */
boost::asio::awaitable<void> ListenLocalPoint(
    TrunkClient& trc, PointID id, boost::asio::ip::tcp::endpoint point) {
  auto executor = co_await this_coro::executor;
  bai::tcp::acceptor acceptor(executor, point);
  for (;;) {
    bai::tcp::socket socket =
        co_await acceptor.async_accept(boost::asio::use_awaitable);
    RegisterConnect(trc, id, std::move(socket));
  }
}


int main(int argc, char** argv) {
  if (argc <= 1) {
    PrintHelp();
    return 1;
  }

  std::map<PointID, bai::tcp::endpoint>
      lps;  //!< Локальные точки для приёма подключений
  std::vector<bai::udp::endpoint> trp;  //!< Транковые точки для запроса данных

  for (int i = 1; i < argc; ++i) {
    std::string a(argv[i]);

    if (a.starts_with(kLocalPrefix)) {
      bai::tcp::endpoint ep;
      PointID id;
      if (ParsePoint(a.substr(kLocalPrefix.size()), id, ep)) {
        lps[id] = ep;
      } else {
        return 2;
      }
    } else if (a.starts_with(kTrunkPrefix)) {
      if (!ParseTrunkPoint(a.substr(kTrunkPrefix.size()), trp)) {
        return 2;
      }
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

  try {
    boost::asio::io_context ctx;
    TrunkClient trc(ctx, trp);

    std::atomic_bool stop_flag = false;
    boost::asio::signal_set signals(ctx, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) {
      stop_flag = true;
      ctx.stop();
    });

    for (auto& p : lps) {
      boost::asio::co_spawn(
          ctx, ListenLocalPoint(trc, p.first, p.second), boost::asio::detached);
    }

    // Запустим потоки обработки сети
    std::vector<std::thread> pool;
    for (size_t i = 0; i < kPoolSize; ++i) {
      std::thread t([&ctx]() { ctx.run(); });
      pool.push_back(std::move(t));
    }

    // Вывод полезной информации
    while (!stop_flag) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(kInformationInterval));

      auto stat = trc.GetStat();
      std::printf("-----\nOut: %u kByte, In: %u kByte, Cnt: %zu\n",
          (unsigned int)(stat.StreamToOutLinks / 1024),
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

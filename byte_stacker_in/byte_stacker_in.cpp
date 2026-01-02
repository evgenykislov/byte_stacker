// byte_stacker_in.cpp : Defines the entry point for the application.
//

#include "byte_stacker_in.h"

#include <map>
#include <vector>
#include <utility>

#include <boost/asio.hpp>

#include "inlink.h"
#include "trunklink.h"

namespace bai = boost::asio::ip;
namespace this_coro = boost::asio::this_coro;

const std::string kLocalPrefix = "--local";
const std::string kTrunkPrefix = "--trunk=";
const size_t kChunkSize = 800;


void PrintHelp() {
  std::cout << "byte_stacker_in" << std::endl;
  std::cout << "byte_stacker_in --local1=ip:port [--local2=ip:port ...] "
               "--trunk=ip:port1,port2..."
            << std::endl;
  // TODO Добавить описание
}


boost::asio::awaitable<void> ProcessPoint(
    TrunkClient& trc, PointID id, bai::tcp::socket socket) {
  ConnectID cnt;
  assert(cnt.is_nil());

  try {
    cnt = trc.CreateConnect(
        id, [&socket](ConnectID cnt) { socket.close(); },
        [](ConnectID cnt, void* data, size_t data_size) {});
    for (;;) {
      char data[kChunkSize];
      std::size_t n = co_await socket.async_read_some(
          boost::asio::buffer(data), boost::asio::use_awaitable);
    }
  } catch (std::exception&) {
    // Чтение прервано. Просто выходим
  }

  trc.ReleaseConnect(cnt);
}


/*! Функция слушает одну локальную точку, устанавливает соединения через неё.
Функция использует архитектуру boost:asio для асинхронной работы
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
    co_spawn(executor, ProcessPoint(trc, id, std::move(socket)),
        boost::asio::detached);
  }
}


bool ParseLocalPoint(
    std::string arg, PointID& id, boost::asio::ip::tcp::endpoint& point) {
  assert(arg.starts_with(kLocalPrefix));
  auto b = arg.substr(kLocalPrefix.size());
  auto p = b.find('=');
  if (p == std::string::npos || p == 0) {
    std::cerr << "Unknown argument '" << arg << "'" << std::endl;
    return false;
  }
  auto sid = b.substr(0, p);
  try {
    std::size_t s;
    id = static_cast<PointID>(std::stoul(sid, &s));
    if (s != (std::size_t)p) {
      throw std::runtime_error("bad format of id");
    }
  } catch (std::exception&) {
    std::cerr << "Bad format of argument '" << arg << "'" << std::endl;
    return false;
  }

  auto adr = b.substr(p + 1);
  auto p1 = adr.find(':');
  if (p1 == std::string::npos || p1 == 0) {
    std::cerr << "Bad format of address in argument '" << arg << "'"
              << std::endl;
    return false;
  }

  auto sip = adr.substr(0, p1);
  auto sport = adr.substr(p1 + 1);
  try {
    std::size_t s;
    unsigned short iport = (unsigned short)std::stoul(sport, &s);
    if (s != sport.size()) {
      throw std::runtime_error("bad format of port");
    }

    point.address(bai::make_address_v4(sip));
    point.port(iport);
    return true;
  } catch (std::exception&) {
    std::cerr << "Bad format of argument '" << arg << "'" << std::endl;
    return false;
  }
  return false;
}

bool ParseTrunkPoint(
    std::string arg, std::vector<boost::asio::ip::udp::endpoint>& points) {
  assert(arg.starts_with(kTrunkPrefix));
  points.clear();
  auto adr = arg.substr(kTrunkPrefix.size());
  auto p1 = adr.find(':');
  if (p1 == std::string::npos || p1 == 0) {
    std::cerr << "Bad format of address in argument '" << arg << "'"
              << std::endl;
    return false;
  }

  auto sip = adr.substr(0, p1);
  auto sports = adr.substr(p1 + 1);
  try {
    auto ip = bai::make_address_v4(sip);
    while (!sports.empty()) {
      std::string chunk;
      auto p2 = sports.find(',');
      if (p2 == std::string::npos) {
        chunk = sports;
        sports.clear();
      } else {
        chunk = sports.substr(0, p2);
        sports = sports.substr(p2 + 1);
      }

      if (chunk.empty()) {
        std::cerr << "Bad format of port in argument '" << arg << "'"
                  << std::endl;
        return false;
      }

      std::size_t s;
      unsigned short iport = (unsigned short)std::stoul(chunk, &s);
      if (s != chunk.size()) {
        throw std::runtime_error("bad format of port");
      }

      boost::asio::ip::udp::endpoint pt;
      pt.address(ip);
      pt.port(iport);
      points.push_back(pt);
    }
    return true;
  } catch (std::exception&) {
    std::cerr << "Bad format of argument '" << arg << "'" << std::endl;
    return false;
  }
  return false;
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
      if (ParseLocalPoint(a, id, ep)) {
        lps[id] = ep;
      } else {
        return 2;
      }
    } else if (a.starts_with(kTrunkPrefix)) {
      if (!ParseTrunkPoint(a, trp)) {
        return 2;
      }
    }
  }

  if (lps.empty()) {
    std::wcerr << "Внимание: Не задано ни одной точки входа" << std::endl;
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
    TrunkClient trc(ctx);

    boost::asio::signal_set signals(ctx, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { ctx.stop(); });

    for (auto& p : lps) {
      boost::asio::co_spawn(
          ctx, ListenLocalPoint(trc, p.first, p.second), boost::asio::detached);
    }

    ctx.run();
  } catch (std::exception& err) {
    std::printf("Exception: %s\n", err.what());
  }

  return 0;
}

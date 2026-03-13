// byte_stacker_out.cpp : Source file for your target.
//

#include <iostream>
#include <mutex>
#include <utility>
#include <vector>

#include <boost/asio.hpp>

#include "parser.h"


namespace bai = boost::asio::ip;

const size_t kPoolSize = 4;

void PrintHelp() {
  std::cout << "Test utility with udp packet processing" << std::endl;
  std::cout << "Usage:" << std::endl;
  std::cout << "  udp_processing --receive=ip:port --transmit=ip:port"
            << std::endl;
}


int main(int argc, char** argv) {
  if (argc <= 1) {
    PrintHelp();
    return 1;
  }

  bool has_receive_point = false;
  bai::udp::endpoint receive_point;
  bool has_transmit_point = false;
  bai::udp::endpoint transmit_point;

  for (int i = 1; i < argc; ++i) {
    std::string a(argv[i]);
    std::string v;
    if (CheckPrefix("--receive=", a, v)) {
      ParseIpPort(v, receive_point);
      has_receive_point = true;
    } else if (CheckPrefix("--transmit=", a, v)) {
      ParseIpPort(v, transmit_point);
      has_transmit_point = true;
    } else {
      std::cerr << "Unknown argument '" << a << "'" << std::endl;
      return 1;
    }
  }

  if (!has_receive_point || !has_transmit_point) {
    PrintHelp();
    return 1;
  }

  try {
    boost::asio::io_context ctx;
    // Переменная на остановку
    std::condition_variable stop_var;
    bool stop_flag = false;
    std::mutex stop_lock;

    boost::asio::signal_set signals(ctx, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) {
      ctx.stop();
      // Проинформируем об остановке
      std::lock_guard lk(stop_lock);
      stop_flag = true;
      stop_var.notify_all();
    });

    // Запустим потоки обработки сети
    std::vector<std::thread> pool;
    for (size_t i = 0; i < kPoolSize; ++i) {
      std::thread t([&ctx]() { ctx.run(); });
      pool.push_back(std::move(t));
    }

    // Ждём остановки
    std::unique_lock sl(stop_lock);
    stop_var.wait(sl, [&]() { return stop_flag; });

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

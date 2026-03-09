// byte_stacker_out.cpp : Source file for your target.
//

#include <iostream>
#include <mutex>
#include <utility>
#include <vector>

#include <boost/asio.hpp>


namespace bai = boost::asio::ip;

const size_t kPoolSize = 4;


void PrintHelp() {}


int main(int argc, char** argv) {
  if (argc <= 1) {
    PrintHelp();
    return 1;
  }

  std::vector<bai::udp::endpoint> trp;  //!< Транковые точки для обмена данными

  for (int i = 1; i < argc; ++i) {
    std::string a(argv[i]);
  }

  try {
    boost::asio::io_context ctx;
    // Переменная на остановку
    std::condition_variable stop_var;
    bool stop_flag;
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

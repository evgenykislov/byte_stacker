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
#include "settings.h"
#include "trace.h"
#include "tracer.h"
#include "trunklink.h"

namespace bai = boost::asio::ip;
namespace this_coro = boost::asio::this_coro;

const std::string kLocalPrefix = "--local";
const std::string kTrunkPrefix = "--trunk=";
const std::string kSettingsPrefix = "--settings=";
const size_t kPoolSize = 4;
const int kInformationInterval = 1000;

std::mt19937 generator_;


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


/*! Регистрируем новое соединение с подключенным сокетом
\param trc клиент транковой связи
\param id идентификатор точки подключения (может быть несколько подключений для
одной и той-же точки)
\param socket подключенный tcp сокет новоко соединения */
void RegisterNewConnection(TrunkClient& trc, PointID id,
    bai::tcp::socket&& socket, const Settings& cfg, Tracer* tracer) {
  // Сгенерируем идентификатор
  uuids::uuid_random_generator gen{generator_};
  uuids::uuid cnt = gen();

  if (tracer) {
    tracer->CreateTrace(cnt);
  }

  try {
    auto ol = OutLink::CreateOutLink(cnt, std::move(socket), cfg, tracer);
    trc.AddConnect(id, ol);
  } catch (std::exception&) {
    // Незарегистрировали. Просто выходим
  }
}


/*! Запрос на подключение по указанному акцептору. Функция сама себя вызывает
в бесконечном цикле, пока работает сетевой контекст (до завершения приложения)
\param ctx сетевой контекст
\param acp акцептор, уже привязанный к нужному адресу и порту
\param trc клиентский обработчик, в котором регистрируются новые соединения
\param id идентификатор точки, задаётся в командной строке */
void RequestAccept(boost::asio::io_context& ctx,
    std::shared_ptr<bai::tcp::acceptor> acp, TrunkClient& trc, PointID id,
    const Settings& cfg, Tracer* tracer) {
  auto socket = std::make_shared<bai::tcp::socket>(ctx);
  acp->async_accept(*socket, [&ctx, &trc, socket, acp, id, &cfg, tracer](
                                 const boost::system::error_code& error) {
    if (!error) {
      // Получили новое соединение. Регистрируем, работаем
      RegisterNewConnection(trc, id, std::move(*socket.get()), cfg, tracer);
    } else if (error == boost::asio::error::connection_aborted) {
      // Соединение пришло и сразу разорвалось. Это некритично. Продолжаем
      // работу
    } else if (error == boost::asio::error::operation_aborted) {
      // Штатно завершаем работу
      return;
    } else {
      // Все остальные ошибки критичные. Выходим
      trlog(
          "ERROR: can't accept to point %u: %s\n", id, error.message().c_str());
      return;
    }

    // Продолжаем принимать новые подключения
    RequestAccept(ctx, acp, trc, id, cfg, tracer);
  });
}


/*! Создание акцептора и запуск его опроса. Если при создании акцептора или
запуске ожидания возникают ошибки, то выдаётся исключение
\param ctx сетевой контекст
\param trc клиентский обработчик, в котором регистрируются новые соединения
\param id идентификатор точки, задаётся в командной строке
\param point точка приёма подключений
\return акцептор, на котором уже ожидаютсмя подключения */
std::shared_ptr<bai::tcp::acceptor> ListenLocalPoint(
    boost::asio::io_context& ctx, TrunkClient& trc, PointID id,
    boost::asio::ip::tcp::endpoint point, const Settings& cfg, Tracer* tracer) {
  auto acceptor = std::make_shared<bai::tcp::acceptor>(ctx, point);
  RequestAccept(ctx, acceptor, trc, id, cfg, tracer);
  return acceptor;
}


int main(int argc, char** argv) {
  int result = 0;

  if (argc <= 1) {
    PrintHelp();
    return 1;
  }

  // Инициализация генератора uuid
  std::random_device rd;
  auto seed_data = std::array<int, std::mt19937::state_size>{};
  std::generate(std::begin(seed_data), std::end(seed_data), std::ref(rd));
  std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
  generator_ = std::mt19937(seq);


  std::map<PointID, bai::tcp::endpoint>
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

  std::shared_ptr<Tracer> tracer;
  if (!cfg.trace_storage_path.empty()) {
    tracer = std::make_shared<Tracer>(
        cfg.trace_storage_path, cfg.trace_completed_path);
  }

  try {
    boost::asio::io_context ctx;
    // Переменная на остановку
    std::condition_variable stop_var;
    bool stop_flag = false;
    std::mutex stop_lock;

    TrunkClient trc(ctx, trp, cfg, tracer.get());

    boost::asio::signal_set signals(ctx, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) {
      // Проинформируем об остановке
      std::lock_guard lk(stop_lock);
      stop_flag = true;
      stop_var.notify_all();
    });

    // Подготовка акцепторов
    std::vector<std::shared_ptr<bai::tcp::acceptor>> acceptors;
    for (auto& p : lps) {
      auto acp =
          ListenLocalPoint(ctx, trc, p.first, p.second, cfg, tracer.get());
      acceptors.push_back(acp);
    }

    // Запустим потоки обработки сети
    std::vector<std::thread> pool;
    for (size_t i = 0; i < kPoolSize; ++i) {
      std::thread t([&ctx]() { ctx.run(); });
      pool.push_back(std::move(t));
    }

    // Вывод полезной информации
    std::unique_lock sl(stop_lock);
    while (
        !stop_var.wait_for(sl, std::chrono::milliseconds(kInformationInterval),
            [&stop_flag]() { return stop_flag; })) {
      auto stat = trc.GetStat();

      // Обязательная часть
      if (stat.no_live) {
        // Есть проблемы с подключением
        tout(": WARNING: Trunk doesn't work! Check internet connection!\n");
      }

      // Дополнительная часть
#if 0
      // Вывод общей статистики
      auto ospeed = (unsigned int)(stat.StreamToOutLinks * 1000 / 1024 /
                                   kInformationInterval);
      auto ispeed = (unsigned int)(stat.StreamFromOutLinks * 1000 / 1024 /
                                   kInformationInterval);
      auto cnt = (unsigned int)(stat.ConnectAmount);
      tout(
          ": FAULT: %8u | Local: %8u kBytes/s | Trunk: %8u kBytes/s | "
          "Connects: %8u | Ping(min,avg,max): %.1f/%.1f/%.1f | Cache: %8u\n",
          stat.FauldPacket, ospeed, ispeed, cnt, stat.MinPing / 1000.0,
          stat.AveragePing / 1000.0, stat.MaxPing / 1000.0, stat.cache_load);
#endif
    }
    sl.unlock();

    // -----------------
    // Останавливаем приложение

    for (auto i : acceptors) {
      boost::system::error_code ec;
      i->close(ec);
      if (ec) {
        trlog("ERROR: can't close acceptance: %s\n", ec.message().c_str());
      }
    }

    // Остановим сетевой контекст и потоки
    ctx.stop();
    for (auto& item : pool) {
      if (item.joinable()) {
        item.join();
      }
    }


  } catch (std::exception& err) {
    std::printf("Exception: %s\n", err.what());
    result = 1;
  }

  return result;
}

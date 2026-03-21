// byte_stacker_out.cpp : Source file for your target.
//

#include <cassert>
#include <iostream>
#include <mutex>
#include <utility>
#include <vector>

#include <boost/asio.hpp>

#include "parser.h"


namespace bai = boost::asio::ip;

const size_t kPoolSize = 4;
const size_t kUndefinedIndex = size_t(-1);
const size_t kReadBufferSize = 2000;
const size_t kMaxPacketSize = 2000;

/*! Структура для описания одного пакета */
struct PacketInfo {
  size_t size_;
  uint8_t data_[kMaxPacketSize];  //!< Данные самого пакета
};

/*! Структура для описания одного "пайпа" между клиентом и сервером */
struct PipeInfo {
  bai::udp::socket SendSocket;  //!< Сокет для отправки пакетов на сервер
  bai::udp::endpoint
      ClientPoint;  //!< Клиентская точка, на которую отправлять пакеты
  std::atomic_flag
      processing_;  //!< Признак, что производится обработка сетевого трафика
  std::mutex
      data_lock_;  //!< Лок на изменение данных: buffer_, ToServer, ToClient
  std::shared_ptr<PacketInfo> buffer_;
  boost::asio::ip::udp::endpoint buffer_point_;
  std::vector<std::shared_ptr<PacketInfo>> ToServer,
      ToClient;  //!< Очереди для пакетов на сервер и на клиента
};

std::vector<std::shared_ptr<PipeInfo>> pipes_;
std::recursive_mutex pipes_lock_;

boost::asio::io_context net_context_;
boost::asio::ip::udp::socket receiver_{
    net_context_};  //!< Сокет для получения пакетов с "клиентской стороны"

uint8_t receiver_buffer_[kReadBufferSize];
boost::asio::ip::udp::endpoint client_holder_;

std::atomic_flag request_processing_;  //!< Флаг на запрос нового "процессинга"
                                       //!< - обработки данных

// TODO Rename
bool has_transmit_point = false;
bai::udp::endpoint transmit_point;


void ProcessPipes();

void PrintHelp() {
  std::cout << "Test utility with udp packet processing" << std::endl;
  std::cout << "Usage:" << std::endl;
  std::cout << "  udp_processing --receive=ip:port --transmit=ip:port"
            << std::endl;
}


size_t GetPipe(bai::udp::endpoint point) {
  std::lock_guard lk(pipes_lock_);
  for (size_t i = 0; i < pipes_.size(); ++i) {
    if (pipes_[i]->ClientPoint == point) {
      return i;
    }
  }

  // Создаём новый канал
  pipes_.emplace_back(
      std::make_shared<PipeInfo>(bai::udp::socket{net_context_}, point));
  assert(!pipes_.empty());
  auto index = pipes_.size() - 1;
  pipes_[index]->processing_.clear();

  return index;
}

void RequestReadPipe(std::shared_ptr<PipeInfo> pipe) {
  std::lock_guard lk(pipe->data_lock_);
  if (!pipe->buffer_) {
    pipe->buffer_ = std::make_shared<PacketInfo>();  // TODO Memory management
  }
  pipe->SendSocket.async_receive_from(
      boost::asio::buffer(pipe->buffer_->data_, sizeof(kMaxPacketSize)),
      pipe->buffer_point_,
      [pipe](boost::system::error_code err, std::size_t data_size) {
        if (err) {
          // TODO Error processing
        } else if (data_size <= kMaxPacketSize) {
          // Получили блок данных
          if (pipe->buffer_point_ == transmit_point) {
            if (!pipe->processing_.test_and_set()) {
              // Это первый запрос с последней обработки
              // Регистрируем обработку
              net_context_.post([]() { ProcessPipes(); });
            }
          }

        } else {
          // Очень большой пакет по udp
          assert(false);
        }

        RequestReadPipe(pipe);
      });
}


void ProcessPipes() {
  request_processing_.clear();

  std::lock_guard lk(pipes_lock_);
  for (auto it = pipes_.begin(); it != pipes_.end(); ++it) {
    // ToService
    for (auto si = (*it)->ToServer.begin(); si != (*it)->ToServer.end(); ++si) {
      auto pack = *si;
      (*it)->SendSocket.async_send_to(
          boost::asio::buffer(pack->data_, pack->size_), (*it)->ClientPoint,
          [pack](
              boost::system::error_code /*ec*/, std::size_t /*bytes_sent*/) {});
    }
    (*it)->ToServer.clear();
  }
}


void RequestReadReceive() {
  receiver_.async_receive_from(
      boost::asio::buffer(receiver_buffer_, sizeof(receiver_buffer_)),
      client_holder_, [](boost::system::error_code err, std::size_t data_size) {
        if (err) {
          // TODO Error processing
        } else if (data_size <= kMaxPacketSize) {
          // Получили из канала блок данных
          std::unique_lock lk(pipes_lock_);
          auto p = GetPipe(client_holder_);
          auto pack =
              std::make_shared<PacketInfo>();  // TODO Check bad_alloc exception
          pack->size_ = data_size;
          memcpy(pack->data_, receiver_buffer_, pack->size_);
          pipes_[p]->ToServer.push_back(pack);
          lk.unlock();

          if (!request_processing_.test_and_set()) {
            // Это первый запрос с последней обработки
            // Регистрируем обработку
            net_context_.post([]() { ProcessPipes(); });
          }
        } else {
          // Очень большой пакет по udp
          assert(false);
        }

        RequestReadReceive();
      });
}


int main(int argc, char** argv) {
  if (argc <= 1) {
    PrintHelp();
    return 1;
  }

  bool has_receive_point = false;
  bai::udp::endpoint receive_point;

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
    // Переменная на остановку
    std::condition_variable stop_var;
    bool stop_flag = false;
    std::mutex stop_lock;

    receiver_ = bai::udp::socket{net_context_, receive_point};

    boost::asio::signal_set signals(net_context_, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) {
      net_context_.stop();
      // Проинформируем об остановке
      std::lock_guard lk(stop_lock);
      stop_flag = true;
      stop_var.notify_all();
    });

    // Запустим получение данных от клиента
    RequestReadReceive();

    // Запустим потоки обработки сети
    std::vector<std::thread> pool;
    for (size_t i = 0; i < kPoolSize; ++i) {
      std::thread t([]() { net_context_.run(); });
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

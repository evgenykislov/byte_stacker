// byte_stacker_out.cpp : Source file for your target.
//

#include <cassert>
#include <iostream>
#include <mutex>
#include <utility>
#include <vector>

#include <boost/asio.hpp>

#include "parser.h"
#include "processor.h"
#include "processor_sender.h"


namespace bai = boost::asio::ip;

const size_t kPoolSize = 4;
const size_t kUndefinedIndex = size_t(-1);
const size_t kReadBufferSize = 2000;

const std::string kProcessorsTypes[] = {"--skip=", "--delay=", "--skip_connection="};


struct ProcInfo {
  std::string prefix;
  std::string value;
};


/*! Структура для описания одного "пайпа" между клиентом и сервером */
struct PipeInfo {
  std::mutex
      data_lock_;  //!< Лок на изменение данных: buffer_, ToServer, ToClient
  boost::asio::ip::udp::endpoint buffer_point_;

  PipeInfo(boost::asio::io_context& ctx, bai::udp::socket& client_socket,
      bai::udp::endpoint client_point, bai::udp::endpoint server_point,
      const std::vector<ProcInfo>& procs)
      : SendSocket(ctx, server_point.protocol()), ClientPoint(client_point) {
    auto s2s = std::make_shared<ProcessorSender>(SendSocket, server_point);
    auto s2c = std::make_shared<ProcessorSender>(client_socket, client_point);

    ToServerChain = s2s;
    ToClientChain = s2c;

    for (auto it = procs.rbegin(); it != procs.rend(); ++it) {
      ToServerChain =
          CreateProcessor(ctx, ToServerChain, it->prefix, it->value);
      ToClientChain =
          CreateProcessor(ctx, ToClientChain, it->prefix, it->value);
      if (!ToServerChain || !ToClientChain) {
        std::cerr << "ERROR: can't create processor for prefix " << it->prefix
                  << std::endl;
        throw std::runtime_error("Wrong processor");
      }
    }
  }

  bai::udp::socket& GetSendSocket() { return SendSocket; }
  ProcessorPtr GetServerChain() { return ToServerChain; }
  ProcessorPtr GetClientChain() { return ToClientChain; }
  bai::udp::endpoint GetClientPoint() { return ClientPoint; }

 private:
  bai::udp::socket
      SendSocket;  //!< Сокет для отправки пакетов на сервер // TODO rename
  bai::udp::endpoint
      ClientPoint;  //!< Клиентская точка, на которую отправлять пакеты

  ProcessorPtr
      ToServerChain;  //!< Цепочка процессоров для отправки данных на сервер
  ProcessorPtr
      ToClientChain;  //!< Цепочка процессоров для отправки данных на клиент
};


std::vector<std::shared_ptr<PipeInfo>> pipes_;
std::recursive_mutex pipes_lock_;

std::vector<ProcInfo> processors_;

boost::asio::io_context net_context_;
boost::asio::ip::udp::socket receiver_{
    net_context_};  //!< Сокет для получения пакетов с "клиентской стороны"

uint8_t receiver_buffer_[kReadBufferSize];
boost::asio::ip::udp::endpoint client_holder_;


// TODO Rename
bool has_transmit_point = false;
bai::udp::endpoint transmit_point;

void RequestReadPipe(std::shared_ptr<PipeInfo> pipe);
void ProcessToServer();
void ProcessToClient(std::shared_ptr<PipeInfo> pipe);

void PrintHelp() {
  std::cout << "Test utility with udp packet processing" << std::endl;
  std::cout << "Usage:" << std::endl;
  std::cout << "  udp_processing --receive=ip:port --transmit=ip:port "
               "[--delay=value_ms] [--skip=n] [--skip_connection=n]"
            << std::endl;
  std::cout << "    --delay add delay to each packet" << std::endl;
  std::cout << "    --skip skip each n-th packet" << std::endl;
  std::cout << "    --skip_connection skip first n packets for connection create" << std::endl;
}


size_t GetPipe(bai::udp::endpoint point) {
  std::lock_guard lk(pipes_lock_);
  for (size_t i = 0; i < pipes_.size(); ++i) {
    if (pipes_[i]->GetClientPoint() == point) {
      return i;
    }
  }

  // Создаём новый канал
  auto p = std::make_shared<PipeInfo>(
      net_context_, receiver_, point, transmit_point, processors_);

  pipes_.push_back(p);

  RequestReadPipe(p);

  assert(!pipes_.empty());
  return pipes_.size() - 1;
}

void RequestReadPipe(std::shared_ptr<PipeInfo> pipe) {
  auto pack = std::make_shared<PacketInfo>();  // TODO Check bad_alloc exception
  pipe->GetSendSocket().async_receive_from(
      boost::asio::buffer(pack->data_, kMaxPacketSize), pipe->buffer_point_,
      [pipe, pack](boost::system::error_code err, std::size_t data_size) {
        if (err) {
          // TODO Error processing
        } else if (data_size <= kMaxPacketSize) {
          // Получили блок данных
          if (pipe->buffer_point_ == transmit_point) {
            pack->size_ = data_size;
            pipe->GetClientChain()->Push(pack);
          }
        } else {
          // Очень большой пакет по udp
          assert(false);
        }

        RequestReadPipe(pipe);
      });
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
          pipes_[p]->GetServerChain()->Push(pack);
          lk.unlock();
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
      // Проверим процессоры
      bool found = false;
      for (auto& i : kProcessorsTypes) {
        if (CheckPrefix(i, a, v)) {
          found = true;
          processors_.push_back({i, v});
          break;
        }
      }

      if (!found) {
        std::cerr << "Unknown argument '" << a << "'" << std::endl;
        return 1;
      }
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

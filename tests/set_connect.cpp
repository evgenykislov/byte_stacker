/*******************************************************************************
 * TCP/IP Connection Forwarding Test (Boost.Asio + Boost.Process)
 *
 * Описание:
 * Данный тест проверяет функциональность переадресации TCP/IP соединений
 * между двумя адресами с использованием асинхронного сетевого программирования.
 *
 * Проверяемая функциональность:
 * - Тест запускает два внешних приложения через Boost.Process
 * - Создает асинхронный TCP-сервер на адресе address_to
 * - Устанавливает асинхронное TCP-соединение с адресом address_from
 * - Проверяет, что в течение 1 секунды после установки соединения с
 *   address_from, происходит входящее подключение к address_to
 *
 * Критерии успеха:
 * - PASS: Если в течение 1 секунды после подключения к address_from
 *   произошло подключение к серверу на address_to
 * - FAIL: Если в течение 1 секунды подключение к address_to не произошло
 *
 * Используемые технологии:
 * - GoogleTest для фреймворка тестирования
 * - Boost.Asio для асинхронной работы с сетью
 * - Boost.Process для управления процессами
 * - C++20 standard
 *
 * Зависимости:
 * - Boost (версия 1.70 или выше рекомендуется)
 * - GoogleTest
 *
 * Примечания:
 * - По завершению теста запущенные приложения корректно останавливаются
 *   через сигнал SIGINT/SIGTERM (Ctrl+C)
 * - Асинхронные операции управляются через boost::asio::io_context
 ******************************************************************************/

#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <memory>
#include <optional>
#include <iostream>
#include <atomic>

#include "fixture_direct_pipe.h"

namespace asio = boost::asio;
namespace process = boost::process;
using tcp = asio::ip::tcp;

//==============================================================================
// Структура для хранения информации об адресе
//==============================================================================
struct AddressInfo {
  std::string ip;
  uint16_t port;
};

//==============================================================================
// Вспомогательная функция для парсинга IP:PORT
//==============================================================================
AddressInfo parseAddress(const std::string& address) {
  size_t colonPos = address.find(':');
  if (colonPos == std::string::npos) {
    return {"127.0.0.1", 0};
  }

  AddressInfo info;
  info.ip = address.substr(0, colonPos);
  info.port = static_cast<uint16_t>(std::stoi(address.substr(colonPos + 1)));
  return info;
}

//==============================================================================
// Класс для асинхронного TCP-сервера
//==============================================================================
class AsyncTcpServer {
 public:
  AsyncTcpServer(
      asio::io_context& io_context, const std::string& ip, uint16_t port)
      : io_context_(io_context),
        acceptor_(io_context, tcp::endpoint(asio::ip::make_address(ip), port)),
        connection_accepted_(false) {}

  // Асинхронное ожидание подключения с таймаутом
  void asyncAccept(std::chrono::milliseconds timeout,
      std::function<void(bool, std::shared_ptr<tcp::socket>)> callback) {
    auto socket = std::make_shared<tcp::socket>(io_context_);
    auto timer = std::make_shared<asio::steady_timer>(io_context_, timeout);

    // Флаг для предотвращения двойного вызова callback
    auto completed = std::make_shared<std::atomic<bool>>(false);

    // Асинхронный accept
    acceptor_.async_accept(*socket, [this, socket, timer, callback, completed](
                                        const boost::system::error_code& ec) {
      if (completed->exchange(true)) {
        return;  // Уже обработано таймером
      }

      timer->cancel();

      if (!ec) {
        connection_accepted_ = true;
        callback(true, socket);
      } else {
        callback(false, nullptr);
      }
    });

    // Таймер
    timer->async_wait([this, socket, callback, completed](
                          const boost::system::error_code& ec) {
      if (completed->exchange(true)) {
        return;  // Уже обработано accept
      }

      if (ec == asio::error::operation_aborted) {
        return;  // Таймер отменен, значит accept успешен
      }

      // Таймаут истек
      acceptor_.cancel();
      callback(false, nullptr);
    });
  }

  bool isConnectionAccepted() const { return connection_accepted_; }

 private:
  asio::io_context& io_context_;
  tcp::acceptor acceptor_;
  std::atomic<bool> connection_accepted_;
};

//==============================================================================
// Класс для асинхронного TCP-клиента
//==============================================================================
class AsyncTcpClient {
 public:
  AsyncTcpClient(asio::io_context& io_context)
      : io_context_(io_context), socket_(io_context), connected_(false) {}

  // Асинхронное подключение
  void asyncConnect(const std::string& ip, uint16_t port,
      std::function<void(bool)> callback) {
    tcp::endpoint endpoint(asio::ip::make_address(ip), port);

    socket_.async_connect(
        endpoint, [this, callback](const boost::system::error_code& ec) {
          if (!ec) {
            connected_ = true;
            callback(true);
          } else {
            callback(false);
          }
        });
  }

  bool isConnected() const { return connected_; }

  void close() {
    if (socket_.is_open()) {
      boost::system::error_code ec;
      socket_.close(ec);
    }
  }

 private:
  asio::io_context& io_context_;
  tcp::socket socket_;
  std::atomic<bool> connected_;
};

//==============================================================================
// Основной тест
//==============================================================================
TEST_F(DirectPipe, ConnectionForwardingTest) {
  // Адреса для тестирования
  // Замените на реальные значения или передавайте через параметры
  const std::string address_from = "127.0.0.2:30001";  // Куда подключаемся
  const std::string address_to = "127.0.0.2:50001";  // Где ожидаем подключение

  // Парсим адреса
  AddressInfo addr_from = parseAddress(address_from);
  AddressInfo addr_to = parseAddress(address_to);

  // Создаем io_context для асинхронных операций
  asio::io_context io_context;

  // Флаги для отслеживания состояния
  std::atomic<bool> server_accepted(false);
  std::atomic<bool> client_connected(false);
  std::shared_ptr<tcp::socket> accepted_socket;

  // Создаем асинхронный сервер на address_to
  AsyncTcpServer server(io_context, addr_to.ip, addr_to.port);

  // Запускаем асинхронное ожидание подключения (с таймаутом 1 секунда)
  constexpr auto timeout = std::chrono::milliseconds(1000);

  server.asyncAccept(timeout, [&server_accepted, &accepted_socket](bool success,
                                  std::shared_ptr<tcp::socket> socket) {
    if (success) {
      server_accepted = true;
      accepted_socket = socket;
      std::cout << "Server attaches connection to address_to" << std::endl;
    } else {
      std::cout << "Connecting time is out" << std::endl;
    }
  });

  // Создаем асинхронный клиент
  AsyncTcpClient client(io_context);

  // Запоминаем время начала
  auto startTime = std::chrono::steady_clock::now();

  // Подключаемся к address_from
  client.asyncConnect(addr_from.ip, addr_from.port,
      [&client_connected, &address_from](bool success) {
        if (success) {
          client_connected = true;
          std::cout << "Client connected to address_from: " << address_from
                    << std::endl;
        } else {
          std::cout << "Can't connect to address_from: "
                    << address_from << std::endl;
        }
      });

  // Запускаем io_context в отдельном потоке
  std::thread io_thread([&io_context]() { io_context.run(); });

  // Ждем завершения всех асинхронных операций
  io_thread.join();

  auto endTime = std::chrono::steady_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime)
          .count();

  // Проверяем результаты
  ASSERT_TRUE(client_connected.load())
      << "Failed to connect to address_from: " << address_from;

  ASSERT_TRUE(server_accepted.load())
      << "In time of " << elapsed << " ms after connecting to " << address_from
      << " thereisn't connect to " << address_to;

  // Закрываем соединения
  if (accepted_socket && accepted_socket->is_open()) {
    boost::system::error_code ec;
    accepted_socket->close(ec);
  }
  client.close();

  // Тест успешен
  std::cout << "Test succeess!" << std::endl;
  std::cout << "  Connection time: " << elapsed << " ms"
            << std::endl;
  std::cout << "  Connection to " << address_from
            << " where connected to " << address_to << std::endl;
}

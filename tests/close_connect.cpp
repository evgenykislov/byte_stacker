/**
 * @file tcp_connection_forwarding_test.cpp
 * @brief Тест для проверки корректной переадресации TCP соединений
 *
 * Описание теста:
 * ===============
 * Тест проверяет, что при разрыве клиентского соединения с адресом
 * address_from, автоматически разрывается связанное с ним соединение на адресе
 * address_to. Это типичная ситуация для TCP прокси-серверов, мостов или
 * туннелей.
 *
 * Сценарий теста:
 * ===============
 * 1. Тест устанавливает TCP соединение (как клиент) с адресом address_from
 * 2. Тест запускает TCP сервер на адресе address_to и ожидает входящего
 * соединения
 * 3. Проверяется, что в течение 2 секунд происходит подключение к address_to
 *    (это означает, что прокси/мост корректно переадресовал соединение)
 * 4. Тест разрывает клиентское соединение с address_from
 * 5. Проверяется, что в течение 1 секунды после разрыва клиентского соединения
 *    также разрывается соединение на стороне address_to
 * 6. Если соединение разорвалось корректно - тест успешен
 *
 * Требования:
 * ===========
 * - C++20
 * - Google Test
 * - Boost.Asio (асинхронный режим)
 * - Кроссплатформенность (Windows, Linux, macOS)
 *
 * Примечания:
 * ===========
 * Тест предполагает наличие работающего TCP прокси/моста между address_from и
 * address_to. Если такой компонент отсутствует, тест не пройдет на этапе
 * ожидания подключения к address_to.
 */

#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>
#include <chrono>
#include <memory>
#include <thread>
#include <atomic>
#include <future>

#include "fixture_direct_pipe.h"

using namespace boost::asio;
using namespace std::chrono_literals;

namespace {

/**
 * @test TcpConnectionForwarding
 * @brief Основной тест переадресации TCP соединений
 */
TEST_F(TcpForwardingTest, ConnectionClosePropagation) {
  // Настройка адресов для теста
  // В реальном сценарии эти адреса должны быть сконфигурированы
  // для конкретного прокси/моста, который тестируется
  const std::string address_from_host = "127.0.0.2";
  const uint16_t address_from_port = 30001;  // Порт прокси-сервера (вход)

  const std::string address_to_host = "127.0.0.2";
  const uint16_t address_to_port = 50001;  // Порт целевого сервера (выход)

  // Флаги состояния
  std::atomic<bool> server_accepted{false};
  std::atomic<bool> server_disconnected{false};
  std::promise<void> accept_promise;
  std::promise<void> disconnect_promise;
  auto accept_future = accept_promise.get_future();
  auto disconnect_future = disconnect_promise.get_future();


  // Шаг 1: Создаем TCP сервер на address_to для приема переадресованного
  // соединения
  ip::tcp::acceptor acceptor(io_ctx_);
  ip::tcp::endpoint server_endpoint(
      ip::address::from_string(address_to_host), address_to_port);

  boost::system::error_code ec;
  acceptor.open(server_endpoint.protocol(), ec);
  ASSERT_FALSE(ec) << "Failed to open acceptor: " << ec.message();

  acceptor.set_option(ip::tcp::acceptor::reuse_address(true), ec);
  ASSERT_FALSE(ec) << "Failed to set SO_REUSEADDR: " << ec.message();

  acceptor.bind(server_endpoint, ec);
  ASSERT_FALSE(ec) << "Failed to bind to " << address_to_host << ":"
                   << address_to_port << " - " << ec.message();

  acceptor.listen(socket_base::max_listen_connections, ec);
  ASSERT_FALSE(ec) << "Failed to listen: " << ec.message();

  // Создаем socket для принятия соединения
  auto accepted_socket = std::make_shared<ip::tcp::socket>(io_ctx_);

  // Асинхронное принятие соединения
  acceptor.async_accept(
      *accepted_socket, [&](const boost::system::error_code& error) {
        if (!error) {
          server_accepted = true;
          accept_promise.set_value();

          // Запускаем асинхронное чтение для обнаружения разрыва соединения
          auto buffer = std::make_shared<std::array<char, 1>>();
          async_read(*accepted_socket, boost::asio::buffer(*buffer),
              [&, buffer, accepted_socket](
                  const boost::system::error_code& read_error, std::size_t) {
                // EOF или ошибка означает разрыв соединения
                if (read_error) {
                  server_disconnected = true;
                  disconnect_promise.set_value();
                }
              });
        } else {
          accept_promise.set_exception(std::make_exception_ptr(
              std::runtime_error("Accept failed: " + error.message())));
        }
      });

  // Шаг 2: Подключаемся как клиент к address_from
  ip::tcp::socket client_socket(io_ctx_);
  ip::tcp::endpoint client_endpoint(
      ip::address::from_string(address_from_host), address_from_port);

  std::promise<void> connect_promise;
  auto connect_future = connect_promise.get_future();

  client_socket.async_connect(
      client_endpoint, [&](const boost::system::error_code& error) {
        if (error) {
          connect_promise.set_exception(
              std::make_exception_ptr(std::runtime_error(
                  "Connect to address_from failed: " + error.message())));
        } else {
          connect_promise.set_value();
        }
      });

  // Ожидаем подключения к address_from
  auto connect_status = connect_future.wait_for(2s);
  ASSERT_EQ(connect_status, std::future_status::ready)
      << "Failed to connect to address_from (" << address_from_host << ":"
      << address_from_port << ") within 2 seconds";

  ASSERT_NO_THROW(connect_future.get()) << "Connection to address_from failed";

  // Шаг 3: Ожидаем подключения к address_to (переадресация прокси)
  auto accept_status = accept_future.wait_for(2s);
  ASSERT_EQ(accept_status, std::future_status::ready)
      << "No incoming connection to address_to (" << address_to_host << ":"
      << address_to_port << ") within 2 seconds. "
      << "Check if proxy/bridge is running and configured correctly.";

  ASSERT_NO_THROW(accept_future.get())
      << "Failed to accept connection on address_to";

  ASSERT_TRUE(server_accepted)
      << "Server did not accept connection on address_to";

  // Шаг 4: Разрываем клиентское соединение с address_from
  client_socket.close(ec);
  ASSERT_FALSE(ec) << "Failed to close client connection: " << ec.message();

  // Шаг 5: Проверяем, что соединение на address_to тоже разорвалось
  auto disconnect_status = disconnect_future.wait_for(1s);
  ASSERT_EQ(disconnect_status, std::future_status::ready)
      << "Connection on address_to was not closed within 1 second "
      << "after closing connection to address_from. "
      << "Proxy/bridge did not propagate connection closure.";

  ASSERT_NO_THROW(disconnect_future.get());

  ASSERT_TRUE(server_disconnected)
      << "Server connection was not closed after client disconnected";

  // Очистка
  accepted_socket->close(ec);
  acceptor.close(ec);
}

}  // anonymous namespace

/**
Тест проверки передачи всех данных

Описание:
- запускаются все приложения "прямого подключения" (через фикстуру);
- запускается ожидание подключения/чтения на адрес address_to;
- тест подключается к адресу address_from;
- как только произошло подключение - передаётся один байт данных (случайный);
- как только байт передан - соединение сразу же закрывается;
- в результате выполнения теста должно произойти подключение к адресу
address_to и получен один байт данных, равный переданному выше;
- после получения байта данных соединение должно быть закрыто. */

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


void ReadData(std::shared_ptr<ip::tcp::socket> socket, char* buffer,
    size_t buffer_size, std::promise<size_t>* result_size, size_t counter = 0) {
  async_read(*socket,
      boost::asio::buffer(buffer + counter, buffer_size - counter),
      [&, buffer, buffer_size, socket, result_size, counter](
          const boost::system::error_code& read_error, std::size_t data_size) {
        auto nc = counter + data_size;
        if (read_error || nc == buffer_size) {
          result_size->set_value(nc);
        } else {
          ReadData(socket, buffer, buffer_size, result_size, nc);
        }
      });
}

TEST_F(DirectPipe, SingleByteTransfer) {
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
  auto out_buffer = std::make_shared<std::array<char, 100>>();

  std::promise<size_t> received_size;
  auto f_received_size = received_size.get_future();

  // Асинхронное принятие соединения
  acceptor.async_accept(
      *accepted_socket, [&](const boost::system::error_code& error) {
        if (!error) {
          server_accepted = true;
          accept_promise.set_value();

          ReadData(accepted_socket, out_buffer->data(), out_buffer->size(),
              &received_size);
        }
      });

  // Шаг 2: Подключаемся как клиент к address_from
  ip::tcp::socket client_socket(io_ctx_);
  ip::tcp::endpoint client_endpoint(
      ip::address::from_string(address_from_host), address_from_port);


  uint8_t target_data =
      0xaa;  //!< Некоторое тестовое значение. Можно выбрать любое

  client_socket.async_connect(
      client_endpoint, [&](const boost::system::error_code& error) {
        if (!error) {
          client_socket.async_write_some(boost::asio::buffer(&target_data, 1),
              [&](boost::system::error_code const& error,
                  std::size_t bytes_transferred) {
                if (!error) {
                  client_socket.close();
                }
              });
        }
      });

  auto received_status = f_received_size.wait_for(2s);
  ASSERT_EQ(received_status, std::future_status::ready)
      << "Failed to receive data in timeout";
  auto received_value = f_received_size.get();
  ASSERT_EQ(received_value, 1) << "Wrong size of received value";
  ASSERT_EQ((*out_buffer)[0], target_data) << "Slop transferring";

  // Очистка
  accepted_socket->close(ec);
  acceptor.close(ec);
}

}  // anonymous namespace

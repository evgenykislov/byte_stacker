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
    AsyncTcpServer(asio::io_context& io_context, const std::string& ip, uint16_t port)
        : io_context_(io_context),
          acceptor_(io_context, tcp::endpoint(asio::ip::make_address(ip), port)),
          connection_accepted_(false) {
    }
    
    // Асинхронное ожидание подключения с таймаутом
    void asyncAccept(std::chrono::milliseconds timeout, 
                     std::function<void(bool, std::shared_ptr<tcp::socket>)> callback) {
        auto socket = std::make_shared<tcp::socket>(io_context_);
        auto timer = std::make_shared<asio::steady_timer>(io_context_, timeout);
        
        // Флаг для предотвращения двойного вызова callback
        auto completed = std::make_shared<std::atomic<bool>>(false);
        
        // Асинхронный accept
        acceptor_.async_accept(*socket, 
            [this, socket, timer, callback, completed](const boost::system::error_code& ec) {
                if (completed->exchange(true)) {
                    return; // Уже обработано таймером
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
        timer->async_wait(
            [this, socket, callback, completed](const boost::system::error_code& ec) {
                if (completed->exchange(true)) {
                    return; // Уже обработано accept
                }
                
                if (ec == asio::error::operation_aborted) {
                    return; // Таймер отменен, значит accept успешен
                }
                
                // Таймаут истек
                acceptor_.cancel();
                callback(false, nullptr);
            });
    }
    
    bool isConnectionAccepted() const {
        return connection_accepted_;
    }
    
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
        : io_context_(io_context),
          socket_(io_context),
          connected_(false) {
    }
    
    // Асинхронное подключение
    void asyncConnect(const std::string& ip, uint16_t port,
                      std::function<void(bool)> callback) {
        tcp::endpoint endpoint(asio::ip::make_address(ip), port);
        
        socket_.async_connect(endpoint,
            [this, callback](const boost::system::error_code& ec) {
                if (!ec) {
                    connected_ = true;
                    callback(true);
                } else {
                    callback(false);
                }
            });
    }
    
    bool isConnected() const {
        return connected_;
    }
    
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
// Фикстура для тестирования с управлением процессами
//==============================================================================
class TcpForwardingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Инициализация (процессы запускаются в самом тесте)
    }
    
    void TearDown() override {
        // Останавливаем процессы
        stopProcess(process1_);
        stopProcess(process2_);
    }
    
    // Запуск первого приложения
    bool startFirstApplication(const std::string& executable, 
                               const std::vector<std::string>& args) {
        try {
            process1_ = std::make_unique<process::child>(
                executable,
                process::args(args),
                process::std_out > process::null,
                process::std_err > process::null
            );
            
            // Даем процессу время на запуск
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            return process1_ && process1_->valid() && process1_->running();
        } catch (const std::exception& e) {
            std::cerr << "Ошибка запуска первого приложения: " << e.what() << std::endl;
            return false;
        }
    }
    
    // Запуск второго приложения
    bool startSecondApplication(const std::string& executable,
                                const std::vector<std::string>& args) {
        try {
            process2_ = std::make_unique<process::child>(
                executable,
                process::args(args),
                process::std_out > process::null,
                process::std_err > process::null
            );
            
            // Даем процессу время на запуск
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            return process2_ && process2_->valid() && process2_->running();
        } catch (const std::exception& e) {
            std::cerr << "Ошибка запуска второго приложения: " << e.what() << std::endl;
            return false;
        }
    }
    
private:
    void stopProcess(std::unique_ptr<process::child>& proc) {
        if (!proc || !proc->valid()) {
            return;
        }
        
        try {
            if (proc->running()) {
                // Посылаем сигнал завершения (SIGTERM/SIGINT)
#ifdef _WIN32
                proc->terminate();
#else
                // На Unix отправляем SIGINT (Ctrl+C)
                ::kill(proc->id(), SIGINT);
#endif
                
                // Ждем завершения процесса с таймаутом
                bool exited = false;
                for (int i = 0; i < 50 && !exited; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    exited = !proc->running();
                }
                
                // Если процесс не завершился, принудительно убиваем
                if (!exited && proc->running()) {
                    proc->terminate();
                }
                
                // Ждем окончательного завершения
                if (proc->running()) {
                    proc->wait();
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Ошибка при остановке процесса: " << e.what() << std::endl;
        }
    }
    
    std::unique_ptr<process::child> process1_;
    std::unique_ptr<process::child> process2_;
};

//==============================================================================
// Основной тест
//==============================================================================
TEST_F(TcpForwardingTest, ConnectionForwardingTest) {
    // Адреса для тестирования
    // Замените на реальные значения или передавайте через параметры
    const std::string address_from = "127.0.0.1:8080";  // Куда подключаемся
    const std::string address_to = "127.0.0.1:9090";    // Где ожидаем подключение
    
    // Параметры для запуска приложений
    // ВАЖНО: Замените на реальные пути и параметры ваших приложений
    const std::string app1_executable = "./app1";
    const std::vector<std::string> app1_args = {"--port", "8080"};
    
    const std::string app2_executable = "./app2";
    const std::vector<std::string> app2_args = {"--port", "9090"};
    
    // Запускаем приложения
    ASSERT_TRUE(startFirstApplication(app1_executable, app1_args)) 
        << "Не удалось запустить первое приложение";
    
    ASSERT_TRUE(startSecondApplication(app2_executable, app2_args))
        << "Не удалось запустить второе приложение";
    
    // Даем приложениям время на инициализацию
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
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
    
    server.asyncAccept(timeout, 
        [&server_accepted, &accepted_socket](bool success, std::shared_ptr<tcp::socket> socket) {
            if (success) {
                server_accepted = true;
                accepted_socket = socket;
                std::cout << "✓ Сервер принял подключение на address_to" << std::endl;
            } else {
                std::cout << "✗ Время ожидания подключения истекло" << std::endl;
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
                std::cout << "✓ Клиент подключился к address_from: " 
                          << address_from << std::endl;
            } else {
                std::cout << "✗ Не удалось подключиться к address_from: " 
                          << address_from << std::endl;
            }
        });
    
    // Запускаем io_context в отдельном потоке
    std::thread io_thread([&io_context]() {
        io_context.run();
    });
    
    // Ждем завершения всех асинхронных операций
    io_thread.join();
    
    auto endTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();
    
    // Проверяем результаты
    ASSERT_TRUE(client_connected.load())
        << "Не удалось подключиться к address_from: " << address_from;
    
    ASSERT_TRUE(server_accepted.load())
        << "В течение " << elapsed << " мс после подключения к " << address_from
        << " не произошло подключение к " << address_to;
    
    // Закрываем соединения
    if (accepted_socket && accepted_socket->is_open()) {
        boost::system::error_code ec;
        accepted_socket->close(ec);
    }
    client.close();
    
    // Тест успешен
    std::cout << "\n✓ Тест пройден успешно!" << std::endl;
    std::cout << "  Время установки соединения: " << elapsed << " мс" << std::endl;
    std::cout << "  Подключение к " << address_from 
              << " -> получено подключение на " << address_to << std::endl;
}

//==============================================================================
// Дополнительный тест: проверка таймаута при отсутствии подключения
//==============================================================================
TEST_F(TcpForwardingTest, TimeoutTest) {
    const std::string address_to = "127.0.0.1:19090";  // Порт, на который никто не подключится
    
    AddressInfo addr_to = parseAddress(address_to);
    
    asio::io_context io_context;
    std::atomic<bool> server_accepted(false);
    std::atomic<bool> timeout_triggered(false);
    
    AsyncTcpServer server(io_context, addr_to.ip, addr_to.port);
    
    auto startTime = std::chrono::steady_clock::now();
    
    // Ожидаем подключение с таймаутом 500 мс
    server.asyncAccept(std::chrono::milliseconds(500),
        [&server_accepted, &timeout_triggered](bool success, std::shared_ptr<tcp::socket> socket) {
            if (success) {
                server_accepted = true;
            } else {
                timeout_triggered = true;
            }
        });
    
    std::thread io_thread([&io_context]() {
        io_context.run();
    });
    
    io_thread.join();
    
    auto endTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();
    
    // Проверяем, что сработал таймаут, а не accept
    ASSERT_FALSE(server_accepted.load()) 
        << "Соединение не должно было быть принято";
    
    ASSERT_TRUE(timeout_triggered.load())
        << "Таймаут должен был сработать";
    
    // Проверяем, что таймаут сработал примерно через 500 мс (±100 мс)
    ASSERT_GE(elapsed, 400) << "Таймаут сработал слишком рано";
    ASSERT_LE(elapsed, 700) << "Таймаут сработал слишком поздно";
    
    std::cout << "\n✓ Тест таймаута пройден успешно!" << std::endl;
    std::cout << "  Время срабатывания таймаута: " << elapsed << " мс" << std::endl;
}

//==============================================================================
// Точка входа для тестов
//==============================================================================
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

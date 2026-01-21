#ifndef FIXTURE_DIRECT_PIPE_H
#define FIXTURE_DIRECT_PIPE_H


#include <boost/process.hpp>

#include <gtest/gtest.h>

namespace process = boost::process;


class TcpForwardingTest: public ::testing::Test {
 protected:
  void SetUp() override {
    // Инициализация (процессы запускаются в самом тесте)
    // Параметры для запуска приложений
    // ВАЖНО: Замените на реальные пути и параметры ваших приложений
    const std::string app1_executable = "../byte_stacker_in/byte_stacker_in";
    const std::vector<std::string> app1_args = {
        "--local1=127.0.0.2:30001", "--trunk=127.0.0.2:40001"};

    const std::string app2_executable = "../byte_stacker_out/byte_stacker_out";
    const std::vector<std::string> app2_args = {
        "--external1=127.0.0.2:50001", "--trunk=127.0.0.2:40001"};

    // Запускаем приложения
    ASSERT_TRUE(startFirstApplication(app1_executable, app1_args))
        << "Не удалось запустить первое приложение";

    ASSERT_TRUE(startSecondApplication(app2_executable, app2_args))
        << "Не удалось запустить второе приложение";

    // Даем приложениям время на инициализацию
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "Запущены приложения Direct Pipe" << std::endl;
  }

  void TearDown() override {
    // Останавливаем процессы
    stopProcess(process1_);
    stopProcess(process2_);
  }

  // Запуск первого приложения
  bool startFirstApplication(
      const std::string& executable, const std::vector<std::string>& args) {
    try {
      process1_ = std::make_unique<process::child>(executable,
          process::args(args), process::std_out > process::null,
          process::std_err > process::null);

      // Даем процессу время на запуск
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      return process1_ && process1_->valid() && process1_->running();
    } catch (const std::exception& e) {
      std::cerr << "Ошибка запуска первого приложения: " << e.what()
                << std::endl;
      return false;
    }
  }

  // Запуск второго приложения
  bool startSecondApplication(
      const std::string& executable, const std::vector<std::string>& args) {
    try {
      process2_ = std::make_unique<process::child>(executable,
          process::args(args), process::std_out > process::null,
          process::std_err > process::null);

      // Даем процессу время на запуск
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      return process2_ && process2_->valid() && process2_->running();
    } catch (const std::exception& e) {
      std::cerr << "Ошибка запуска второго приложения: " << e.what()
                << std::endl;
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


#endif  // FIXTURE_DIRECT_PIPE_H

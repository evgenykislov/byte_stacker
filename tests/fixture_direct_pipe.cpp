#include "fixture_direct_pipe.h"


void DirectPipe::SetUp() {
  // Инициализация (процессы запускаются в самом тесте)
  // Запускаем приложения
  ASSERT_TRUE(StartFirstApplication())
      << "Не удалось запустить первое приложение";

  ASSERT_TRUE(StartSecondApplication())
      << "Не удалось запустить второе приложение";

  // Даем приложениям время на инициализацию
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  std::cout << "Запущены приложения Direct Pipe" << std::endl;

  // Создаем work guard чтобы io_context не завершился преждевременно
  work_ = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(io_ctx_.get_executor());

  // Запускаем io_context в отдельном потоке
  io_thread_ = std::thread([this]() { io_ctx_.run(); });
}


void DirectPipe::TearDown() {
  // Останавливаем io_context
  work_.reset();
  io_ctx_.stop();

  if (io_thread_.joinable()) {
    io_thread_.join();
  }


  // Останавливаем процессы
  StopProcess(proc1);
  StopProcess(proc2);
}


bool DirectPipe::StartApplication(
    std::unique_ptr<boost::process::process>& process,
    const std::string& executable, const std::vector<std::string>& args) {
  try {
    process = std::make_unique<boost::process::process>(io_ctx_, executable, args);

    // Даем процессу время на запуск
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    return process && process->running();
  } catch (const std::exception&) {
    return false;
  }
}


void DirectPipe::StopProcess(std::unique_ptr<boost::process::process>& proc) {
  if (!proc || !proc->running()) {
    return;
  }

  try {
    if (proc->running()) {
      // Посылаем сигнал завершения (SIGTERM/SIGINT)
      proc->request_exit();
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

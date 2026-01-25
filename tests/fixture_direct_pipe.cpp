#include "fixture_direct_pipe.h"


void DirectPipe::SetUp() {
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
  ASSERT_TRUE(StartFirstApplication(app1_executable, app1_args))
      << "Не удалось запустить первое приложение";

  ASSERT_TRUE(StartSecondApplication(app2_executable, app2_args))
      << "Не удалось запустить второе приложение";

  // Даем приложениям время на инициализацию
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  std::cout << "Запущены приложения Direct Pipe" << std::endl;

  // Создаем work guard чтобы io_context не завершился преждевременно
  work_ = std::make_unique<boost::asio::io_context::work>(io_ctx_);

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
    std::unique_ptr<boost::process::child>& process,
    const std::string& executable, const std::vector<std::string>& args) {
  try {
    process = std::make_unique<boost::process::child>(executable,
        boost::process::args(args),
        boost::process::std_out > boost::process::null,
        boost::process::std_err > boost::process::null);

    // Даем процессу время на запуск
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    return process && process->valid() && process->running();
  } catch (const std::exception& e) {
    return false;
  }
}


void DirectPipe::StopProcess(std::unique_ptr<boost::process::child>& proc) {
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

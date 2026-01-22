#include "fixture_direct_pipe.h"


void TcpForwardingTest::SetUp() {
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
}


void TcpForwardingTest::TearDown() {
  // Останавливаем процессы
  StopProcess(proc1);
  StopProcess(proc2);
}


bool TcpForwardingTest::StartApplication(
    std::unique_ptr<process::child>& process, const std::string& executable,
    const std::vector<std::string>& args) {
  try {
    process = std::make_unique<process::child>(executable, process::args(args),
        process::std_out > process::null, process::std_err > process::null);

    // Даем процессу время на запуск
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    return process && process->valid() && process->running();
  } catch (const std::exception& e) {
    return false;
  }
}


void TcpForwardingTest::StopProcess(std::unique_ptr<process::child>& proc) {
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

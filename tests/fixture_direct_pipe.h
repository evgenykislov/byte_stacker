#ifndef FIXTURE_DIRECT_PIPE_H
#define FIXTURE_DIRECT_PIPE_H

#include <utility>

#include <boost/asio.hpp>
#include <boost/process.hpp>

#include <gtest/gtest.h>


class DirectPipe: public ::testing::Test {
 protected:
  boost::asio::io_context io_ctx_;

  void SetUp() override;
  void TearDown() override;

  // Запуск первого приложения
  bool StartFirstApplication(
      const std::string& executable, const std::vector<std::string>& args) {
    return StartApplication(proc1, executable, args);
  }

  // Запуск второго приложения
  bool StartSecondApplication(
      const std::string& executable, const std::vector<std::string>& args) {
    return StartApplication(proc2, executable, args);
  }

 private:
  std::unique_ptr<boost::process::child> proc1;
  std::unique_ptr<boost::process::child> proc2;
  std::unique_ptr<boost::asio::io_context::work> work_;
  std::thread io_thread_;

  bool StartApplication(std::unique_ptr<boost::process::child>& programm,
      const std::string& executable, const std::vector<std::string>& args);
  void StopProcess(std::unique_ptr<boost::process::child>& proc);
};


#endif  // FIXTURE_DIRECT_PIPE_H

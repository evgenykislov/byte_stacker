#ifndef FIXTURE_DIRECT_PIPE_H
#define FIXTURE_DIRECT_PIPE_H


#include <boost/process.hpp>

#include <gtest/gtest.h>

namespace process = boost::process;


class TcpForwardingTest: public ::testing::Test {
 protected:
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
  bool StartApplication(std::unique_ptr<process::child>& programm,
      const std::string& executable, const std::vector<std::string>& args);
  void StopProcess(std::unique_ptr<process::child>& proc);

  std::unique_ptr<process::child> proc1;
  std::unique_ptr<process::child> proc2;
};


#endif  // FIXTURE_DIRECT_PIPE_H

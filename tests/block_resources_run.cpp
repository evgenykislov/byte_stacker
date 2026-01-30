/*
Тест для проверки запуска приложений при недостатке ресурсов: занят порт и т.д.

*/


#include <gtest/gtest.h>

#include "fixture_direct_pipe.h"


TEST_F(DirectPipe, BlockedRun) {
  ASSERT_FALSE(StartFirstApplication())
      << "Запуск первого приложения при недостатке ресурсов";

  ASSERT_FALSE(StartSecondApplication())
      << "Запуск второго приложения при недостатке ресурсов";
}

#!/bin/bash

# Тестовый скрипт для проверки отработки ситуации недоступных ресурсов: занятый порт и т.п.
# Тест работает по следующим пунктам:
# - запускает клиента и сервер. Запуск через фикстуру
# - запускает клиентсткую часть. Проверяет, что она завершилась с ошибкой
# - запускает серверную часть. Проверяет, что завершилась с ошибкой
# - Тест пройдён успешно, если при занятых ресурсах и клиентсткая и серверная часть завершаются сразу и с ошибками
#
# Параметры (должны быть установлены вызывающим окружением):
# BIN_PATH - путь к исполняемым файлам
#

# Название теста
BTEST_TEST_NAME="Блокированный запуск"

if [[ -z "${BIN_PATH}" ]]; then
  echo "Test needs BIN_PATH variable point to executable files"
  exit 1
fi


source ./fixture_direct.sh


function test() {
  SUMRES=0
  timeout 1s ${BIN_PATH}/byte_stacker_in --local1=127.0.0.2:30001 --local2=127.0.0.2:30002 --trunk=127.0.0.2:40001
  RES=$?
  if [[ "${RES}" -eq "0" || "${RES}" -eq "124" ]]; then
    echo "Client doesn't check resource availability"
	SUMRES=1
  fi

  timeout 1s ${BIN_PATH}/byte_stacker_out --external1=127.0.0.2:50001 --external2=127.0.0.2:50002 --trunk=127.0.0.2:40001
  RES=$?
  if [[ "${RES}" -eq "0" || "${RES}" -eq "124" ]]; then
    echo "Server doesn't check resource availability"
    SUMRES=1
  fi

  return ${SUMRES}
}

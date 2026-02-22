#!/bin/bash

# Тестовый скрипт для проверки передачи большого блока данных
# Тест работает по следующим пунктам:
# - запускает обе половинки транкового канала через фикстуру
# - запускает серверную часть, которая готова принимать данные и записывать их в файл
# - запускает клиентскую часть, которая сразу же начинает передавать данные по каналу
# - как только клиентская часть завершила передачу, ждём завершения серверной части. Таймаут 5 сек
# - сравниваются исходный файл для передачи и полученный через канал. Если всё одинаково, то тест прошёл успешно
#
# Прим.: большой блок для такого канала - это больше 3 МБайт
# Параметры (должны быть установлены вызывающим окружением):
# BIN_PATH - путь к исполняемым файлам
#

# Название теста
BTEST_TEST_NAME="Передача большого блока"


source ./fixture_direct.sh
source ./generate_file.sh


function test() {
  # Файл с полученными данными
  RECEIVER=-1
  SOURCE_FILE=$(generatefile 5000000)
  DESTINATION_FILE=$(mktemp)
  RES=0
  
  nc -4 -l 127.0.0.2 50001 > ${DESTINATION_FILE} < /dev/null &
  RECEIVER=$!
  if ! ps -p ${RECEIVER} > /dev/null 2>&1 ; then
    echo "receiver hasn't run"
	RES=1
  fi

  if [[ ${RES} == 0 ]]; then
    nc -4 -N 127.0.0.2 30001 < ${SOURCE_FILE}
    timeout 5 tail --pid=${RECEIVER} -f /dev/null

    if ps -p ${RECEIVER} > /dev/null 2>&1 ; then
      echo "receiver hasn't stopped after receiving data"
	  RES=1
    else
      echo "Transferred $(stat -c %s "${DESTINATION_FILE}") bytes"
      RECEIVER=-1
	fi
  fi

  if [[ ${RES} == 0 ]]; then
    if ! cmp -s "${SOURCE_FILE}" ${DESTINATION_FILE} >/dev/null ; then
      echo "Files are different"
	  RES=1
    fi
  fi

  # Освобождение ресурсов
  if [[ ${RECEIVER} != -1 ]]; then
    kill ${RECEIVER}
  fi
  if [[ ${DESTINATION_FILE} != "" ]]; then
    rm -f ${DESTINATION_FILE}
  fi
  if [[ ${SOURCE_FILE} != "" ]]; then
    rm -f ${SOURCE_FILE}
  fi
  
  return ${RES}
}

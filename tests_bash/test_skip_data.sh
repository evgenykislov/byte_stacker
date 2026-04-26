#!/bin/bash

# Тестовый скрипт для проверки передачи данных при потере данных
# Тест работает по следующим пунктам:
# - запускает обе половинки транкового канала и обработчик данных через фикстуру
# - запускает серверную часть, которая готова принимать данные и записывать их в файл
# - запускает клиентскую часть, которая сразу же начинает передавать данные по каналу
# - как только клиентская часть завершила передачу, ждём завершения серверной части. Таймаут 5 сек
# - сравниваются исходный файл для передачи и полученный через канал. Если всё одинаково, то тест прошёл успешно
#
# Параметры (должны быть установлены вызывающим окружением):
# BIN_PATH - путь к исполняемым файлам
#

# Название теста
BTEST_TEST_NAME="Передача с потерей данных"

if [[ -z "${BIN_PATH}" ]]; then
  echo "Test needs BIN_PATH variable point to executable files"
  exit 1
fi

PROCESSORS="--skip=5"

source ./fixture_processor.sh


function test() {
  # Файл с полученными данными
  RECEIVER=-1
  RES=0
  SOURCE_FILE=$(mktemp)
  DESTINATION_FILE=$(mktemp)

  if ! ${BIN_PATH}/file_generator --size=4000000 --file=${SOURCE_FILE} ; then
    echo "can't generate test file"
	RES=1
  fi

  if [[ ${RES} == 0 ]]; then
    nc -4 -l 127.0.0.2 50001 > ${DESTINATION_FILE} < /dev/null &
	RECEIVER=$!
    if ! ps -p ${RECEIVER} > /dev/null 2>&1 ; then
      echo "receiver hasn't run"
	  RES=1
    fi
  fi

  if [[ ${RES} == 0 ]]; then
    nc -4 -N 127.0.0.2 30001 < ${SOURCE_FILE}
    timeout 5 tail --pid=${RECEIVER} -f /dev/null

    if ps -p ${RECEIVER} > /dev/null 2>&1 ; then
      echo "receiver hasn't stopped after receiving data"
	  RES=1
    else
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

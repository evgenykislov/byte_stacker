#!/bin/bash

# Тестовый скрипт для проверки передачи большого блока данных
# В качестве блока данных используется файл с программой byte_stacker_in
# Параметры (должны быть установлены вызывающим окружением):
# BIN_PATH - путь к исполняемым файлам

# Название теста
BTEST_TEST_NAME="Передача большого блока"


source ./fixture_direct.sh


function test() {
  # Файл с полученными данными
  RECEIVER=-1
  RES_FILE=$(mktemp)
  RES=0
  
  nc -4 -l 127.0.0.2 50001 > ${RES_FILE} < /dev/null &
  RECEIVER=$!
  if ! ps -p ${RECEIVER} > /dev/null 2>&1 ; then
    echo "receiver hasn't run"
	RES=1
  fi

  if [[ ${RES} == 0 ]]; then
    nc -4 -N 127.0.0.2 30001 < "${BIN_PATH}/byte_stacker_in"
    timeout 5 tail --pid=${RECEIVER} -f /dev/null

    if ps -p ${RECEIVER} > /dev/null 2>&1 ; then
      echo "receiver hasn't stopped after receiving data"
	  RES=1
    else
      echo "Transferred $(stat -c %s "${RES_FILE}") bytes"
      RECEIVER=-1
	fi
  fi

  if [[ ${RES} == 0 ]]; then
    if ! cmp -s "${BIN_PATH}/byte_stacker_in" ${RES_FILE} >/dev/null ; then
      echo "Files are different"
	  RES=1
    fi
  fi

  # Освобождение ресурсов
  if [[ ${RECEIVER} != -1 ]]; then
    kill ${RECEIVER}
  fi
  if [[ ${RES_FILE} != "" ]]; then
    rm -f ${RES_FILE}
  fi
  
  return ${RES}
}

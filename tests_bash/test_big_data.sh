#!/bin/bash

# Тестовый скрипт для проверки передачи большого блока данных
# В качестве блока данных используется файл с программой byte_stacker_in
# Параметры (должны быть установлены вызывающим окружением):
# BIN_PATH - путь к исполняемым файлам

BTEST_TEST_NAME="Передача большого блока"

PROC_IN=-1
PROC_OUT=-1
RECEIVER=-1

# Файл с полученными данными
RES_FILE=$(mktemp)


function setup() {
  ${BIN_PATH}/byte_stacker_in --local1=127.0.0.2:30001 --trunk=127.0.0.2:40001 &
  PROC_IN=$!
  ${BIN_PATH}/byte_stacker_out --external1=127.0.0.2:50001 --trunk=127.0.0.2:40001 &
  PROC_OUT=$!

  sleep 0.5

  RES=0
  if ! ps -p ${PROC_IN} > /dev/null 2>&1 ; then
    echo "in-process hasn't run"
    RES=1
    PROC_IN=-1
  fi

  if ! ps -p ${PROC_OUT} > /dev/null 2>&1 ; then
    echo "out-process hasn't run"
    RES=1
    PROC_OUT=-1
  fi
  return ${RES}
}


function teardown() {
  if [[ ${PROC_IN} != -1 ]]; then
    kill ${PROC_IN}
  fi
  if [[ ${PROC_OUT} != -1 ]]; then
    kill ${PROC_OUT}
  fi
  if [[ ${RECEIVER} != -1 ]]; then
    kill ${RECEIVER}
  fi
  if [[ ${RES_FILE} != "" ]]; then
    rm -f ${RES_FILE}
  fi
}


function test() {
  nc -4 -l 127.0.0.2 50001 > ${RES_FILE} < /dev/null &
  RECEIVER=$!
  if ! ps -p ${RECEIVER} > /dev/null 2>&1 ; then
    echo "receiver hasn't run"
    return 1
  fi

  nc -4 -N 127.0.0.2 30001 < "${BIN_PATH}/byte_stacker_in"

  timeout 5 tail --pid=${RECEIVER} -f /dev/null

  if ps -p ${RECEIVER} > /dev/null 2>&1 ; then
    echo "receiver hasn't stopped after receiving data"
    return 1
  fi
  RECEIVER=-1

  echo "Transferred $(stat -c %s "${RES_FILE}") bytes"

  if ! cmp -s "${BIN_PATH}/byte_stacker_in" ${RES_FILE} >/dev/null ; then
    echo "Files are different"
    return 1
  fi
}

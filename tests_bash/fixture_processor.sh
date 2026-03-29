#!/bin/bash

# Фикстура для запуска канала через обработчик пакетов
# Параметры (должны быть установлены вызывающим окружением):
# BIN_PATH - путь к исполняемым файлам

PROC_IN=-1
PROC_OUT=-1
RECEIVER=-1

# Файл с полученными данными
RES_FILE=$(mktemp)


function setup() {
  ${BIN_PATH}/byte_stacker_in --local1=127.0.0.2:30001 --trunk=127.0.0.1:20000 &
  PROC_IN=$!
  ${BIN_PATH}/byte_stacker_out --external1=127.0.0.2:50001 --trunk=127.0.0.1:20001 &
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


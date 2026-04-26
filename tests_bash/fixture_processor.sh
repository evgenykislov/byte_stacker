#!/bin/bash

# Фикстура для запуска канала через обработчик пакетов
# Параметры (должны быть установлены вызывающим окружением):
# BIN_PATH - путь к исполняемым файлам
# PROCESSORS - список обработчиков, через пробел, как в командной строке. Допустимо не указывать обработчики

PROC_IN=-1
PROC_OUT=-1
PROC_PROCESSOR=-1


if [[ -z "${BIN_PATH}" ]]; then
  echo "Test needs BIN_PATH variable with path to executable files"
  exit 1
fi

if [[ ! -v "PROCESSORS" ]]; then
  PROCESSORS=""
fi



function setup() {
  ${BIN_PATH}/byte_stacker_in --local1=127.0.0.2:30001 --trunk=127.0.0.2:20000 &
  PROC_IN=$!
  ${BIN_PATH}/byte_stacker_out --external1=127.0.0.2:50001 --trunk=127.0.0.2:20001 &
  PROC_OUT=$!
  ${BIN_PATH}/udp_processor --receive=127.0.0.2:20000 --transmit=127.0.0.2:20001 &
  PROC_PROCESSOR=$!

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

  if ! ps -p ${PROC_PROCESSOR} > /dev/null 2>&1 ; then
    echo "processor-process hasn't run"
    RES=1
    PROC_PROCESSOR=-1
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
  if [[ ${PROC_PROCESSOR} != -1 ]]; then
    kill ${PROC_PROCESSOR}
  fi
}


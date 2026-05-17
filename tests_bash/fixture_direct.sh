#!/bin/bash

# Функции запуска и останова программ byte_stacker
# Прямое подключение, без обработчиков. На 2 точки:
# 127.0.0.2:30001 -> 127.0.0.2:50001
# 127.0.0.2:30002 -> 127.0.0.2:50002
# Параметры (должны быть установлены вызывающим окружением):
# BIN_PATH - путь к исполняемым файлам

PROC_IN=-1
PROC_OUT=-1


if [[ -z "${BIN_PATH}" ]]; then
  echo "Fixture needs BIN_PATH variable. It should point to executable files folder"
  exit 1
fi


function setup() {
  ${BIN_PATH}/byte_stacker_in --local1=127.0.0.2:30001 --local2=127.0.0.2:30002 --trunk=127.0.0.2:40001 &
  PROC_IN=$!
  ${BIN_PATH}/byte_stacker_out --external1=127.0.0.2:50001 --external2=127.0.0.2:50002 --trunk=127.0.0.2:40001 &
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
}


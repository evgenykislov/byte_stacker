#!/bin/bash

# Функции запуска и останова программ byte_stacker на 2 транка
# Прямое подключение, без обработчиков. На 2 точки:
# Клиент 1: 127.0.0.2:30001 -> 127.0.0.2:50001
# Клиент 2: 127.0.0.2:30002 -> 127.0.0.2:50002
# Параметры (должны быть установлены вызывающим окружением):
# BIN_PATH - путь к исполняемым файлам

PROC_IN1=-1
PROC_IN2=-1
PROC_OUT=-1


function setup() {
  ${BIN_PATH}/byte_stacker_in --local1=127.0.0.2:30001 --trunk=127.0.0.2:40001 &
  PROC_IN1=$!
  ${BIN_PATH}/byte_stacker_in --local2=127.0.0.2:30002 --trunk=127.0.0.2:40002 &
  PROC_IN2=$!
  ${BIN_PATH}/byte_stacker_out --external1=127.0.0.2:50001 --external2=127.0.0.2:50002 --trunk=127.0.0.2:40001 --trunk=127.0.0.2:40002 &
  PROC_OUT=$!

  sleep 0.5

  RES=0
  if ! ps -p ${PROC_IN1} > /dev/null 2>&1 ; then
    echo "in-process point 1 hasn't run"
    RES=1
    PROC_IN1=-1
  fi

  if ! ps -p ${PROC_IN2} > /dev/null 2>&1 ; then
    echo "in-process point 2hasn't run"
    RES=1
    PROC_IN2=-1
  fi

  if ! ps -p ${PROC_OUT} > /dev/null 2>&1 ; then
    echo "out-process hasn't run"
    RES=1
    PROC_OUT=-1
  fi
  return ${RES}
}


function teardown() {
  if [[ ${PROC_IN1} != -1 ]]; then
    kill ${PROC_IN1}
  fi
  if [[ ${PROC_IN2} != -1 ]]; then
    kill ${PROC_IN2}
  fi
  if [[ ${PROC_OUT} != -1 ]]; then
    kill ${PROC_OUT}
  fi
}


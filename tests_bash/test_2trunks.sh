#!/bin/bash

# Тестовый скрипт для проверки передачи блоков данных в два транка, через разных клиентов
# Тест работает по следующим пунктам:
# - запускает две входных и одну выходную сторону: рабора через 2 транка. Запуск через фикстуру
# - запускает две серверные части, которые готовы принимать данные и записывать их в файл
# - запускает две клиентские части, которая сразу же начинают передавать данные по каналу
# - как только клиентские части завершили передачу, ждём завершения серверных частей. Таймаут 5 сек
# - сравниваются 2 исходных файла для передачи и полученные через канал. Если всё одинаково, то тест прошёл успешно
#
# Прим.: большой блок для такого канала - это больше 3 МБайт
# Параметры (должны быть установлены вызывающим окружением):
# BIN_PATH - путь к исполняемым файлам
#

# Название теста
BTEST_TEST_NAME="Передача 2 транка"

if [[ -z "${BIN_PATH}" ]]; then
  echo "Test needs BIN_PATH variable point to executable files"
  exit 1
fi


source ./fixture_2trunks.sh


function test() {
  # Файл с полученными данными
  SENDER1=-1
  SENDER2=-1
  RECEIVER1=-1
  RECEIVER2=-1
  RES=0
  SOURCE1_FILE=$(mktemp)
  DESTINATION1_FILE=$(mktemp)
  SOURCE2_FILE=$(mktemp)
  DESTINATION2_FILE=$(mktemp)

  if ! ${BIN_PATH}/file_generator --size=4000000 --file=${SOURCE1_FILE} ; then
    echo "can't generate test file 1"
	RES=1
  fi
  if ! ${BIN_PATH}/file_generator --size=5000000 --file=${SOURCE2_FILE} ; then
    echo "can't generate test file 2"
	RES=1
  fi

  if [[ ${RES} == 0 ]]; then
    nc -4 -l 127.0.0.2 50001 > ${DESTINATION1_FILE} < /dev/null &
	RECEIVER1=$!
    if ! ps -p ${RECEIVER1} > /dev/null 2>&1 ; then
      echo "receiver 1 hasn't run"
	  RES=1
    fi
    nc -4 -l 127.0.0.2 50002 > ${DESTINATION2_FILE} < /dev/null &
	RECEIVER2=$!
    if ! ps -p ${RECEIVER2} > /dev/null 2>&1 ; then
      echo "receiver 2 hasn't run"
	  RES=1
    fi
  fi

  if [[ ${RES} == 0 ]]; then
    nc -4 -N 127.0.0.2 30001 < ${SOURCE1_FILE} &
	SENDER1=$!
    nc -4 -N 127.0.0.2 30002 < ${SOURCE2_FILE} &
	SENDER2=$!
	
	# Ожидаем завершения передачи со стороны утилит nc
    timeout 20 tail --pid=${SENDER1} -f /dev/null
    timeout 20 tail --pid=${SENDER2} -f /dev/null
	
	# Ожидаем завершения приёма
    timeout 5 tail --pid=${RECEIVER1} -f /dev/null
    timeout 5 tail --pid=${RECEIVER2} -f /dev/null

    if ps -p ${RECEIVER1} > /dev/null 2>&1 ; then
      echo "receiver 1 hasn't stopped after receiving data"
      RES=1
    else
      RECEIVER1=-1
    fi
    if ps -p ${RECEIVER2} > /dev/null 2>&1 ; then
      echo "receiver 2 hasn't stopped after receiving data"
      RES=1
    else
      RECEIVER2=-1
    fi
  fi

  if [[ ${RES} == 0 ]]; then
    if ! cmp -s "${SOURCE1_FILE}" ${DESTINATION1_FILE} >/dev/null ; then
      echo "Files 1 are different"
      RES=1
    fi
    if ! cmp -s "${SOURCE2_FILE}" ${DESTINATION2_FILE} >/dev/null ; then
      echo "Files 2 are different"
      RES=1
    fi
  fi

  # Освобождение ресурсов
  if [[ ${RECEIVER1} != -1 ]]; then
    kill ${RECEIVER1}
  fi
  if [[ ${DESTINATION1_FILE} != "" ]]; then
    rm -f ${DESTINATION1_FILE}
  fi
  if [[ ${SOURCE1_FILE} != "" ]]; then
    rm -f ${SOURCE1_FILE}
  fi
  if [[ ${RECEIVER2} != -1 ]]; then
    kill ${RECEIVER2}
  fi
  if [[ ${DESTINATION2_FILE} != "" ]]; then
    rm -f ${DESTINATION2_FILE}
  fi
  if [[ ${SOURCE2_FILE} != "" ]]; then
    rm -f ${SOURCE2_FILE}
  fi
  
  return ${RES}
}

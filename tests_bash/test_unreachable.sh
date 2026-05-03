#!/bin/bash

# Тестовый скрипт для проверки работы в случае недоступного сервера/интернета и т.п.
# Тест работает по следующим пунктам:
# - запускает клиентскую часть канала
# - запускает утилита-источник, которая сразу же начинает передавать данные по каналу. Объём данных больше кэша (обычно 2 Мбайт)
# Тест считается выполненным успешно, если возникла ошибка при передаче данных: т.е. не все данные ушли в канал
#
# Параметры (должны быть установлены вызывающим окружением):
# BIN_PATH - путь к исполняемым файлам

# Название теста
BTEST_TEST_NAME="Отсутствие сети"

# Размер маленького файла, который должен уместиться в кэш и как-бы передаться
SMALL_SIZE=100

# Размер большого файла, который должен НЕ уместиться в кэш и привести к ошибке передачи
BIG_SIZE=10000000


if [[ -z "${BIN_PATH}" ]]; then
  echo "Test needs BIN_PATH variable point to executable files"
  exit 1
fi


function test() {
  # Файл с полученными данными
  PROC_IN=-1
  RES=0
  SOURCE_FILE=$(mktemp)

  # Запустим клиентскую часть
  ${BIN_PATH}/byte_stacker_in --local1=127.0.0.2:30001 --trunk=127.0.0.2:40002 &
  PROC_IN=$!
  sleep 0.5
  if ! ps -p ${PROC_IN} > /dev/null 2>&1 ; then
    echo "stacker client hasn't run"
    RES=1
    PROC_IN=-1
  fi

  # Проверим на большом файле
  if [[ ${RES} == 0 ]]; then
    if ! ${BIN_PATH}/file_generator --size=${BIG_SIZE} --file=${SOURCE_FILE} ; then
      echo "can't generate big test file"
	  RES=1
    else
      dd if=${SOURCE_FILE} status=none | nc -4 -N 127.0.0.2 30001
	  ALL_RES=("${PIPESTATUS[@]}")
      if [[ ${ALL_RES[0]} == 0 ]]; then
	    echo "big file transferred - error"
        RES=1
      fi
	fi
  fi

  # Проверим на маленьком файле
  if [[ ${RES} == 0 ]]; then
    if ! ${BIN_PATH}/file_generator --size=${SMALL_SIZE} --file=${SOURCE_FILE} ; then
      echo "can't generate small test file"
	  RES=1
    else
      dd if=${SOURCE_FILE} status=none | nc -4 -N 127.0.0.2 30001
	  ALL_RES=("${PIPESTATUS[@]}")
      if [[ ${ALL_RES[0]} != 0 ]]; then
	    echo "small file doesn't transferred - error"
        RES=1
      fi
	fi
  fi


  # Освобождение ресурсов
  if [[ ${PROC_IN} != -1 ]]; then
    kill ${PROC_IN}
  fi
  if [[ ${SOURCE_FILE} != "" ]]; then
    rm -f ${SOURCE_FILE}
  fi
  
  return ${RES}
}

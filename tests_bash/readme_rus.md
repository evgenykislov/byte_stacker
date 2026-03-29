# Тестовые скрипты для проверки и покрытия кода

# Использование
Скрипты работают во фреймворке bash-tester (https://github.com/evgenykislov/bash-tester , https://gitflic.ru/project/evgenykislov/bash-tester).
Для правильной работы самих тестов необходимо экспортировать переменную BIN_PATH, в которой будет путь к собранным приложениям для тестирования.

Пример запуска:  
export BIN_PATH=$(pwd)/../byte_stacker/build_coverage/bin  
bash-tester.sh $(pwd)/../byte_stacker/tests_bash

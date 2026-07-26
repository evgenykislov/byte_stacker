# byte_stacker
  
# Сборка под Linux
Установить пакеты: g++ cmake libboost-all-dev  
  
Команды сборки:  
cmake -B build  
cmake --build build  



# Сборка под Windows
Установите boost, пропишите BOOST_ROOT



# Сборка под ios
Скачайте и соберите boost. В настройках xcode добавьте путь к заголовочным файлам под именем "boost": Xcode - Settings - Locations - Custom Paths добавьте строку:
Name=boost
Display Name=boost
Path=some-path/frameworks/Headers

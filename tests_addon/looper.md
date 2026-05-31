# Терминал 1 — запуск сервера
python3 echo_server.py

# Терминал 2 — тест через netcat
echo "Hello, server!" | nc 127.0.0.1 9090

# Тест превышения порога (передаём >1024 байт)
python3 -c "import socket; s=socket.create_connection(('127.0.0.1',9090)); s.sendall(b'X'*2000); print(s.recv(4096))"

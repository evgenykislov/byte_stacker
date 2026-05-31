import socket
import os
import time
import datetime
import sys

# ==============================================================================
# НАСТРОЙКИ
# ==============================================================================

HOST            = "127.0.0.2"   # Адрес сервера
PORT            = 9090          # TCP-порт сервера

LOOP_COUNT      = 20            # Количество итераций цикла (0 = бесконечно)

PRE_SEND_DELAY  = 1.0           # Задержка перед отправкой данных (секунды)
SEND_BYTES      = 1            # Количество случайных байт для отправки
RECV_TIMEOUT    = 5.0           # Таймаут ожидания ответа (секунды)

CONNECT_TIMEOUT = 10.0          # Таймаут подключения (секунды)
BUFFER_SIZE     = 4096          # Размер буфера приёма

# ==============================================================================


def ts() -> str:
    """Возвращает метку времени с точностью до микросекунд."""
    return datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")


def log(message: str) -> None:
    """Выводит сообщение в консоль с меткой времени."""
    print(f"[{ts()}] {message}", flush=True)


def recv_exact(sock: socket.socket, expected_len: int, timeout: float) -> bytes | None:
    """
    Принимает ровно expected_len байт с заданным таймаутом.
    Возвращает полученные байты или None при ошибке / истечении таймаута.
    """
    sock.settimeout(timeout)
    data = b""
    deadline = time.monotonic() + timeout

    while len(data) < expected_len:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            log(f"⚠  Таймаут ожидания ответа ({timeout} с) истёк. "
                f"Получено {len(data)}/{expected_len} байт.")
            return None
        sock.settimeout(remaining)
        try:
            chunk = sock.recv(min(BUFFER_SIZE, expected_len - len(data)))
        except socket.timeout:
            log(f"⚠  Таймаут ожидания ответа ({timeout} с) истёк. "
                f"Получено {len(data)}/{expected_len} байт.")
            return None
        if not chunk:
            log("✖  Соединение закрыто удалённой стороной во время приёма данных.")
            return None
        data += chunk

    return data


def run() -> None:
    # ── Подключение ────────────────────────────────────────────────────────────
    log(f"→  Подключение к {HOST}:{PORT} (таймаут {CONNECT_TIMEOUT} с)…")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    try:
        sock.settimeout(CONNECT_TIMEOUT)
        sock.connect((HOST, PORT))
    except socket.timeout:
        log(f"✖  Не удалось подключиться: таймаут ({CONNECT_TIMEOUT} с) истёк.")
        sock.close()
        sys.exit(1)
    except ConnectionRefusedError:
        log(f"✖  Подключение отклонено ({HOST}:{PORT}).")
        sock.close()
        sys.exit(1)
    except OSError as exc:
        log(f"✖  Ошибка подключения: {exc}")
        sock.close()
        sys.exit(1)

    log(f"✔  Соединение установлено: {sock.getsockname()} → {sock.getpeername()}")

    # ── Основной цикл ──────────────────────────────────────────────────────────
    iteration   = 0
    total_ok    = 0
    total_fail  = 0
    infinite    = (LOOP_COUNT == 0)

    try:
        while infinite or iteration < LOOP_COUNT:
            iteration += 1
            loop_label = "∞" if infinite else str(LOOP_COUNT)
            log(f"── Итерация {iteration}/{loop_label} {'─' * 40}")

            # 1. Задержка перед отправкой
            log(f"   ⏳ Пауза {PRE_SEND_DELAY} с перед отправкой…")
            time.sleep(PRE_SEND_DELAY)

            # 2. Генерация и отправка случайных данных
            payload = os.urandom(SEND_BYTES)
            log(f"   ↑  Отправка {SEND_BYTES} байт: {payload.hex()}")
            try:
                sock.settimeout(CONNECT_TIMEOUT)
                sent = sock.sendall(payload)          # sendall возвращает None при успехе
            except (BrokenPipeError, ConnectionResetError, OSError) as exc:
                log(f"✖  Соединение разорвано при отправке: {exc}")
                break
            log(f"   ✔  Данные отправлены успешно.")

            # 3. Ожидание и приём ответа
            log(f"   ↓  Ожидание ответа ({SEND_BYTES} байт, таймаут {RECV_TIMEOUT} с)…")
            received = recv_exact(sock, SEND_BYTES, RECV_TIMEOUT)

            if received is None:
                # Соединение разорвано или таймаут — завершаем работу
                total_fail += 1
                break

            log(f"   ↓  Получено {len(received)} байт: {received.hex()}")

            # 4. Сравнение данных
            if payload == received:
                total_ok += 1
                log(f"   ✔  Данные совпадают. [OK: {total_ok}  FAIL: {total_fail}]")
            else:
                total_fail += 1
                log(f"   ✖  Данные НЕ совпадают! "
                    f"Отправлено: {payload.hex()} | "
                    f"Получено: {received.hex()} "
                    f"[OK: {total_ok}  FAIL: {total_fail}]")

    except KeyboardInterrupt:
        log("⚠  Прервано пользователем (Ctrl+C).")

    finally:
        # ── Закрытие соединения ───────────────────────────────────────────────
        log(f"→  Закрытие соединения…")
        try:
            sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        sock.close()
        log(f"✔  Соединение закрыто. Итого итераций: {iteration}. "
            f"Успешно: {total_ok}, Ошибок: {total_fail}.")


if __name__ == "__main__":
    run()

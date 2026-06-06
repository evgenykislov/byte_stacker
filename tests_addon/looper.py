import asyncio
import logging
from datetime import datetime

# =============================================================================
# НАСТРОЙКИ
# =============================================================================
HOST            = "127.0.0.2"   # Адрес для прослушивания
PORT            = 9090         # TCP-порт для прослушивания
BUFFER_SIZE     = 4096         # Размер буфера чтения (байт)
MAX_BYTES       = 5         # Порог данных, после которого соединение будет закрыто (байт)
CLOSE_DELAY     = 5.0          # Задержка перед принудительным закрытием соединения (секунд)
# =============================================================================


# ---------------------------------------------------------------------------
# Логгер с точностью до микросекунд
# ---------------------------------------------------------------------------
class MicrosecondFormatter(logging.Formatter):
    def formatTime(self, record, datefmt=None):
        return datetime.fromtimestamp(record.created).strftime("%Y-%m-%d %H:%M:%S.%f")


handler = logging.StreamHandler()
handler.setFormatter(MicrosecondFormatter("[%(asctime)s] %(levelname)s %(message)s"))

logger = logging.getLogger("echo_server")
logger.setLevel(logging.DEBUG)
logger.addHandler(handler)


# ---------------------------------------------------------------------------
# Обработчик одного клиентского соединения
# ---------------------------------------------------------------------------
async def handle_client(reader: asyncio.StreamReader,
                         writer: asyncio.StreamWriter) -> None:
    peer = writer.get_extra_info("peername")
    logger.info(f"[CONNECT]  {peer}  —  соединение установлено")

    total_bytes    = 0          # суммарно принято байт
    close_pending  = False      # флаг: отсчёт закрытия уже запущен
    close_task     = None       # asyncio.Task отложенного закрытия

    async def delayed_close():
        """Ждёт CLOSE_DELAY секунд, затем принудительно закрывает соединение."""
        await asyncio.sleep(CLOSE_DELAY)
        if not writer.is_closing():
            logger.info(
                f"[CLOSE]    {peer}  —  принудительное закрытие "
                f"(порог {MAX_BYTES} Б превышен, прошло {CLOSE_DELAY} с)"
            )
            writer.close()
            try:
                await writer.wait_closed()
            except Exception:
                pass

    try:
        while True:
            data = await reader.read(BUFFER_SIZE)

            # Клиент закрыл соединение со своей стороны
            if not data:
                logger.info(f"[CLOSE]    {peer}  —  клиент закрыл соединение")
                break

            total_bytes += len(data)
            logger.info(
                f"[RECV]     {peer}  —  получено {len(data)} Б  "
                f"(итого: {total_bytes} Б)"
            )

            # Эхо: отправляем данные обратно
            writer.write(data)
            await writer.drain()
            logger.info(
                f"[SEND]     {peer}  —  отправлено {len(data)} Б обратно"
            )

            # Если порог превышен и отсчёт ещё не запущен — запускаем
            if total_bytes > MAX_BYTES and not close_pending:
                close_pending = True
                logger.info(
                    f"[LIMIT]    {peer}  —  порог {MAX_BYTES} Б превышен, "
                    f"соединение будет закрыто через {CLOSE_DELAY} с"
                )
                close_task = asyncio.create_task(delayed_close())

    except asyncio.IncompleteReadError:
        logger.info(f"[CLOSE]    {peer}  —  соединение неожиданно разорвано")
    except ConnectionResetError:
        logger.info(f"[CLOSE]    {peer}  —  соединение сброшено клиентом")
    except Exception as exc:
        logger.exception(f"[ERROR]    {peer}  —  непредвиденная ошибка: {exc}")
    finally:
        # Отменяем задачу отложенного закрытия, если она ещё не выполнилась
        if close_task and not close_task.done():
            close_task.cancel()
        if not writer.is_closing():
            writer.close()
            try:
                await writer.wait_closed()
            except Exception:
                pass


# ---------------------------------------------------------------------------
# Точка входа
# ---------------------------------------------------------------------------
async def main() -> None:
    server = await asyncio.start_server(handle_client, HOST, PORT)
    addrs  = ", ".join(str(s.getsockname()) for s in server.sockets)
    logger.info(f"[START]    Сервер запущен, слушает: {addrs}")
    logger.info(f"[CONFIG]   MAX_BYTES={MAX_BYTES} Б  |  CLOSE_DELAY={CLOSE_DELAY} с  |  BUFFER={BUFFER_SIZE} Б")

    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("[STOP]     Сервер остановлен (KeyboardInterrupt)")

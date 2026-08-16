#ifndef OUTLINK_H
#define OUTLINK_H

#include <atomic>
#include <cstdint>
#include <fstream>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include <boost/asio.hpp>

#include "../common/data.h"
#include "../common/trace.h"

struct AddressPortPoint {
  std::string Address;
  uint16_t Port;
};


struct Settings;
class TrunkLink;
class Tracer;


/*! \class IOutLink класс для управления внешними tcp-соединениями.
Экземпляр создаётся либо на основе подключенного сокета, либо на основе точки
подключения (и тогда соединение устанавливается в процессе).
Механизм работы:
- создаём экземпляр;
- проводим дополнительные регистрации, назначение обработчиков;
- запускаем подключение/чтение на сокете: вызываем метод Run();
- когда подключение завершает работу (по причинам отключений/ошибок или вызова
Stop), то класс сам вызывает функцию CloseConnect хостера */
class OutLink: public std::enable_shared_from_this<OutLink> {
 public:
  // Функции конструирования экземпляров. Описание см. для соответствующих
  // приватных конструкторов
  static std::shared_ptr<OutLink> CreateOutLink(ConnectID cnt,
      boost::asio::ip::tcp::socket&& socket, const Settings& cfg,
      Tracer* tracer);
  static std::shared_ptr<OutLink> CreateOutLink(ConnectID cnt,
      boost::asio::io_context& ctx, std::string address, uint16_t port,
      const Settings& cfg, Tracer* tracer);


  virtual ~OutLink();

  enum StopReason {
    kStopReleaseCommand,  // Пришла команда на закрытие соединения
    kStopNoLive,  // Нет live-пакетов о соединении (возможно, другая сторона
                  // отсутствует)
    kStopChunkAbsent  // Нет чанка с данными
  };

  // TODO descr
  ConnectID GetConnectID() { return connect_id_; }

  // TODO Сделать потокобезопасной
  /*! Запуск подключения в работу. Функция неблокирующая
  \param hoster указатель на "хостера", который работает со всеми подключениями.
  Указатель должен быть корректным, пока идёт работе подлключения
  \param cnr идентификатор этого подключения для идентификации данных */
  void Run(TrunkLink* hoster, ConnectID cnt);

  // TODO Descr
  void SendData(uint32_t chunk_id, const void* data, size_t data_size);

  /*! Запрос на закрытие соединения после отправки всех данных ДО указанного
  номера чанка. Т.е. все чанки с номерами равными или большими указанному - не
  принимаются в отправку. Если нужно остановить соединение "на сейчас", то
  задаём чанк 0.
  \param stop_chunk номер чанка, следующего за последним валидным */
  void Stop(uint32_t stop_chunk, StopReason reason);

  /*! Выдаём объём данных, записанных "наружу" этим outlink-ом. Значение
  используется для балансировки трафика
  \return общий записанный объём, байт */
  uint64_t GetWrittenVolume();

  /*! Сохраняет объём данных, записанный парным/другим концом соединения. Вызов
  функции будет "спамиться" с одинаковыми значениями \param volume объём
  записанных данных на другой стороне, байт */
  void SetOtherSideWrittenVolume(uint64_t volume);

 protected:
  Tracer* tracer_;

 private:
  OutLink() = delete;
  OutLink(const OutLink&) = delete;
  OutLink& operator=(const OutLink&) = delete;
  OutLink(OutLink&& arg) = delete;
  OutLink& operator=(OutLink&& arg) = delete;

  // Приватные конструкторы, используются через соответствующую Create...
  // функцию

  /*! Конструктор экземпляра на основе сокета с установленным соединением
  \param socket сокет с соединением */
  OutLink(ConnectID cnt, boost::asio::ip::tcp::socket&& socket,
      const Settings& cfg, Tracer* tracer);

  /*! Конструктор экземпляра на основе адреса и порта (например mysite.com:80).
  Подключение выполняется автоматически (и асинхронно) при вызове функции Run.
  Если подключение неуспешно (нет адреса, сервера и т.д.), то вызывается явно
  функция отключения для hoster-а.
  \param ctx сетевой контекст бибилиотеки asio
  \param address адрес для подключения. Может быть как явный ip-адрес, так и имя
  сайта
  \param port порт для подключения */
  OutLink(ConnectID cnt, boost::asio::io_context& ctx, std::string address,
      uint16_t port, const Settings& cfg, Tracer* tracer);


  static const size_t kChunkSize = 800;
  static const size_t kMaxChunkAmount = 5000;

  /*! Таймаут на ожидание данных на запись, в миллисекундах */
  static const size_t kWriteIdleTimeout = 10000;

  /*! Таймаут на ожидание перед запросом чтения, в миллисекундах */
  static const size_t kReadIdleTimeout = 200;

  /*! Максимальный объём данных в обработке, байт */
  static const uint64_t kMaxProcessingDataSize = 2000000;

  static const uint32_t kUndefinedChunkID = static_cast<uint32_t>(-1);

  ConnectID connect_id_;

  boost::asio::ip::tcp::socket socket_;  //! Сокет подключения
  boost::asio::ip::tcp::resolver resolver_;
  char read_buffer_[kChunkSize];  //! Буфер для приёма данных. Однопоточный
  std::string host_;
  std::string service_;
  std::list<boost::asio::ip::tcp::endpoint> resolved_points_;
  TrunkLink* hoster_;
  ConnectID selfid_;
  std::string selfid_str_;

  const Settings& cfg_settings_;

  std::atomic_bool read_processing_;
  std::atomic_bool write_processing_;
  std::atomic_bool
      connected_socket_;  //!< Признак, что сокет с установленным соединением

  std::atomic_uint64_t
      written_volume_;  //!< Общий записанный вовне объём данных
  std::atomic_uint64_t
      otherside_written_volume_;  //!< Общий объём записанных данных другой
                                  //!< частью соединения
  std::atomic_uint64_t read_volume_;  //!< Общий объём прочитанных данных

  /*! Флаг, что вызов закрытия соединения на хостере уже инициирован.
  Используется только в функции CheckStopReadWrite.
  */
  std::atomic_flag close_invoked_;


  // Система выдачи данных наружу

  /*! Финальный буфер для выдачи данных в сетевой сокет. Буфер работает в
  квази-однопоточном режиме: только одна функция в каждый момент работает с
  буфером, работа только из RequestWrite */
  std::vector<uint8_t> network_write_buffer_;

  /*! Отдельные чанки для сборки полноценного блока данных. Лочится через
  write_chunks_lock_ */
  std::map<uint32_t, std::vector<uint8_t>> write_chunks_;

  /*! Идентификатор чанка, когда прекращается запись (и сокет закрывается).
  Если идентификатор не задан, то используется значение kUndefinedChunkID.
  Лочится через write_chunks_lock_ */
  uint32_t stop_write_chunk_id_;

  /*! Признак, что все нужные данные уже в буфере на запись и больше данных не
  будет. После выдачи всего буфера соединение можно закрывать. Лочится через
  write_chunks_lock_ */
  bool stop_after_all_write_;

  /*! Признак, что нужно остановить запись немедленно. Лочится через
  write_chunks_lock_ */
  bool stop_write_immediate_;

  /*! Идентификатор пакета, который сейчас валиден и ожидается. Лочится через
  write_chunks_lock_ */
  uint32_t next_write_chunk_id_;

  std::recursive_mutex write_chunks_lock_;

  /*! Таймер на ожидание новых данных для записи */
  boost::asio::steady_timer write_idle_timer_;

  /*! Таймер ожидания разрешения чтения, чтобы передались и подтвердились
  переданные данные - и не переполнять входную очередь */
  boost::asio::steady_timer read_idle_timer_;

  /*! Файл и блокировка для логирования операций по коннекту */
#ifdef CONNECT_LOG
  std::ofstream log_;
  std::mutex log_lock_;
#endif

  // TODO Descr
  void FillNetworkBuffer();

  /*! Отменить операции записи в сокет. Это всё с учётом пустых буферов,
  ожиданий и др., когда до собственно записи даже не дошли */
  void CancelReadWrite();

  /*! Функция запрос чтения данных. Функция асинхронная, данные запрашиваются и
  функция сразу завершает работу. Для каждого экземпляра подключения функция
  должна вызываться "однопоточно" */
  void RequestRead();
  /*! Парная функция обработки для RequestRead */
  void RequestReadProcessing(
      const boost::system::error_code& err, std::size_t bytes_transferred);

  /*! Функция принудительного ожидания перед чтением, чтобы не вычитать слишком
   * много данных, которые потом не передать */
  void RequestReadIdle();

  /*! Функция запроса подключения к первой точке в списке от резолвинга. В
  случае неудачи эта "первая точка" удаляется из списка и делается опять вызов
  этой же функции */
  void RequestConnect();
  /*! Парная функция обработки для RequestConnect */
  void RequestConnectProcessing(const boost::system::error_code& err);


  /*! Функция запроса операций на запись в сокет. Функция вызывается
  "однопоточно": с первого запуска она вызывает сама себя */
  void RequestWrite();
  /*! Парная функция обработки для RequestWrite */
  void RequestWriteProcessing(
      const boost::system::error_code& err, std::size_t bytes_transferred);

  /*! Функция проверки на остановку операций чтения/записи. Если всё
  остановлено, то вызывается закрытие соединения у хостера */
  void CheckReadyClose();
  /*! Парная функция обработки для CheckReadyClose */
  void CheckReadyCloseProcessing();

  /*! Функция для обработки результатов резолвинга адресов */
  void ResolverProcessing(const boost::system::error_code& err,
      boost::asio::ip::tcp::resolver::results_type results);


  /*! Запись сообщений (логов) в файл для дальнейшего разбора.
  Функция делает запись только при наличии дефайна CONNECT_LOG
  В записях добавляются временные метки.
  \param format форматка для вывода
  \param args аргументы для вывода */
  template <typename... Types>
  void LogWrite(const char* format, Types... args) {
#ifdef CONNECT_LOG
    std::lock_guard lk(log_lock_);
    if (!log_) {
      return;
    }
    log_ << timemark() << ": ";
    const size_t kBufSize = 1000;
    char buf[kBufSize];
    if (std::snprintf(buf, kBufSize, format, args...) >= 0) {
      log_ << buf << std::endl;
    } else {
      log_ << "ERROR: memory overflow" << std::endl;
    }
    log_.flush();
#endif
  }
};


#endif  // OUTLINK_H

#ifndef TRUNKLINK_H
#define TRUNKLINK_H

#include <deque>
#include <mutex>
#include <utility>

#include <boost/asio.hpp>

#include "data.h"
#include "outlink.h"


enum TrunkCommand : uint32_t {
  kTrunkCommandCreateConnect = 1,
  /*! Закрытие соединения. Обязательно с номером, чтобы дошли все пакеты данных.
  Команда посылается как данные, и подтверждается как данные */
  kTrunkCommandReleaseConnect = 2,
  kTrunkCommandAckCreateConnect = 3,
  kTrunkCommandDataOut =
      11,  // Пакет данных с локальной точки на внешний сервер
  kTrunkCommandDataIn = 12,
  // Подтверждение получения пакета данных серверной стороной
  kTrunkCommandAckDataOut = 21,
  // Подтверждение получения пакета данных клиентской стороной
  kTrunkCommandAckDataIn = 22,
  kTrunkCommandLive =
      31,  //!< Live-пакет для поддержания актуальности соединения
};


const size_t kConnectIDSize = 16;

const unsigned int kResendTimeout = 300;  //!< Интервал перепосылки пакетов

/*! Интервал перепосылки коннект-пакетов (создания нового соединения) */
const unsigned int kResendConnectTimeout = 150;

/*! Таймаут в течение которого должен прийти ЛЮБОЙ ответ для соединения: live,
подтверждение пакета и т.п.. Иначе соединение будет считаться мёртвым */
const unsigned int kDeadOutLinkTimeout = 5000;

/*! Таймаут в течение которого должно прийти подтверждение пакета.
Иначе пакет будет считаться мёртвым и его удалят из очереди
Прим: У мёртвого соединения могут быть "живые" пакеты - это нормально */
static const size_t kDeadPacketTimeout = 20000;

const size_t kMaxChunkSize = 800;

/*! Минимальный размер буфера udp сокета на отправку. Используется если
реальный размер получить не удалось (или он совсем маленький) */
const int kMinimalUdpBufferSize = 20000;

const double kUdpBufferDataPart =
    0.7;  //!< Часть буфера, отводимая под отправку данных. Остальная часть
          //!< остаётся как резерв для live-пакетов и др. важной информации

/*! TODO Хардкод. Убрать. Типовая скорость udp обмена байт в микросекунду (или:
 * мегабайт/сек) */
const double kDefaultUdpTrafficSpeed = 4.0;

const size_t kUdpPacketOverhead =
    40;  //!< Дополнительное место, которое занимает пакет в буфере на отправку.
         //!< Как минимум 20 байт на заголовки, ip-адреса и др.

/*! Время хранения пакета с данными без существующего соединения (пришёл раньше
соединения или оно уже удалено). В миллисекундах */
const int kOrphanDataTimeout = 5000;

/*! Возвращаемое значение свободного места в udp буфере, если буфер недоступен
 */
const int kUdpBufferUnavailable = INT_MIN;

struct PacketHeader {
  uint8_t ConnectID[kConnectIDSize];
  TrunkCommand PacketCommand;
};

struct PacketConnect: PacketHeader {
  uint32_t PointID;
  uint32_t Timeout;
};


struct PacketData: PacketHeader {
  uint32_t PacketIndex;
  uint32_t DataSize;
  // uint8_t Data[DataSize];
};


struct PacketAck: PacketHeader {
  uint32_t PacketIndex;
};


struct PacketLive: PacketHeader {
  uint64_t WrittenOutSize;  //!< Общий записанный вовне объём для соединения
};


struct StatInfo {
  size_t StreamToOutLinks;  //!< Поток данных наружу через внешние соединения,
                            // байт с момента последнего запроса
  size_t StreamFromOutLinks;  //!< Поток данных в транс из внешних соединений,
                              // байт с момента последнего запроса
  size_t ConnectAmount;  //! Текущее количество подключений
  size_t MinPing;  // Минимальный пинг на транке, в микросекундах
  size_t MaxPing;
  size_t AveragePing;
  size_t FauldPacket;
  size_t cache_load;  // Количество кэшированных пакетов для повторной отправки
  bool no_live;  // Признак, что были попытки соединения без подтверждения
};

class OutLink;
struct Settings;
class Tracer;

/*! \class TrunkLink Общая часть алгоритмов транковой связи. TrunkLink не
предназначен для самостоятельного использвоания, только как базовый класс */
class TrunkLink {
 public:
  TrunkLink(boost::asio::io_context& ctx, bool server_side, const Settings& cfg,
      Tracer* tracer);

  virtual ~TrunkLink() {}

  // TODO Descr
  // Допустимо указать data = nullptr (или любой другой адрес), если data_size
  // == 0
  void SendCmdData(
      ConnectID cnt, const void* data, size_t data_size, TrunkCommand cmd);

  // TODO descr
  void SendPacketQueue();

  // TODO Descr
  void SendData(ConnectID cnt, const void* data, size_t data_size);

  /*! Закрыть коннект по сигналу "снаружи": соединение разорвано и т.п.
  Коннект уже может быть закрыт (дубликат события)
  \param cnt идентификатор коннекта */
  void CloseConnect(ConnectID cnt);


 protected:
  static const uint32_t kEmptyPacketID = static_cast<uint32_t>(-1);
  static const uint32_t kConnectionAbsentError = static_cast<uint32_t>(-2);
  static const uint32_t kOverflowError = static_cast<uint32_t>(-3);
  static_assert(kOverflowError < kConnectionAbsentError);

  static const size_t kPacketBufferSize = 1000;
  using PacketBuffer = uint8_t[kPacketBufferSize];

  /*! Информация о целом пакете: с заголовком и т.д. */
  struct PacketInfo {
    ConnectID CtxID;
    uint32_t PacketID;  // Номер пакета или kEmptyPacketID
    std::shared_ptr<PacketBuffer> PacketData;
    uint32_t PacketSize;
  };

  /*! Информация о пришедших данных для соединения (только разобранные данные)
   */
  struct DataInfo {
    ConnectID CtxID;
    PacketIDType PacketID;
    std::shared_ptr<PacketBuffer> data;
    uint32_t size;
    //! Метка времени, после которой пакет уже можно удалять - недождались
    std::chrono::steady_clock::time_point deadline;
  };

  /*! Информация о пришедших данных для закрытия соединения */
  struct ReleaseInfo {
    ConnectID CtxID;
    PacketIDType PacketID;
    //! Метка времени, после которой данные уже можно удалять - недождались
    std::chrono::steady_clock::time_point deadline;
  };

  /*! Описание одного внешнего коннекта (tcp-соединения). У коннекта
  обязательно есть link */
  struct OutLinkInfo {
    uuids::uuid connect_id;
    std::shared_ptr<OutLink> link;
    uint32_t next_index_to_trunk;  //!< Индекс пакета для следующего пакета
    std::chrono::steady_clock::time_point
        deadlink_timeout_;  //!< Время, после которого соединение считается
                            //!< мёртвым
  };


  // TODO
  // Массив закрывается out_links_lock_, которая заявлена как protected
  std::vector<OutLinkInfo> out_links_;

  /*! Массив с пришедшими данными, для которых ещё или уже нет соединения.
  Массив закрывается out_links_lock_ */
  std::deque<DataInfo> data_cache_;

  /*! Массив с информацией о закрытии соединений, для которых ещё или уже нет
  соединения. Массив закрывается out_links_lock_ */
  std::deque<ReleaseInfo> release_cache_;


  std::mutex out_links_lock_;

  const Settings& cfg_settings_;

  /*! Указатель на класс трейсера: записывает события по соединению в отдельный
  файл. Указатель может быть nullptr */
  Tracer* tracer_;

  /*! Обработка пакета с данными полученными из транка. Никакая предварительная
  проверка валидности не проводилась. Параметры socket_index и client
  используются для отправки ответа даже для несуществующего или уже удалённого
  соединения \param socket_index - индекс сокета для транковой связи. 0 в случае
  одного сокета \param client адрес возврата клиента \param data блок данных
  \param data_size размер данных */
  void ProcessTrunkData(size_t socket_index,
      boost::asio::ip::udp::endpoint client, const void* data,
      size_t data_size);

  PacketInfo FormPacket(
      const PacketData& header, uint8_t* data, size_t data_size);

  /*! Возвращает размер свободного места в буфере на передачу для заданного
  соединения. Если буфер не существует (соединение уже удалено и т.д.), то
  возвращается размер kUdpBufferUnavailable.
  \param ctx идентификатор соединения
  \return размер свободного места в байтах. Размер может быть отрицательным */
  virtual int GetAvailableBuffer(ConnectID ctx) = 0;

  /*! Отправить пакет по транку. Ошибки отправки не контролируются,
  переотправка должна реализовываться раньше/в другом месте
  \param pkt отправляемый пакет */
  virtual void SendPacket(PacketInfo pkt) = 0;

  /*! Отправить пакет по транку через заданный сокет (по индексу) и целевай
  адрес. Ошибки не контролируются, переотправки нет \param socket_index индекс
  сокета в транковом пуле. Для случая одного сокета (клиентсткая сторона)
  использовать 0 \param target целевой адрес/порт, куда отправлять пакет \param
  pkt пакет с данными (упакованы сами данные, длина и т.д.) */
  virtual void SendPacket(size_t socket_index,
      boost::asio::ip::udp::endpoint target, PacketInfo pkt) = 0;

  // Обработчики отдельных команд
  virtual void ProcessConnectData(uuids::uuid cnt, const PacketConnect* info,
      size_t socket_index, boost::asio::ip::udp::endpoint target){};
  virtual void ProcessAckConnectData(
      uuids::uuid cnt, const PacketHeader* info){};

  // TODO Descr
  void ProcessDataToOutlink(
      uuids::uuid cnt, const PacketData* info, const void* data);

  // TODO Descr
  void ProcessAckData(uuids::uuid cnt, uint32_t packet_index);

  // TODO Descr
  void ProcessReleaseConnect(uuids::uuid cnt, uint32_t packet_id);

  /*! Обработка пришедшего из транка live-пакета
  \param cnt идентификатор соединения
  \param written объем записанных данных (для другого конца коннекта) */
  void ProcessLive(uuids::uuid cnt, uint64_t written);

  // TODO
  // Вызов должен быть закрыт out_links_lock_
  std::shared_ptr<OutLink> GetOutLinkWOLock(uuids::uuid cnt);

  std::shared_ptr<OutLink> GetOutLink(uuids::uuid cnt);

  // TODO Descr?
  std::shared_ptr<PacketBuffer> GetBuffer();

  /* Перепосылка кэша пакетов. При перепосылке используется таймер, чтобы не
  устраивать шторм пакетов */
  virtual void OnCacheResend();


  /*! Послать по транку информацию о разрыве соединения
  \param cnt идентификатор коннекта */
  void SendDisconnectInformation(ConnectID cnt);

  /*! Получить статистику по работе приложения */
  StatInfo GetStat();

  /*! Хелпер на вывод сообщения по соединению в трейсер */
  void TracerMessage(uuids::uuid id, const std::string& msg);

  /*! Функция добавления нового внешнего соединения для работы с транком
  Функция добавляет и проводит регистрационные действия, инициализирует
  счётчики. НО не запускает операции чтения/записи на соединении: причина:
  сначала регистрируемся (убеждаемся в отсутствии дубликатов, всё ок),
  потом шлём регистрационную информацию везде, потом запускаем соединение в
  работу
  \param cnt идентификатор подключения
  \param link экземпляр внешнего соединения */
  bool AddOutLink(uuids::uuid cnt, std::shared_ptr<OutLink> link);

  /*! Функция запуска операций чтение/запись для внешней связи и выдача
  кэша из блоков данных и команд закрытия (если они есть)
  \param cnt идентификатор подключения */
  void RunOutLink(uuids::uuid cnt);

 private:
  TrunkLink() = delete;
  TrunkLink(const TrunkLink&) = delete;
  TrunkLink(TrunkLink&&) = delete;
  TrunkLink& operator=(const TrunkLink&) = delete;
  TrunkLink& operator=(TrunkLink&&) = delete;


  struct PacketDataCache {
    PacketInfo info;
    std::chrono::steady_clock::time_point
        FirstSend;  //!< Время первоначальной отсылки пакета
    std::chrono::steady_clock::time_point
        Deadline;  //!< Время, после которого считается соединение разорванным
    std::chrono::steady_clock::time_point
        NextSend;  //!< Время посылки дублириющей посылки
  };


  static const size_t kUpdateTick = 100;
  static const size_t kLiveUpdateTick = 300;

  //! Интервал разбора очереди на отправку
  static constexpr size_t kSendQueueTick = 10;

  static const size_t kForceRemoveLinkTimeout =
      5000;  //!< Таймаут на "мягкое" удаление соединения. Если за это время оно
             //!< само не удалится, то его "жёстко" удалят
  static const size_t kUndefinedSizeT = static_cast<size_t>(-1);

  bool server_side_;
  /*! Символы для обозначения пакетов на своей стороне и на другой стороне
  Используются для удобства разбора логов, зависят от server_side_ */
  char my_packet_symbol_, other_packet_symbol_;

  // TODO Переделать в deque
  // TODO Descr
  std::deque<PacketInfo> packet_send_queue_;
  // TODO Descr
  std::mutex packet_send_queue_lock_;

  // TODO Переделать в deque
  std::vector<PacketDataCache>
      packet_data_cache_;  //!< Кэш пакетов для повторной отправки
  std::mutex packet_data_cache_lock_;  //!< Блокировка для работы с кэшем
                                       //!< packet_data_cache_
  boost::asio::steady_timer update_timer_;

  // TODO Descr
  boost::asio::steady_timer send_queue_timer_;

  // Данные для вывода статистики
  std::atomic_size_t out_stream_counter_;
  std::atomic_size_t in_stream_counter_;

  // Поля статистики. Лочатся stat_lock_
  size_t trunk_ping_min_;  // Минимальное время посылки-подтверждения пакета, в
                           // микросекундах
  size_t trunk_ping_max_;  // Максимальное время посылки-подтверждения пакета
  size_t trunk_ping_summ_;  // Общее время посылки-подтверждения пакета
  size_t trunk_ping_count_;  // Количетсво посылок-подтверждений пакетов
  size_t trunk_packet_fault_;  // Количество недоставленных пакетов
  std::atomic_flag trunk_live_ok;  // Признак, что live-пакеты по транку ходят
  std::mutex stat_lock_;

  std::chrono::steady_clock::time_point
      next_live_update_;  //!< Время, когда следующий раз посылать live-пакеты


  // TODO Descr
  std::ofstream error_log_;
  std::mutex error_log_lock_;

  /*! Выдаёт последовательные номера пакетов для задаваемого соединения
  \param cnt идентификатор соединения, для которого выдать номер пакета
  \return номер пакета в случае успеха. Может вернуться ошибка:
    kOverflowError если счётчик переполнился (нужно выкачать более 3 ТБайт
    одним соединением. TODO оценить ограничение)
    kConnectionAbsentError соединение не найдено */
  uint32_t GetNextPacketIndex(ConnectID cnt);


  /*! Запросить переотправку кэша */
  void RequestUpdate();

  /*! Запросить разбор очереди пакетов на отправку
  Вызываться должно очень часто. Порядка 100 раз в секунду */
  void RequestSendQueue();

  /*! Очистим кэш данных без соединений */
  void ClearDataOrphans();


  // TODO descr
  void SendLivePacket();

  /*! Удаляем коннект из списка коннектов. Предполагается, что коннект уже
  остановил все операции и готов к удалению
  \param cnt идентификатор коннекта */
  void RemoveOutLink(uuids::uuid cnt);

  /*! Отправить подтверждение на приём пакета данных. Подтверждение может
  отсылаться даже при уже удалённом соединении, чтобы не поддерживать спам
  Прим.: такое же подтверждение посылается на закрытие соединения
  \param cnt идентификатор соединения (которое может отсутствовать)
  \param packet_id идентификатор пакета, который подтверждается
  \param socket_index индекс сокета для отправки
  \param target адрес/порт, куда отправлять */
  void SendDataAck(ConnectID cnt, uint32_t packet_id, size_t socket_index,
      boost::asio::ip::udp::endpoint target);
};


/*! \class TrunkClient Клиентская часть транковой (многоканальной)
связи */
class TrunkClient: public TrunkLink {
 public:
  TrunkClient(boost::asio::io_context& ctx,
      const std::vector<boost::asio::ip::udp::endpoint>& trpoints,
      const Settings& cfg, Tracer* tracer);
  virtual ~TrunkClient();

  /*! Добавить новое подключение.  Подключение будет добавлено, функция его\
  зарегистрирует и запустит (вызовет Run).
  \param point идентификатор внешней точки подключения
  \param link экземпляр соединения. Объект не может быть пустым */
  void AddConnect(PointID point, std::shared_ptr<OutLink> link);

  /*! Получить статистику по работе приложения */
  StatInfo GetStat() { return TrunkLink::GetStat(); }

 private:
  TrunkClient() = delete;
  TrunkClient(const TrunkClient&) = delete;
  TrunkClient(TrunkClient&&) = delete;
  TrunkClient& operator=(const TrunkClient&) = delete;
  TrunkClient& operator=(TrunkClient&&) = delete;

  // Данные для подтверждения подключения
  struct PacketConnectCache {
    PacketInfo info;
    std::chrono::steady_clock::time_point
        Deadline;  //!< Время, после которого считается соединение разорванным
    std::chrono::steady_clock::time_point
        NextSend;  //!< Время посылки дублириющей посылки
  };

  /*! Кэш пакетов для установки соединений с серверной частью */
  std::vector<PacketConnectCache> connect_cache_;

  /*! Блокировка для кэша коннект-пакетов */
  std::mutex connect_cache_lock_;

  std::vector<boost::asio::ip::udp::endpoint> points_;

  boost::asio::ip::udp::socket trunk_socket_;

  /*!< Допустимый размер буфера на отправку для сокета. Берётся меньше
   * реального, чтобы был запас на лив-пакеты и др. важные сообщения */
  int trunk_socket_buffer_size_;

  /*!< Cвободный размер буфера на метку времени */
  int trunk_buffer_last_size_;
  std::chrono::steady_clock::time_point trunk_buffer_last_time_;
  std::mutex trunk_buffer_lock_;

  PacketBuffer trunk_read_buffer_;
  boost::asio::ip::udp::endpoint trunk_read_point_;

  /*! Отправить оповещение о новом коннекте на сторону сервера
  \param cnt идентификатор коннекта
  \param point идентификатор внешней точки кодключения
  \param timeout таймаут для обмена данными, мс */
  void SendConnectInformation(
      ConnectID cnt, PointID point, unsigned int timeout);

  void OnCacheResend() override;

  void ReceiveTrunkData();


  // Asio Requesters

  int GetAvailableBuffer(ConnectID ctx) override;

  void SendPacket(PacketInfo pkt) override;
  void SendPacket(size_t socket_index, boost::asio::ip::udp::endpoint target,
      PacketInfo pkt) override;


  void ProcessAckConnectData(
      uuids::uuid cnt, const PacketHeader* info) override;
};


/*! \class TrunkServer Серверная часть транковой (многоканальной)
связи */
class TrunkServer: public TrunkLink {
 public:
  TrunkServer(boost::asio::io_context& ctx,
      const std::vector<std::vector<boost::asio::ip::udp::endpoint>>& trpoints,
      std::function<std::shared_ptr<OutLink>(PointID, ConnectID)> link_fabric,
      const Settings& cfg, Tracer* tracer);
  virtual ~TrunkServer();

  /*! Получить статистику по работе приложения */
  StatInfo GetStat() { return TrunkLink::GetStat(); }

 private:
  TrunkServer() = delete;
  TrunkServer(const TrunkServer&) = delete;
  TrunkServer(TrunkServer&&) = delete;
  TrunkServer& operator=(const TrunkServer&) = delete;
  TrunkServer& operator=(TrunkServer&&) = delete;

  static const size_t kPacketBufferSize = 1000;
  using PacketBuffer = uint8_t[kPacketBufferSize];


  boost::asio::io_context& asio_context_;

  /*! Сокеты для транковой связи. Массив инициализируется в конструкторе и в
  дальнейшем не меняется. Сокеты можно адресовать по индексу в массиве */
  struct ServerSocket {
    size_t trunk_id_;
    boost::asio::ip::udp::socket socket;
    std::shared_ptr<TrunkServer::PacketBuffer> buffer;
    boost::asio::ip::udp::endpoint client_holder;

    // Отслеживание размера буфера на отправку
    /*!< Cвободный размер буфера на метку времени */
    int buffer_last_size_;
    std::chrono::steady_clock::time_point buffer_last_time_;
    /*!< Допустимый размер буфера на отправку для сокета. Берётся меньше
     * реального, чтобы был запас на лив-пакеты и др. важные сообщения */
    int socket_buffer_size_;
  };
  std::vector<ServerSocket> trunk_sockets_;

  std::mutex
      buffer_lock_;  //!< Блокировка для пересчёта свободного размера буфера

  /*! Информация для связи с клиентами по транковой связи: какой сокет
  использовать и конечную точку */
  struct ConnectInfo {
    uuids::uuid connect;
    size_t socket_index;
    boost::asio::ip::udp::endpoint client;
  };
  std::vector<ConnectInfo> clients_link_;
  std::mutex clients_link_lock_;

  std::function<std::shared_ptr<OutLink>(PointID, ConnectID)> link_fabric_;

  // TODO Descr?
  std::shared_ptr<PacketBuffer> GetBuffer();

  /*! Функция инициации (запроса) асинхронного чтения данных по транковой связи
  из порта с индексом index. Если в транковой связи несколько портов, то делать
  запрос чтения нужно по всем портам сразу. Функция неблокирующая, возвращает
  управление сразу
  \param index номер порта в транковой связи (для разделения отдельных портов)
*/
  void RequestReadingTrunk(size_t index);

  // TODO Descr

  void ProcessConnectData(uuids::uuid cnt, const PacketConnect* info,
      size_t socket_index, boost::asio::ip::udp::endpoint target) override;

  int GetAvailableBuffer(ConnectID ctx) override;

  void SendPacket(PacketInfo pkt) override;
  void SendPacket(size_t socket_index, boost::asio::ip::udp::endpoint target,
      PacketInfo pkt) override;


  // TODO Descr
  bool GetPacketConnectID(const void* data, size_t data_size, uuids::uuid& cnt);

  void AddClientLink(ConnectInfo info);
  bool GetClientLink(ConnectInfo& info);
};

#endif  // TRUNKLINK_H

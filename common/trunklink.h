#ifndef TRUNKLINK_H
#define TRUNKLINK_H

#include <mutex>
#include <utility>

#include <boost/asio.hpp>

#include "data.h"
#include "outlink.h"


enum TrunkCommand : uint32_t {
  kTrunkCommandCreateConnect = 1,
  kTrunkCommandReleaseConnect = 2,
  kTrunkCommandAckCreateConnect = 3,
  kTrunkCommandDataOut =
      11,  // Пакет данных с локальной точки на внешний сервер
  kTrunkCommandDataIn = 12,
  kTrunkCommandAckDataOut = 21,
  kTrunkCommandAckDataIn = 22,
  kTrunkCommandLive =
      31,  //!< Live-пакет для поддержания актуальности соединения
};


const size_t kConnectIDSize = 16;
const unsigned int kResendTimeout = 300;
const unsigned int kDeadlineTimeout = 2000;

const size_t kMaxChunkSize = 800;

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

class OutLink;


/*! \class TrunkLink Общая часть алгоритмов транковой связи. TrunkLink не
предназначен для самостоятельного использвоания, только как базовый класс */
class TrunkLink {
 public:
  TrunkLink(boost::asio::io_context& ctx, bool server_side);

  virtual ~TrunkLink() {}

  // TODO Descr
  void SendData(ConnectID cnt, const void* data, size_t data_size);

  /*! Закрыть коннект по сигналу "снаружи": соединение разорвано и т.п.
  Коннект уже может быть закрыт (дубликат события)
  \param cnt идентификатор коннекта */
  void CloseConnect(ConnectID cnt);


 protected:
  static const uint32_t kEmptyPacketID = static_cast<uint32_t>(-1);
  static const uint32_t kBadPacketIndex = static_cast<uint32_t>(-2);

  static const size_t kPacketBufferSize = 1000;
  using PacketBuffer = uint8_t[kPacketBufferSize];

  struct PacketInfo {
    ConnectID CtxID;
    uint32_t PacketID;  // Номер пакета или kEmptyPacketID
    std::shared_ptr<PacketBuffer> PacketData;
    uint32_t PacketSize;
  };

  struct OutLinkInfo {
    uuids::uuid connect_id;
    std::shared_ptr<OutLink> link;
    uint32_t next_index_to_trunk;  //!< Индекс пакета для следующего пакета
  };


  // TODO
  // Массив закрывается out_links_lock_, которая заявлена как protected
  std::vector<OutLinkInfo> out_links_;
  std::mutex out_links_lock_;

  // TODO parameter client - remove ???
  void ProcessTrunkData(boost::asio::ip::udp::endpoint client, const void* data,
      size_t data_size);

  PacketInfo FormPacket(
      const PacketData& header, uint8_t* data, size_t data_size);

  // TODO Descr
  virtual void SendPacket(PacketInfo pkt) = 0;

  // Обработчики отдельных команд
  virtual void ProcessConnectData(uuids::uuid cnt, const PacketConnect* info){};
  virtual void ProcessAckConnectData(
      uuids::uuid cnt, const PacketHeader* info){};

  // TODO Descr
  void ProcessDataToOutlink(
      uuids::uuid cnt, const PacketData* info, const void* data);

  // TODO Descr
  void ProcessAckData(uuids::uuid cnt, const PacketAck* info);


  /*! Внутренняя функция: добавляет внешнюю связь для заданного коннекта.
  Функцию необходимо вызывать с захваченной блокировкой out_links_lock_.
  \param cnt идентификатор подключения
  \param link экземпляр объекта внешней связи */
  void IntAddOutLinkWOLock(uuids::uuid cnt, std::shared_ptr<OutLink> link);

  // TODO
  // Вызов должен быть закрыт out_links_lock_
  std::shared_ptr<OutLink> GetOutLinkWOLock(uuids::uuid cnt);

  std::shared_ptr<OutLink> GetOutLink(uuids::uuid cnt);

  // TODO Descr?
  std::shared_ptr<PacketBuffer> GetBuffer();

  // TODO Descr
  virtual void OnCacheResend();

  // TODO Descr
  void RemoveOutLink(uuids::uuid cnt);

  /*! Послать по транку информацию о разрыве соединения
  \param cnt идентификатор коннекта */
  void SendDisconnectInformation(ConnectID cnt);

  /*! Очистить всю информацию о соединении (перед удалением): пакеты, кэши и
  т.д. \param cnt идентификатор коннекта */
  virtual void ClearConnectInformation(ConnectID cnt);

 private:
  TrunkLink() = delete;
  TrunkLink(const TrunkLink&) = delete;
  TrunkLink(TrunkLink&&) = delete;
  TrunkLink& operator=(const TrunkLink&) = delete;
  TrunkLink& operator=(TrunkLink&&) = delete;


  struct PacketDataCache {  // TODO remove due to parent class
    PacketInfo info;
    std::chrono::steady_clock::time_point
        Deadline;  //!< Время, после которого считается соединение разорванным
    std::chrono::steady_clock::time_point
        NextSend;  //!< Время посылки дублириющей посылки
  };


  static const size_t kResendTick = 100;

  bool server_side_;

  std::vector<PacketDataCache> packet_data_cache_;
  std::mutex packet_data_cache_lock_;
  boost::asio::steady_timer cache_timer_;


  // TODO Descr + kBadPacketIndex
  uint32_t GetNextPacketIndex(ConnectID cnt);

  /*! Запросить переотправку кэша */
  void RequestCacheResend();


  // TODO descr
  void SendLivePacket();


  /*! Очистить кэш для соединения cnt
  \param cnt идентификатор коннекта */
  void ClearDataCache(ConnectID cnt);
};


/*! \class TrunkClient Клиентская часть транковой (многоканальной)
связи */
class TrunkClient: public TrunkLink {
 public:
  TrunkClient(boost::asio::io_context& ctx,
      const std::vector<boost::asio::ip::udp::endpoint>& trpoints);
  virtual ~TrunkClient();

  /*! Добавить новое подключение.  Подключение будет добавлено, функция его\
  зарегистрирует и запустит (вызовет Run).
  \param point идентификатор внешней точки подключения
  \param link экземпляр соединения. Объект не может быть пустым */
  void AddConnect(PointID point, std::shared_ptr<OutLink> link);

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

  std::vector<PacketConnectCache> connect_cache_;
  std::mutex connect_cache_lock_;

  std::vector<boost::asio::ip::udp::endpoint> points_;

  boost::asio::ip::udp::socket trunk_socket_;
  PacketBuffer trunk_read_buffer_;
  boost::asio::ip::udp::endpoint trunk_read_point_;

  std::mt19937 generator_;

  /*! Отправить оповещение о новом коннекте на сторону сервера
  \param cnt идентификатор коннекта
  \param point идентификатор внешней точки кодключения
  \param timeout таймаут для обмена данными, мс */
  void SendConnectInformation(
      ConnectID cnt, PointID point, unsigned int timeout);

  // TODO Descr
  void OnCacheResend() override;

  void ReceiveTrunkData();

  void ClearConnectInformation(ConnectID cnt) override;


  // Asio Requesters


  void SendPacket(PacketInfo pkt) override;

  void ProcessAckConnectData(
      uuids::uuid cnt, const PacketHeader* info) override;
};


/*! \class TrunkServer Серверная часть транковой (многоканальной)
связи */
class TrunkServer: public TrunkLink {
 public:
  TrunkServer(boost::asio::io_context& ctx,
      const std::vector<boost::asio::ip::udp::endpoint>& trpoints,
      std::function<std::shared_ptr<OutLink>(PointID)> link_fabric);
  virtual ~TrunkServer();

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
    boost::asio::ip::udp::socket socket;
    std::shared_ptr<TrunkServer::PacketBuffer> buffer;
    boost::asio::ip::udp::endpoint client_holder;
  };
  std::vector<ServerSocket> trunk_sockets_;

  /*! Информация для связи с клиеннтами по транковой связи: какой сокет
  использовать и конечную точку */
  struct ConnectInfo {
    uuids::uuid connect;
    size_t socket_index;
    boost::asio::ip::udp::endpoint client;
  };
  std::vector<ConnectInfo> clients_link_;
  std::mutex clients_link_lock_;

  std::function<std::shared_ptr<OutLink>(PointID)> link_fabric_;

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

  void ProcessConnectData(uuids::uuid cnt, const PacketConnect* info) override;


  void SendPacket(PacketInfo pkt) override;

  // TODO Descr
  bool GetPacketConnectID(const void* data, size_t data_size, uuids::uuid& cnt);

  void AddClientLink(ConnectInfo info);
  bool GetClientLink(ConnectInfo& info);
};

#endif  // TRUNKLINK_H

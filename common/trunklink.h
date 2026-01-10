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
  kTrunkCommandDataOut = 11,
  kTrunkCommandDataIn = 12,
  kTrunkCommandAckDataOut = 21,
  kTrunkCommandAckDataIn = 22
};


const size_t kConnectIDSize = 16;
const unsigned int kResendTimeout = 300;
const unsigned int kDeadlineTimeout = 5000;

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

/*! \class TrunkClient Клиентская часть транковой (многоканальной)
связи */
class TrunkClient {
 public:
  TrunkClient(boost::asio::io_context& ctx,
      const std::vector<boost::asio::ip::udp::endpoint>& trpoints);
  virtual ~TrunkClient();

  // TODO Change
  /*! Добавить новое подключение к транковой связи
  \param point идентификатор внешней точки подключения
  \param link экземпляр соединения */
  void AddConnect(PointID point, std::shared_ptr<OutLink> link);

  /*! Разорвать коннект. При этом будет вызван обработчик дисконнекта.
  Допустимо разрывать уже разорванный коннект.
  \param cnt идентификатор коннекта */
  void ReleaseConnect(ConnectID cnt) noexcept;

  /*! Отправить оповещение о новом коннекте
  \param cnt идентификатор коннекта
  \param point идентификатор внешней точки кодключения
  \param timeout таймаут для обмена данными, мс */
  void SendConnect(ConnectID cnt, PointID point, unsigned int timeout);

  /*! Отправить данные по коннекту
  \param cnt идентификатор коннекта
  \param data буфер с данными
  \param data_size размер данных на отправку
  \return признак, что данные приняты к отправке. Если коннекта нет, или он
  закрыт, то возвращается false */
  bool SendData(ConnectID cnt, void* data, size_t data_size);

 private:
  TrunkClient() = delete;
  TrunkClient(const TrunkClient&) = delete;
  TrunkClient(TrunkClient&&) = delete;
  TrunkClient& operator=(const TrunkClient&) = delete;
  TrunkClient& operator=(TrunkClient&&) = delete;

  static const size_t kPacketBufferSize = 1000;
  static const size_t kResendTick = 100;
  using PacketBuffer = uint8_t[kPacketBufferSize];
  static const uint32_t kBadPacketIndex = static_cast<uint32_t>(-1);

  struct ConnectInfo {
    ConnectID ID;
    std::shared_ptr<OutLink> Link;
    uint32_t NextIndex;  //!< Индекс пакета для следующего пакета
  };

  struct PacketConnectCache {
    ConnectID CtxID;
    std::shared_ptr<PacketBuffer> PacketData;
    uint32_t PacketSize;
    std::chrono::steady_clock::time_point
        Deadline;  //!< Время, после которого считается соединение разорванным
    std::chrono::steady_clock::time_point
        NextSend;  //!< Время посылки дублириющей посылки
  };


  struct PacketDataCache {
    ConnectID CtxID;
    uint32_t PacketID;
    std::shared_ptr<PacketBuffer> PacketData;
    uint32_t PacketSize;
  };


  std::vector<boost::asio::ip::udp::endpoint> points_;
  boost::asio::ip::udp::socket trunk_socket_;

  std::vector<ConnectInfo> connects_;
  std::mutex connects_lock_;

  std::vector<PacketConnectCache> packet_connect_cache_;
  std::vector<PacketDataCache> packet_data_cache_;
  std::mutex packet_cache_lock_;
  boost::asio::steady_timer cache_timer_;


  std::mt19937 generator_;

  // TODO Descr?
  void SendConnect(TrunkCommand cmd, ConnectID cnt);

  // TODO Descr?
  std::shared_ptr<PacketBuffer> GetBuffer();

  // TODO Descr + kBadPacketIndex
  uint32_t GetPacketIndex(ConnectID cnt);

  // TODO Descr
  void CacheResend();

  // Asio Requesters

  /*! Запросить переотправку кэша */
  void RequestCacheResend();
};


/*! \class TrunkServer Серверная часть транковой (многоканальной)
связи */
class TrunkServer {
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
  std::function<std::shared_ptr<OutLink>(PointID)> link_fabric_;

  // TODO Descr?
  std::shared_ptr<PacketBuffer> GetBuffer();

  // TODO Descr
  void ReceiveTrunkData(std::shared_ptr<boost::asio::ip::udp::socket> socket,
      std::shared_ptr<PacketBuffer> buffer,
      std::shared_ptr<boost::asio::ip::udp::endpoint> client);

  // TODO Descr
  void ProcessTrunkData(boost::asio::ip::udp::endpoint client, const void* data,
      size_t data_size);

  void ProcessConnectData(uuids::uuid cnt, const PacketConnect* info);
};

#endif  // TRUNKLINK_H

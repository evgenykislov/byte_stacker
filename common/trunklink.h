#ifndef TRUNKLINK_H
#define TRUNKLINK_H

#include <mutex>
#include <utility>

#include <boost/asio.hpp>

#include "data.h"


enum TrunkCommand : uint32_t {
  kTrunkCommandCreateConnect = 1,
  kTrunkCommandReleaseConnect = 2,
  kTrunkCommandAckCreateConnect = 3,
  kTrunkCommandDataOut = 11,
  kTrunkCommandDataIn = 12,
  kTrunkCommandAckDataOut = 21,
  kTrunkCommandAckDataIn = 22
};


enum TrunkConnectStatus { kTrunkConnectCreated, kTrunkConnectConfirmed };


/*! \class TrunkClient Клиентская часть транковой (многоканальной)
связи */
class TrunkClient {
 public:
  TrunkClient(boost::asio::io_context& ctx,
      const std::vector<boost::asio::ip::udp::endpoint>& trpoints);
  virtual ~TrunkClient();

  /*! Создать новый "виртуальный" коннект
  \param point идентификатор внешней точки подключения
  \param on_disconnect обработчик события разрыва коннекта
  \param on_data обработчик события прихода данных
  \return идентификатор коннекта. В случае ошибки возвращается пустой
  идентификатор (.is_nil() == true) */
  ConnectID CreateConnect(PointID point,
      std::function<void(ConnectID)> on_disconnect,
      std::function<void(ConnectID, void*, size_t)> on_data);

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
  using PacketBuffer = uint8_t[kPacketBufferSize];
  static const uint32_t kBadPacketIndex = static_cast<uint32_t>(-1);

  struct ConnectInfo {
    ConnectID ID;
    std::function<void(ConnectID)> OnDisconnect;
    std::function<void(ConnectID, void*, size_t)> OnData;
    TrunkConnectStatus Status;
    uint32_t NextIndex;  //!< Индекс пакета для следующего пакета
  };

  struct PacketConnectCache {
    ConnectID CtxID;
    std::shared_ptr<PacketBuffer> PacketData;
    uint32_t PacketSize;
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


  std::mt19937 generator_;

  // TODO Descr?
  void SendConnect(TrunkCommand cmd, ConnectID cnt);

  // TODO Descr?
  std::shared_ptr<PacketBuffer> GetBuffer();

  // TODO Descr + kBadPacketIndex
  uint32_t GetPacketIndex(ConnectID cnt);
};


/*! \class TrunkServer Серверная часть транковой (многоканальной)
связи */
class TrunkServer {};

#endif  // TRUNKLINK_H

#ifndef TRUNKLINK_H
#define TRUNKLINK_H

#include <mutex>
#include <utility>

#include <boost/asio.hpp>

#include "data.h"

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

  struct ConnectInfo {
    ConnectID ID;
    std::function<void(ConnectID)> OnDisconnect;
    std::function<void(ConnectID, void*, size_t)> OnData;
  };

  std::vector<boost::asio::ip::udp::endpoint> points_;

  std::vector<ConnectInfo> connects_;
  std::mutex connect_lock_;


  std::mt19937 generator_;
};


/*! \class TrunkServer Серверная часть транковой (многоканальной)
связи */
class TrunkServer {};

#endif  // TRUNKLINK_H

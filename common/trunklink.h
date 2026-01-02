#ifndef TRUNKLINK_H
#define TRUNKLINK_H

#include <utility>

#include <boost/asio.hpp>

#include "data.h"

/*! \class TrunkClient Клиентская часть транковой (многоканальной)
связи */
class TrunkClient {
 public:
  TrunkClient(boost::asio::io_context& ctx);
  virtual ~TrunkClient();

  /*! Создать новый "виртуальный" коннект
  \param point идентификатор внешней точки подключения
  \param OnDisconnect обработчик события разрыва коннекта
  \param OnData обработчик события прихода данных
  \return идентификатор коннекта. В случае ошибки возвращается пустой
  идентификатор (.is_nil() == true) */
  ConnectID CreateConnect(PointID point,
      std::function<void(ConnectID)> OnDisconnect,
      std::function<void(ConnectID, void*, size_t)> OnData);

  /*! Разорвать коннект
  \param cnt идентификатор коннекта */
  void ReleaseConnect(ConnectID cnt);

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
};


/*! \class TrunkServer Серверная часть транковой (многоканальной)
связи */
class TrunkServer {};

#endif  // TRUNKLINK_H

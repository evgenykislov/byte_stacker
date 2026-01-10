#ifndef OUTLINK_H
#define OUTLINK_H

#include <cstdint>
#include <string>
#include <utility>

#include <boost/asio.hpp>

#include "../common/data.h"

struct AddressPortPoint {
  std::string Address;
  uint16_t Port;
};


class TrunkClient;

/*! \class IOutLink класс для управления внешними tcp-соединениями.
На момент создания экземпляра соединение должно быть установлено.
Механизм работы:
- создаём экземпляр с уже установленным соединением;
- проводим дополнительные регистрации, назначение обработчиков;
- запускаем чтение на сокете: метод Run(); */
class OutLink {
 public:
  OutLink(boost::asio::ip::tcp::socket&& socket);

  OutLink(OutLink&& arg) = default;
  OutLink& operator=(OutLink&& arg) = default;
  virtual ~OutLink();

  // TODO descr
  void Run(TrunkClient* hoster, ConnectID cnt);

 private:
  OutLink() = delete;
  OutLink(const OutLink&) = delete;
  OutLink& operator=(const OutLink&) = delete;

  static const size_t kChunkSize = 800;

  boost::asio::ip::tcp::socket socket_;
  char read_buffer_[kChunkSize];

  // TODO Descr + однопоточный
  void RequestRead();
};


std::shared_ptr<OutLink> CreateOutLink(PointID point);

std::shared_ptr<OutLink> CreateOutLink(
    PointID point, boost::asio::ip::tcp::socket&& socket);

#endif  // OUTLINK_H

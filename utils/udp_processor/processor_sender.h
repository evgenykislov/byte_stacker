#ifndef PROCESSOR_SENDER_H
#define PROCESSOR_SENDER_H

#include <memory>

#include <boost/asio.hpp>

#include "processor.h"

// Класс для выдачи пакетов в сеть
class ProcessorSender: public Processor {
 public:
  /*! Конструирование
  \param socket отправлять пакеты через заданный сокет
  \param point отправлять пакеты на заданную точку */
  ProcessorSender(boost::asio::io_context& ctx, boost::asio::ip::udp::socket& socket,
      boost::asio::ip::udp::endpoint point): Processor(ctx, nullptr), socket_(socket), point_(point) {
  }

  /*! Отправить пакет в обработку */
  virtual void Push(PacketInfoPtr packet) {
    socket_.async_send_to(boost::asio::buffer(packet->data_, packet->size_),
        point_,
        [packet](boost::system::error_code e /*ec*/, std::size_t /*bytes_sent*/) {
        });
  }

 private:
  boost::asio::ip::udp::socket& socket_;
  boost::asio::ip::udp::endpoint point_;
};

#endif // PROCESSOR_SENDER_H

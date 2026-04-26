#ifndef PROCESSOR_H
#define PROCESSOR_H

#include <memory>

#include <boost/asio.hpp>


/*! Структура для описания одного пакета */
const size_t kMaxPacketSize = 2000;
struct PacketInfo {
  size_t size_;
  uint8_t data_[kMaxPacketSize];  //!< Данные самого пакета
};
using PacketInfoPtr = std::shared_ptr<PacketInfo>;


class Processor {
 public:
  Processor(boost::asio::io_context& ctx, std::shared_ptr<Processor> next) {}
  virtual ~Processor() {}
  virtual void Push(PacketInfoPtr packet) = 0;

 protected:
  std::shared_ptr<Processor> next_processor_;

 private:
  Processor(const Processor&) = delete;
  Processor(Processor&&) = delete;
  Processor& operator=(const Processor&) = delete;
  Processor& operator=(Processor&&) = delete;
};

using ProcessorPtr = std::shared_ptr<Processor>;

#endif // PROCESSOR_H

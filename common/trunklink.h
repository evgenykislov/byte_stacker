#ifndef TRUNKLINK_H
#define TRUNKLINK_H

#include <utility>

#include <boost/asio.hpp>

/*! \class TrunkClient Клиентская часть транковой (многоканальной)
связи */
class TrunkClient {
 public:
  TrunkClient(boost::asio::io_context& ctx);
  virtual ~TrunkClient();

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

#ifndef INLINK_H
#define INLINK_H

#include <memory>
#include <utility>

#include <boost/asio.hpp>

namespace bai = boost::asio::ip;

using TcpSocket = std::shared_ptr<boost::asio::ip::tcp::socket>;

class InLink {
 public:
  InLink(TcpSocket s);
  InLink(InLink&& arg);
  InLink& operator=(InLink&& arg);
  virtual ~InLink() {}

 private:
  InLink() = delete;
  InLink(const InLink&) = delete;
  InLink& operator=(const InLink&) = delete;

  TcpSocket socket_;
};

#endif  // INLINK_H

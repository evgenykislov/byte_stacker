#include "outlink.h"

#include <iostream>

// TODO Remove?
std::shared_ptr<OutLink> CreateOutLink(PointID point) {
  std::cout << "Debug: create link to point " << point << std::endl;
  return nullptr;
}

OutLink::OutLink(boost::asio::ip::tcp::socket&& socket)
    : socket_(std::move(socket)) {}

void OutLink::RequestRead() {
  socket_.async_read_some(boost::asio::buffer(read_buffer_),
      [this](
          const boost::system::error_code& err, std::size_t bytes_transferred) {
        //


        RequestRead();
      });
}


OutLink::~OutLink() { socket_.close(); }

void OutLink::Run(TrunkClient* hoster, ConnectID cnt) { RequestRead(); }


// TODO Remove??
std::shared_ptr<OutLink> CreateOutLink(
    PointID point, boost::asio::ip::tcp::socket&& socket) {
  return nullptr;
}

#include "outlink.h"

#include <iostream>

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

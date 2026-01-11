#include "outlink.h"

#include <iostream>

OutLink::OutLink(boost::asio::ip::tcp::socket&& socket)
    : socket_(std::move(socket)), resolver_(socket_.get_executor()) {}


OutLink::OutLink(
    boost::asio::io_context& ctx, std::string address, uint16_t port)
    : socket_(ctx),
      resolver_(ctx),
      host_(address),
      service_(std::to_string(port)) {}


void OutLink::RequestRead() {
  socket_.async_read_some(boost::asio::buffer(read_buffer_),
      [this](
          const boost::system::error_code& err, std::size_t bytes_transferred) {
        //


        RequestRead();
      });
}

void OutLink::RequestConnect() {
  if (resolved_points_.empty()) {
    // TODO Process errors
    return;
  }

  // TRACE
  auto ep = resolved_points_.front();
  std::printf(
      "-- Try connect to %s:%u\n", ep.address().to_string().c_str(), ep.port());

  socket_.async_connect(
      resolved_points_.front(), [this](const boost::system::error_code& error) {
        if (error) {
          // Неподключились. Текущую точку удаляем, берём следующую
          resolved_points_.pop_front();
          RequestConnect();
        } else {
          // TRACE
          std::printf("-- Connected\n");

          RequestRead();
        }
      });
}


OutLink::~OutLink() { socket_.close(); }

void OutLink::Run(TrunkLink* hoster, ConnectID cnt) {
  if (socket_.is_open()) {
    RequestRead();
  } else {
    // TRACE
    std::printf("-- Resolving host %s:%s\n", host_.c_str(), service_.c_str());

    resolver_.async_resolve(host_, service_,
        [this](const boost::system::error_code& err,
            boost::asio::ip::tcp::resolver::results_type results) {
          if (err) {
            // TODO Process errors
          } else {
            for (auto it = results.begin(); it != results.end(); ++it) {
              resolved_points_.push_back(*it);
            }

            // Прим.: пустой список конечных точек - это поведение будет
            // обработано на этапе коннекта

            RequestConnect();
          }
        });
  }
}

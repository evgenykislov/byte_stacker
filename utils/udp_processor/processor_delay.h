#ifndef PROCESSOR_DELAY_H
#define PROCESSOR_DELAY_H

#include <deque>
#include <iostream>
#include <mutex>
#include <string>

#include <boost/asio.hpp>

#include "processor.h"

class ProcessorDelay: public Processor {
 public:
  ProcessorDelay(boost::asio::io_context& ctx, std::shared_ptr<Processor> next,
      std::string value)
      : Processor(next), tick_timer_(ctx) {
    delay_ = std::chrono::milliseconds(0);
    try {
      delay_ = std::chrono::milliseconds(std::stoul(value));
    } catch (std::exception&) {
      std::cerr << "Delay parameter error. Set minimal delaying" << std::endl;
    }

    OnTick();
  }

  /*! Отправить пакет в обработку */
  virtual void Push(PacketInfoPtr packet) {
    std::lock_guard<std::mutex> lk(items_lock_);
    items_.push_back({packet, std::chrono::steady_clock::now() + delay_});
  }

 private:
  struct Item {
    PacketInfoPtr packet;
    std::chrono::steady_clock::time_point send_time;
  };

  const int kTickIntervalMs = 20;
  std::chrono::steady_clock::duration delay_;
  boost::asio::steady_timer tick_timer_;

  std::deque<Item> items_;
  std::mutex items_lock_;


  void OnTick() {
    tick_timer_.expires_after(std::chrono::milliseconds(kTickIntervalMs));
    tick_timer_.async_wait([this](const boost::system::error_code& err) {
      if (err) {
        // Отменили все операции, закрываем приложение
        return;
      }

      std::vector<PacketInfoPtr> sending;
      std::unique_lock<std::mutex> lk(items_lock_);
      auto t = std::chrono::steady_clock::now();
      while (!items_.empty() && items_.front().send_time < t) {
        sending.push_back(items_.front().packet);
        items_.pop_front();
      }
      lk.unlock();

      for (auto& i : sending) {
        next_processor_->Push(i);
      }

      OnTick();
    });
  }
};


#endif  // PROCESSOR_DELAY_H

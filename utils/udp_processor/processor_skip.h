#ifndef PROCESSOR_SKIP_H
#define PROCESSOR_SKIP_H

#include <iostream>
#include <string>

#include <boost/asio.hpp>

#include "processor.h"

class ProcessorSkip: public Processor {
 public:
  ProcessorSkip(std::shared_ptr<Processor> next, std::string value)
      : Processor(next) {
    try {
      n_ = std::stoul(value);
    } catch (std::exception&) {
      std::cerr << "Skip parameter error. Disable skipping" << std::endl;
      n_ = 0;
    }
    counter_ = 0;
  }

  /*! Отправить пакет в обработку */
  virtual void Push(PacketInfoPtr packet) {
    if (n_ >= 2) {
      ++counter_;
      if (counter_ == n_) {
        counter_ = 0;
        return;  // Пропускаем пакет
      }
    }

    next_processor_->Push(packet);
  }

 private:
  unsigned long n_;
  unsigned long counter_;
};


class ProcessorSkipConnection: public Processor {
 public:
  ProcessorSkipConnection(std::shared_ptr<Processor> next, std::string value)
      : Processor(next) {
    try {
      n_ = std::stoul(value);
    } catch (std::exception&) {
      std::cerr << "Skip parameter error. Disable skipping" << std::endl;
      n_ = 0;
    }
    counter_ = 0;
  }

  /*! Отправить пакет в обработку */
  virtual void Push(PacketInfoPtr packet) {
    if (packet->size_ >= 20) {
      if (*(uint32_t*)&packet->data_[16] == 1) {
        // Это команда на установление соединения
        if (counter_ < n_) {
          ++counter_;
          return; // Пропускаем пакет
        }
      }
    }

    next_processor_->Push(packet);
  }

 private:
  unsigned long n_;
  unsigned long counter_;
};


#endif  // PROCESSOR_SKIP_H

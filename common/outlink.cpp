#include "outlink.h"

#include <iostream>

#include "trunklink.h"

OutLink::OutLink(boost::asio::ip::tcp::socket&& socket)
    : socket_(std::move(socket)),
      resolver_(socket_.get_executor()),
      hoster_(nullptr),
      next_write_chunk_id_{0} {
  // Сбрасываем блокировку на запись - можно записывать
  network_write_operation_.clear();
}


void OutLink::FillNetworkBuffer() {
  std::lock_guard lk(write_chunks_lock_);
  while (!write_chunks_.empty()) {
    auto it = write_chunks_.begin();
    assert(it->first >= next_write_chunk_id_);
    if (it->first != next_write_chunk_id_) {
      // Нет нужного пакета
      break;
    }

    ++next_write_chunk_id_;
    network_write_buffer_.insert(
        network_write_buffer_.end(), it->second.begin(), it->second.end());
    // TODO Process memory crash

    write_chunks_.erase(it);
  }
}


OutLink::OutLink(
    boost::asio::io_context& ctx, std::string address, uint16_t port)
    : socket_(ctx),
      resolver_(ctx),
      host_(address),
      service_(std::to_string(port)),
      hoster_(nullptr),
      next_write_chunk_id_{0} {
  // Устанавливаем блокировку на запись - запись только после коннекта
  network_write_operation_.test_and_set();
}


void OutLink::RequestRead() {
  //  std::printf("TRACE: -- Request read for outlink socket\n");
  socket_.async_read_some(boost::asio::buffer(read_buffer_),
      [this](
          const boost::system::error_code& err, std::size_t bytes_transferred) {
        //        std::printf("TRACE: -- Read some from outlink socket\n");
        // Вне зависимости от ошибок чтения, если есть вычитанные данные - их
        // обрабатываем
        if (bytes_transferred > 0) {
          assert(hoster_);
          hoster_->SendData(selfid_, read_buffer_, bytes_transferred);
        }

        if (err) {
          // Ошибка чтения. Обычные ситуации:
          // - закрыто соединение (boost::asio::error::eof)
          // - операция прервана. например, закрывается сам сокет
          // (boost::asio::error::operation_aborted)
          // - другие тоже бывают (ресурсы отобрали и т.д.)
          // std::printf("TRACE: -- Read error of outlink: %s\n",
          // err.message().c_str());
          hoster_->CloseConnect(selfid_);
          return;
        }

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
          assert(network_write_operation_.test());
          RequestWrite();
        }
      });
}


void OutLink::RequestWrite() {
  assert(network_write_operation_.test());
  // TODO LOCK-LOCK
  FillNetworkBuffer();

  if (network_write_buffer_.empty()) {
    network_write_operation_.clear();
    // Проверим опять чанки. Если ничего полезного не появилось - выходим
    std::unique_lock lk(write_chunks_lock_);
    if (write_chunks_.find(next_write_chunk_id_) == write_chunks_.end()) {
      return;
    }
    lk.unlock();

    // Появилось что-то интересное. Попробуем заново включить запись?
    if (network_write_operation_.test_and_set()) {
      // Запись уже идёт, перехватили
      return;
    }

    // Запись опять наша. Заполним буфер
    FillNetworkBuffer();
  }

  //  std::printf("TRACE: -- Writing %u bytes to outlink socket\n",
  //      network_write_buffer_.size());

  socket_.async_write_some(boost::asio::buffer(network_write_buffer_.data(),
                               network_write_buffer_.size()),
      [this](const boost::system::error_code& error,
          std::size_t bytes_transferred) {
        if (error) {
          // TODO Process Error
          std::printf("TRACE: -- Writing outlink error\n");
          return;
        }

        if (bytes_transferred == 0) {
          // TODO Error???
          std::printf("TRACE: -- Writing outlink zero-error\n");
          return;
        }

        if (bytes_transferred > network_write_buffer_.size()) {
          assert(false);
          // TODO ERRRORR
          return;
        }

        network_write_buffer_.erase(network_write_buffer_.begin(),
            network_write_buffer_.begin() + bytes_transferred);
        RequestWrite();
      });
}


OutLink::~OutLink() { socket_.close(); }

void OutLink::Run(TrunkLink* hoster, ConnectID cnt) {
  assert(hoster);
  hoster_ = hoster;
  selfid_ = cnt;

  if (socket_.is_open()) {
    assert(!network_write_operation_.test());
    RequestRead();
  } else {
    // TRACE
    std::printf("-- Resolving host %s:%s\n", host_.c_str(), service_.c_str());

    assert(network_write_operation_.test());
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


void OutLink::SendData(uint32_t chunk_id, const void* data, size_t data_size) {
  std::unique_lock lk(write_chunks_lock_);
  if (chunk_id < next_write_chunk_id_) {
    // Пришёл очень старый пакет. Отбрасываем его
    return;
  }
  auto chunk = write_chunks_.find(chunk_id);
  if (chunk != write_chunks_.end()) {
    // Такой чанк уже есть, пришёл дубликат. Отбрасываем его
    return;
  }

  // Добавим чанк
  auto ud = static_cast<const uint8_t*>(data);
  write_chunks_.insert(
      std::make_pair(chunk_id, std::vector<uint8_t>(ud, ud + data_size)));

  if (chunk_id != next_write_chunk_id_) {
    // Пришедший пакет слишком новый, сначала нужно получить другой. Пока ждём
    return;
  }

  lk.unlock();

  // Нужно запустить запись в сеть (если она ещё не запущена)
  bool prev_value = network_write_operation_.test_and_set();
  if (prev_value) {
    // Запись уже идёт. Всё запущено
    return;
  }

  boost::asio::post(socket_.get_executor(), [this]() { RequestWrite(); });
}

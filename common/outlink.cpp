#include "outlink.h"

#include <iostream>

#include "inttypes.h"

#include "settings.h"
#include "trace.h"
#include "tracer.h"
#include "trunklink.h"


#ifdef CONNECT_LOG
const char kLogPrefix[] = "/var/log/stacker/cnt_";
#endif

std::shared_ptr<OutLink> OutLink::CreateOutLink(ConnectID cnt,
    boost::asio::ip::tcp::socket&& socket, const Settings& cfg,
    Tracer* tracer) {
  return std::shared_ptr<OutLink>(
      new OutLink(cnt, std::move(socket), cfg, tracer));
}


std::shared_ptr<OutLink> OutLink::CreateOutLink(ConnectID cnt,
    boost::asio::io_context& ctx, std::string address, uint16_t port,
    const Settings& cfg, Tracer* tracer) {
  return std::shared_ptr<OutLink>(
      new OutLink(cnt, ctx, address, port, cfg, tracer));
}


OutLink::OutLink(ConnectID cnt, boost::asio::ip::tcp::socket&& socket,
    const Settings& cfg, Tracer* tracer)
    : connect_id_(cnt),
      tracer_(tracer),
      socket_(std::move(socket)),
      resolver_(socket_.get_executor()),
      hoster_(nullptr),
      cfg_settings_(cfg),
      read_processing_(false),
      write_processing_(false),
      connected_socket_(true),
      written_volume_(0),
      otherside_written_volume_(0),
      read_volume_(0),
      stop_write_chunk_id_(kUndefinedChunkID),
      stop_after_all_write_(false),
      stop_write_immediate_(false),
      next_write_chunk_id_{0},
      write_idle_timer_(socket_.get_executor()),
      read_idle_timer_(socket_.get_executor()) {
  close_invoked_.clear();
}


void OutLink::FillNetworkBuffer() {
  std::lock_guard lk(write_chunks_lock_);
  if (stop_write_chunk_id_ != kUndefinedChunkID &&
      stop_write_chunk_id_ <= next_write_chunk_id_) {
    stop_after_all_write_ = true;
    write_chunks_.clear();
    return;
  }

  while (!write_chunks_.empty()) {
    auto it = write_chunks_.begin();
    assert(it->first >= next_write_chunk_id_);
    if (it->first != next_write_chunk_id_) {
      // Нет нужного пакета
      break;
    }

    // Нашёлся пакет с идентификатором next_write_chunk_id

    ++next_write_chunk_id_;
    network_write_buffer_.insert(
        network_write_buffer_.end(), it->second.begin(), it->second.end());
    // TODO Process memory crash

    write_chunks_.erase(it);

    if (stop_write_chunk_id_ != kUndefinedChunkID &&
        stop_write_chunk_id_ <= next_write_chunk_id_) {
      stop_after_all_write_ = true;
      write_chunks_.clear();
      if (tracer_) {
        tracer_->Message(connect_id_, "Got stop chunk");
      }
      break;
    }
  }
}


void OutLink::CancelReadWrite() {
  boost::system::error_code err;
  socket_.cancel(err);
  // Отмена всех операций может вернуть ошибку - это штатное поведение.
  // Скорее всего, где уже ранее вызвался cancel() на сокете
  // Ошибку не обрабатываем

  std::lock_guard lk(write_chunks_lock_);
  stop_write_immediate_ = true;
  write_idle_timer_.cancel();
  read_idle_timer_.cancel();

  if (tracer_) {
    tracer_->Message(connect_id_, "Cancelled all read-write operations");
  }
}


OutLink::OutLink(ConnectID cnt, boost::asio::io_context& ctx,
    std::string address, uint16_t port, const Settings& cfg, Tracer* tracer)
    : connect_id_(cnt),
      tracer_(tracer),
      socket_(ctx),
      resolver_(ctx),
      host_(address),
      service_(std::to_string(port)),
      hoster_(nullptr),
      cfg_settings_(cfg),
      read_processing_(false),
      write_processing_(false),
      connected_socket_(false),
      written_volume_(0),
      otherside_written_volume_(0),
      read_volume_(0),
      stop_write_chunk_id_(kUndefinedChunkID),
      stop_after_all_write_(false),
      stop_write_immediate_(false),
      next_write_chunk_id_{0},
      write_idle_timer_(ctx),
      read_idle_timer_(ctx) {
  close_invoked_.clear();
}


void OutLink::RequestRead() {
  assert(read_processing_.load());
  // Посчитаем объём недоставленных данных. Может пока повременить с чтением
  uint64_t rv = read_volume_;
  uint64_t w2 = otherside_written_volume_;
  assert(
      w2 <= rv);  // Обычно объём уже доставленных данным не больше отправленных
  if (w2 > rv) {
    // Какая-то суровая логическая ошибка
    LogWrite(": CLOSE: Volume logic error\n");
    read_processing_ = false;
    CancelReadWrite();
    CheckReadyClose();
    return;
  }

  uint64_t dw = rv - w2;  // Данных в доставке
  if (dw > kMaxProcessingDataSize) {
    // Пока подождём
    if (tracer_) {
      tracer_->Message(connect_id_, "-------- Idle now");
    }

    RequestReadIdle();
    return;
  }


  auto selfptr = shared_from_this();
  socket_.async_read_some(boost::asio::buffer(read_buffer_),
      [selfptr](
          const boost::system::error_code& err, std::size_t bytes_transferred) {
        selfptr->RequestReadProcessing(err, bytes_transferred);
      });
}


void OutLink::RequestReadProcessing(
    const boost::system::error_code& err, std::size_t bytes_transferred) {
  if (!err) {
    // Пришли данные
    if (bytes_transferred > 0) {
      read_volume_ += bytes_transferred;

      if (tracer_) {
        std::stringstream ss;
        ss << "Read " << bytes_transferred << " bytes (summ: " << read_volume_
           << ")";
        tracer_->Message(connect_id_, ss.str());
      }

      assert(hoster_);
      hoster_->SendData(selfid_, read_buffer_, bytes_transferred);
    }
  } else {
    // Есть ошибки
    assert(err);

    if (tracer_) {
      tracer_->Message(connect_id_, "Reading returns error/cancel");
    }

    if (err == boost::asio::error::operation_aborted) {
      // Отменили все операции: кто-то вызвал cancel(). Ничего не делаем.
    } else if (err == boost::asio::error::eof ||
               err == boost::asio::error::connection_reset ||
               err == boost::asio::error::connection_aborted) {
      // Соединение закрыто. По тем или иным причинам
      if (tracer_) {
        tracer_->Message(connect_id_, "  Reading: connection closed");
      }

      connected_socket_ = false;
    }
    // Ошибка чтения. Обычные ситуации:
    // - закрыто соединение (boost::asio::error::eof)
    // - операция прервана. например, закрывается сам сокет
    // (boost::asio::error::operation_aborted)
    // - другие тоже бывают (ресурсы отобрали и т.д.)
    // trlog("-- Read error of outlink: %s\n",
    // err.message().c_str());

    read_processing_ = false;
    CancelReadWrite();
    CheckReadyClose();
    return;
  }

  RequestRead();
}

void OutLink::RequestReadIdle() {
  auto selfptr = shared_from_this();
  std::chrono::milliseconds intrv{kReadIdleTimeout};
  read_idle_timer_.expires_after(intrv);
  read_idle_timer_.async_wait([selfptr](const boost::system::error_code& err) {
    if (selfptr->tracer_) {
      selfptr->tracer_->Message(selfptr->connect_id_, "Idle finished");
    }

    if (err) {
      // Ошибка на ожидание перед чтением.
      // Скорее всего всё закрывается
      if (selfptr->tracer_) {
        selfptr->tracer_->Message(selfptr->connect_id_, "  idle-cancel-all");
      }

      selfptr->read_processing_ = false;
      selfptr->CancelReadWrite();
      selfptr->CheckReadyClose();
      return;
    }

    if (selfptr->tracer_) {
      selfptr->tracer_->Message(selfptr->connect_id_, "Reading go");
    }

    selfptr->RequestRead();
  });
}


void OutLink::RequestConnect() {
  if (resolved_points_.empty()) {
    if (tracer_) {
      tracer_->Message(
          connect_id_, "Nowhere to connect: resolving hasn't any points");
    }

    // TODO Process errors
    return;
  }

  if (tracer_) {
    std::stringstream ss;
    auto ep = resolved_points_.front();
    ss << "Connecting to " << ep.address().to_string() << ":" << ep.port();
    tracer_->Message(connect_id_, ss.str());
  }

  auto selfptr = shared_from_this();
  socket_.async_connect(resolved_points_.front(),
      [selfptr](const boost::system::error_code& error) {
        selfptr->RequestConnectProcessing(error);
      });
}

void OutLink::RequestConnectProcessing(const boost::system::error_code& error) {
  if (error) {
    if (tracer_) {
      std::stringstream ss;
      ss << "Can't connect: " << error.message();
      tracer_->Message(connect_id_, ss.str());
    }

    // Неподключились. Текущую точку удаляем, берём следующую
    resolved_points_.pop_front();
    RequestConnect();
  } else {
    if (tracer_) {
      tracer_->Message(connect_id_, "Connected now. Start reading and writing");
    }

    read_processing_ = true;
    connected_socket_ = true;
    RequestRead();
    write_processing_ = true;
    RequestWrite();
  }
}


void OutLink::RequestWrite() {
  assert(write_processing_.load());

  std::unique_lock lk(write_chunks_lock_);
  if (stop_write_immediate_) {
    if (tracer_) {
      tracer_->Message(connect_id_, "Stop writing now");
    }

    CancelReadWrite();
    write_processing_ = false;
    CheckReadyClose();
    return;
  }
  FillNetworkBuffer();
  if (network_write_buffer_.empty()) {
    if (stop_after_all_write_) {
      if (tracer_) {
        tracer_->Message(
            connect_id_, "PreStop: All data written. Cancel and Stop");
      }

      CancelReadWrite();
      write_processing_ = false;
      CheckReadyClose();
      return;
    }

    // Пока нечего передавать - включаем ожидание
    if (tracer_) {
      tracer_->Message(connect_id_, "---- Nothing to write. Waiting");
    }

    //    trlog("-- Nothing write. Use idle timeout\n");
    auto selfptr = shared_from_this();
    std::chrono::milliseconds intrv{kWriteIdleTimeout};
    write_idle_timer_.expires_after(intrv);
    write_idle_timer_.async_wait([selfptr](
                                     const boost::system::error_code& err) {
      if (selfptr->tracer_) {
        selfptr->tracer_->Message(selfptr->connect_id_, "  Return to writing");
      }

      if (!err) {
        // TODO Error or NotError
      }

      selfptr->RequestWrite();
    });
    return;
  }
  lk.unlock();

  if (tracer_) {
    std::stringstream ss;
    ss << "Writing " << network_write_buffer_.size() << " bytes";
    tracer_->Message(connect_id_, ss.str());
  }

  auto selfptr = shared_from_this();
  socket_.async_write_some(boost::asio::buffer(network_write_buffer_.data(),
                               network_write_buffer_.size()),
      [selfptr](const boost::system::error_code& error,
          std::size_t bytes_transferred) {
        selfptr->RequestWriteProcessing(error, bytes_transferred);
      });
}


void OutLink::RequestWriteProcessing(
    const boost::system::error_code& error, std::size_t bytes_transferred) {
  // Проверка на всякие ошибки
  if (error || bytes_transferred == 0) {
    if (tracer_) {
      tracer_->Message(connect_id_, "Write operation returns error");
    }

    write_processing_ = false;
  }
  if (bytes_transferred > network_write_buffer_.size()) {
    if (tracer_) {
      tracer_->Message(connect_id_, "LOGIC_ERROR: Writes over-more data");
    }

    write_processing_ = false;
  }

  if (!write_processing_) {
    socket_.cancel();
    CheckReadyClose();
    return;
  }

  written_volume_ += bytes_transferred;
  if (tracer_) {
    std::stringstream ss;
    ss << "Written " << bytes_transferred
       << " bytes. Summ: " << written_volume_.load();
    tracer_->Message(connect_id_, ss.str());
  }

  network_write_buffer_.erase(network_write_buffer_.begin(),
      network_write_buffer_.begin() + bytes_transferred);

  RequestWrite();
}


void OutLink::CheckReadyClose() {
  auto selfptr = shared_from_this();
  boost::asio::post(socket_.get_executor(),
      [selfptr]() { selfptr->CheckReadyCloseProcessing(); });
}


void OutLink::CheckReadyCloseProcessing() {
  if (!read_processing_ && !write_processing_) {
    // Готовый к вызову удалителя
    if (!close_invoked_.test_and_set()) {
      if (tracer_) {
        tracer_->Message(connect_id_, "Check ready-invoke to close");
      }

      // Ранее закрытие ещё не вызывалось
      if (socket_.is_open()) {
        boost::system::error_code error;

        // Для подключенного сокета вызываем shutdown
        if (connected_socket_) {
          socket_.shutdown(boost::asio::socket_base::shutdown_both, error);
          if (error) {
            if (tracer_) {
              std::stringstream ss;
              ss << "Socket shutdown returns error: " << error.message();
              tracer_->Message(connect_id_, ss.str());
            }
          }
        }

        socket_.close(error);
        if (error) {
          if (tracer_) {
            std::stringstream ss;
            ss << "Socket close returns error: " << error.message();
            tracer_->Message(connect_id_, ss.str());
          }
        }
        assert(!socket_.is_open());
      }
      hoster_->CloseConnect(selfid_);
    }
  }
}

void OutLink::ResolverProcessing(const boost::system::error_code& err,
    boost::asio::ip::tcp::resolver::results_type results) {
  if (err) {
    // Неизвестный адрес, непонятно куда подключаться
    // Завершаем работу коннекта
    if (tracer_) {
      tracer_->Message(connect_id_, "Unknown connection point");
    }

    CheckReadyClose();
    return;
  }

  // Получили список адресов для подключения
  for (auto it = results.begin(); it != results.end(); ++it) {
    resolved_points_.push_back(*it);
  }

  // Прим.: пустой список конечных точек - это поведение будет
  // обработано на этапе коннекта
  if (tracer_) {
    tracer_->Message(connect_id_, "Resolving completed. Start connecting");
  }

  RequestConnect();
}


OutLink::~OutLink() {
  // Возможно удаление с открытым сокетом, когда сделали force-закрытие по
  // доптаймауту
}

void OutLink::Run(TrunkLink* hoster,
    ConnectID cnt) {  // TODO Remove cnt argument. Outlink knows its connection
                      // id at constructing
  assert(hoster);
  hoster_ = hoster;
  selfid_ = cnt;
  selfid_str_ = uuids::to_string(cnt);

  if (tracer_) {
    tracer_->Message(connect_id_, "++ Running");
  }

  if (socket_.is_open()) {
    if (tracer_) {
      tracer_->Message(connect_id_, "Request reading and writing");
    }

    read_processing_ = true;
    RequestRead();
    write_processing_ = true;
    RequestWrite();
  } else {
    if (tracer_) {
      std::stringstream ss;
      ss << "Run outlink for " << host_ << ":" << service_;
      tracer_->Message(connect_id_, ss.str());
    }

    auto selfptr = shared_from_this();
    resolver_.async_resolve(host_, service_,
        [selfptr](const boost::system::error_code& err,
            boost::asio::ip::tcp::resolver::results_type results) {
          selfptr->ResolverProcessing(err, results);
        });
  }
}


void OutLink::SendData(uint32_t chunk_id, const void* data, size_t data_size) {
  std::lock_guard lk(write_chunks_lock_);
  if (chunk_id < next_write_chunk_id_) {
    // Пришёл очень старый пакет. Отбрасываем его
    if (tracer_) {
      std::stringstream ss;
      ss << "? Strange packet to write " << data_size << " bytes. Drop It!";
      tracer_->Message(connect_id_, ss.str());
    }

    return;
  }
  if (stop_write_chunk_id_ != kUndefinedChunkID &&
      chunk_id >= stop_write_chunk_id_) {
    // Пришёл пакет после закрытия соединения
    // Как-бы это ошибка логики. В любом случае, этот пакет отбрасывается
    if (tracer_) {
      std::stringstream ss;
      ss << "? Packet to write after closing. Size " << data_size
         << " bytes. And Drop It!";
      tracer_->Message(connect_id_, ss.str());
    }

    assert(false);
    return;
  }

  if (tracer_) {
    std::stringstream ss;
    ss << "Get chunk to write. ID: " << chunk_id;
    tracer_->Message(connect_id_, ss.str());
  }

  auto chunk = write_chunks_.find(chunk_id);
  if (chunk != write_chunks_.end()) {
    // Такой чанк уже есть, пришёл дубликат. Отбрасываем его
    if (tracer_) {
      tracer_->Message(connect_id_, "  Chunk double. Drop!");
    }

    return;
  }

  // Добавим чанк
  auto ud = static_cast<const uint8_t*>(data);
  write_chunks_.insert(
      std::make_pair(chunk_id, std::vector<uint8_t>(ud, ud + data_size)));

  if (chunk_id != next_write_chunk_id_) {
    // Пришедший пакет слишком новый, сначала нужно получить другой. Пока ждём
    if (tracer_) {
      tracer_->Message(connect_id_, "Chunk from neat future. Some wait");
    }

    return;
  }

  write_idle_timer_.cancel();  // Отменяем таймер на ожидание следующей записи
}


void OutLink::Stop(uint32_t stop_chunk, StopReason reason) {
  if (tracer_) {
    std::string msg = "Stop outlink due to trunk request: ";
    switch (reason) {
      case kStopReleaseCommand:
        // Корректное завершение, всё ок
        msg += "Closing successfull\n";
        break;
      case kStopNoLive:
        msg += "FORCE CLOSE: no-live\n";
        break;
      case kStopChunkAbsent:
        msg += ": CLOSE: chunk absent\n";
        break;
      default:
        msg += ": CLOSE: unknown reason\n";
    }

    tracer_->Message(connect_id_, msg);
  }

  std::unique_lock lk(write_chunks_lock_);
  if (stop_write_chunk_id_ != kUndefinedChunkID &&
      stop_write_chunk_id_ < stop_chunk) {
    // Остановка уже инициирована и указан более ранний чанк
    if (tracer_) {
      tracer_->Message(connect_id_, "? Stop has already requested early");
    }
    return;
  }

  // Инициировали остановку
  // Или задали новые параметры остановки: напораньше
  if (stop_chunk <= next_write_chunk_id_) {
    // Фактически уже всё передали. Возможно даже и с опозданием.
    // В любом случае, данных больше не планируется
    // Закрываемся тем, что есть на текущий момент
    //    trlog("Outlink close on current point\n");
    if (tracer_) {
      tracer_->Message(connect_id_, "Stop now");
    }

    stop_write_chunk_id_ = next_write_chunk_id_;
    stop_after_all_write_ = true;
    write_chunks_.clear();
    write_idle_timer_.cancel();
    read_idle_timer_.cancel();
    return;
  }

  // Так, планируются ещё данные к передаче
  assert(stop_chunk > next_write_chunk_id_);
  if (tracer_) {
    std::stringstream ss;
    ss << "Close will be later. Needs to send "
       << stop_chunk - next_write_chunk_id_ << " packets";
    tracer_->Message(connect_id_, ss.str());
  }

  stop_write_chunk_id_ = stop_chunk;
  for (auto it = write_chunks_.begin(); it != write_chunks_.end(); /* noop */) {
    if (it->first >= stop_chunk) {
      it = write_chunks_.erase(it);
    } else {
      ++it;
    }
  }
}

uint64_t OutLink::GetWrittenVolume() { return written_volume_; }

void OutLink::SetOtherSideWrittenVolume(uint64_t volume) {
  auto prev = otherside_written_volume_.exchange(volume);
  if (prev != volume) {
    if (tracer_) {
      std::stringstream ss;
      ss << "Delivered " << otherside_written_volume_.load() << " bytes";
      tracer_->Message(connect_id_, ss.str());
    }
  }
}

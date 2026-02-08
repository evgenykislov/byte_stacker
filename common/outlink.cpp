#include "outlink.h"

#include <iostream>

#include "inttypes.h"

#include "trace.h"
#include "trunklink.h"


#ifdef CONNECT_LOG
const char kLogPrefix[] = "/var/log/stacker/cnt_";
#endif

std::shared_ptr<OutLink> OutLink::CreateOutLink(
    boost::asio::ip::tcp::socket&& socket) {
  return std::shared_ptr<OutLink>(new OutLink(std::move(socket)));
}


std::shared_ptr<OutLink> OutLink::CreateOutLink(
    boost::asio::io_context& ctx, std::string address, uint16_t port) {
  return std::shared_ptr<OutLink>(new OutLink(ctx, address, port));
}


OutLink::OutLink(boost::asio::ip::tcp::socket&& socket)
    : socket_(std::move(socket)),
      resolver_(socket_.get_executor()),
      hoster_(nullptr),
      read_processing_(false),
      write_processing_(false),
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
      break;
    }
  }
}


void OutLink::CancelReadWrite() {
  socket_.cancel();
  std::lock_guard lk(write_chunks_lock_);
  stop_write_immediate_ = true;
  write_idle_timer_.cancel();
  read_idle_timer_.cancel();
}


OutLink::OutLink(
    boost::asio::io_context& ctx, std::string address, uint16_t port)
    : socket_(ctx),
      resolver_(ctx),
      host_(address),
      service_(std::to_string(port)),
      hoster_(nullptr),
      read_processing_(false),
      write_processing_(false),
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
    RequestReadIdle();
    return;
  }


  auto selfptr = shared_from_this();
  //  trlog("-- Request read for outlink socket\n");
  socket_.async_read_some(boost::asio::buffer(read_buffer_),
      [selfptr](
          const boost::system::error_code& err, std::size_t bytes_transferred) {
        selfptr->RequestReadProcessing(err, bytes_transferred);
      });
}


void OutLink::RequestReadProcessing(
    const boost::system::error_code& err, std::size_t bytes_transferred) {
  //        trlog("-- Read some from outlink socket\n");
  // Вне зависимости от ошибок чтения, если есть вычитанные данные - их
  // обрабатываем
  if (bytes_transferred > 0) {
    read_volume_ += bytes_transferred;
    LogWrite("Read %" PRIu64 " bytes", read_volume_.load());
    assert(hoster_);
    hoster_->SendData(selfid_, read_buffer_, bytes_transferred);
  }

  if (err) {
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
    if (err) {
      // Ошибка на ожидание перед чтением.
      // Скорее всего всё закрывается
      selfptr->read_processing_ = false;
      selfptr->CancelReadWrite();
      selfptr->CheckReadyClose();
      return;
    }
    selfptr->RequestRead();
  });
}


void OutLink::RequestConnect() {
  if (resolved_points_.empty()) {
    // TODO Process errors
    return;
  }

  // TRACE
  auto ep = resolved_points_.front();
  trlog(
      "-- Try connect to %s:%u\n", ep.address().to_string().c_str(), ep.port());
  std::cout.flush();

  auto selfptr = shared_from_this();
  socket_.async_connect(resolved_points_.front(),
      [selfptr](const boost::system::error_code& error) {
        selfptr->RequestConnectProcessing(error);
      });
}

void OutLink::RequestConnectProcessing(const boost::system::error_code& error) {
  if (error) {
    std::printf("ERROR: -- Connecting error: %s\n", error.message().c_str());
    std::cout.flush();
    // Неподключились. Текущую точку удаляем, берём следующую
    resolved_points_.pop_front();
    RequestConnect();
  } else {
    // TRACE
    trlog("-- Connected. Start reading/writing\n");
    std::cout.flush();

    read_processing_ = true;
    RequestRead();
    write_processing_ = true;
    RequestWrite();
  }
}


void OutLink::RequestWrite() {
  assert(write_processing_.load());

  std::unique_lock lk(write_chunks_lock_);
  if (stop_write_immediate_) {
    CancelReadWrite();
    write_processing_ = false;
    CheckReadyClose();
    return;
  }
  FillNetworkBuffer();
  if (network_write_buffer_.empty()) {
    if (stop_after_all_write_) {
      CancelReadWrite();
      write_processing_ = false;
      CheckReadyClose();
      return;
    }

    // Пока нечего передавать - включаем ожидание
    //    trlog("-- Nothing write. Use idle timeout\n");
    auto selfptr = shared_from_this();
    std::chrono::milliseconds intrv{kWriteIdleTimeout};
    write_idle_timer_.expires_after(intrv);
    write_idle_timer_.async_wait(
        [selfptr](const boost::system::error_code& err) {
          if (!err) {
            //            trlog("-- Write idles for timeout. Connect %s\n",
            //                uuids::to_string(selfptr->selfid_).c_str());
          }
          selfptr->RequestWrite();
        });
    return;
  }
  lk.unlock();

  //  trlog("-- Writing %u bytes to outlink socket\n",
  //      network_write_buffer_.size());
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
    LogWrite(": CLOSE: Write operation returns error\n");
    write_processing_ = false;
  }
  if (bytes_transferred > network_write_buffer_.size()) {
    assert(false);
    LogWrite(": CLOSE: Writes over-more data\n");
    write_processing_ = false;
  }

  if (!write_processing_) {
    socket_.cancel();
    CheckReadyClose();
    return;
  }

  written_volume_ += bytes_transferred;
  LogWrite("<- Written %" PRIu64 " bytes", written_volume_.load());
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
      // Ранее закрытие ещё не вызывалось
      if (socket_.is_open()) {
        boost::system::error_code error;
        socket_.shutdown(boost::asio::socket_base::shutdown_both, error);
        if (error) {
          trlog("Socket shutdown returns error: %s\n", error.message().c_str());
        }
        socket_.close(error);
        if (error) {
          trlog("Socket close returns error: %s\n", error.message().c_str());
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
    LogWrite(": CLOSE: can't resolve address\n");
    CheckReadyClose();
    return;
  }

  // Получили список адресов для подключения
  for (auto it = results.begin(); it != results.end(); ++it) {
    resolved_points_.push_back(*it);
  }

  // Прим.: пустой список конечных точек - это поведение будет
  // обработано на этапе коннекта

  RequestConnect();
}


OutLink::~OutLink() { assert(!socket_.is_open()); }

void OutLink::Run(TrunkLink* hoster, ConnectID cnt) {
  assert(hoster);
  hoster_ = hoster;
  selfid_ = cnt;

#ifdef CONNECT_LOG
  std::string fn = kLogPrefix;
  fn += uuids::to_string(cnt);
  log_.open(fn, std::ios_base::trunc);  // Не лочится, т.к. потокобезопасная
#endif

  if (socket_.is_open()) {
    read_processing_ = true;
    RequestRead();
    write_processing_ = true;
    RequestWrite();
    LogWrite("Run outlink by opened socket\n");
  } else {
    LogWrite("Run outlink for %s:%s\n", host_.c_str(), service_.c_str());

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
    return;
  }
  if (stop_write_chunk_id_ != kUndefinedChunkID &&
      chunk_id >= stop_write_chunk_id_) {
    // Пришёл пакет после закрытия соединения
    // Как-бы это ошибка логики. В любом случае, этот пакет отбрасывается
    assert(false);
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

  write_idle_timer_.cancel();  // Отменяем таймер на ожидание следующей записи
}

void OutLink::Stop(uint32_t stop_chunk, StopReason reason) {
  switch (reason) {
    case kStopReleaseCommand:
      // Корректное завершение, всё ок
      LogWrite("Closing successfull\n");
      break;
    case kStopNoLive:
      LogWrite("FORCE CLOSE: no-live\n");
      break;
    case kStopChunkAbsent:
      LogWrite(": CLOSE: chunk absent\n");
      break;
    default:
      LogWrite(": CLOSE: unknown reason\n");
  }

  std::unique_lock lk(write_chunks_lock_);
  if (stop_write_chunk_id_ != kUndefinedChunkID &&
      stop_write_chunk_id_ < stop_chunk) {
    // Остановка уже инициирована и указан более ранний чанк
    return;
  }

  // Инициировали остановку
  // Или задали новые параметры остановки: напораньше
  if (stop_chunk <= next_write_chunk_id_) {
    // Фактически уже всё передали. Возможно даже и с опозданием.
    // В любом случае, данных больше не планируется
    // Закрываемся тем, что есть на текущий момент
    //    trlog("Outlink close on current point\n");

    stop_write_chunk_id_ = next_write_chunk_id_;
    stop_after_all_write_ = true;
    write_chunks_.clear();
    write_idle_timer_.cancel();
    read_idle_timer_.cancel();
    return;
  }

  // Так, планируются ещё данные к передаче
  assert(stop_chunk > next_write_chunk_id_);
  LogWrite("  close will be  later: needs send %u packets",
      stop_chunk - next_write_chunk_id_);
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
    LogWrite(
        "--- Delivered %" PRIu64 " bytes", otherside_written_volume_.load());
  }
}

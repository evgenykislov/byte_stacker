#include "trunklink.h"

#include <chrono>
#include <iostream>

#include "trace.h"

namespace bai = boost::asio::ip;

// TODO Descr
void CopyConnectID(uint8_t dest[16], const uuids::uuid& src) {
  auto cnt_bin = src.as_bytes();
  assert(cnt_bin.size_bytes() == 16);
  memcpy(dest, cnt_bin.data(), 16);
}


TrunkLink::TrunkLink(boost::asio::io_context& ctx, bool server_side)
    : server_side_(server_side),
      update_timer_(ctx),
      out_stream_counter_(0),
      in_stream_counter_(0),
      trunk_ping_min_(kUndefinedSizeT),
      trunk_ping_max_{0},
      trunk_ping_summ_{0},
      trunk_ping_count_{0},
      trunk_packet_fault_{0} {
  std::chrono::milliseconds intrv{kLiveUpdateTick};
  next_live_update_ = std::chrono::steady_clock::now() + intrv;

  RequestUpdate();
}


uint32_t TrunkLink::GetNextPacketIndex(ConnectID cnt) {
  std::lock_guard lk(out_links_lock_);
  for (auto& item : out_links_) {
    if (item.connect_id == cnt) {
      auto res = item.next_index_to_trunk;
      ++item.next_index_to_trunk;
      return res;
    }
  }
  return kBadPacketIndex;
}


void TrunkLink::RequestUpdate() {
  std::chrono::milliseconds intrv{kUpdateTick};
  update_timer_.expires_after(intrv);
  update_timer_.async_wait([this](const boost::system::error_code& err) {
    if (err) {
      // Отменили все операции, закрываем приложение
      return;
    }

    // Продвинем очередь
    std::unique_lock lk(packet_data_cache_lock_);
    PushDataQueueWOLock();
    lk.unlock();

    SendLivePacket();
    OnCacheResend();

    RequestUpdate();
  });
}


void TrunkLink::SendLivePacket() {
  auto curt = std::chrono::steady_clock::now();
  if (curt < next_live_update_) {
    return;
  }
  std::chrono::milliseconds intrv{kLiveUpdateTick};
  next_live_update_ = curt + intrv;

  // Рассылаем live-пакеты
  // trlog("LIVE-LIVE-LIVE\n");
  std::lock_guard lk(out_links_lock_);
  for (auto& item : out_links_) {
    // Сначала удалим мёртвые соединения
    if (curt > item.deadlink_timeout_) {
      trlog("-- Dead connect %s - removing\n",
          uuids::to_string(item.connect_id).c_str());
      item.link->Stop(0, OutLink::kStopNoLive);
      continue;
    }

    assert(sizeof(PacketLive) <= kPacketBufferSize);
    auto buf = GetBuffer();
    auto pkt = (PacketLive*)(buf.get());
    CopyConnectID(pkt->ConnectID, item.connect_id);
    pkt->PacketCommand = kTrunkCommandLive;
    pkt->WrittenOutSize = item.link->GetWrittenVolume();
    PacketInfo pi;
    pi.CtxID = item.connect_id;
    pi.PacketID = kEmptyPacketID;
    pi.PacketData = buf;
    pi.PacketSize = sizeof(PacketLive);
    SendPacket(pi);  // Live-пакет шлём без кэширования - это же live
  }
}

void TrunkLink::PushDataQueueWOLock() {
  if (data_sent_.size() >= kDefaultSentQueueSize) {
    // Очередь отправленных пакетов полная. Ждём
    return;
  }
  if (data_queue_.empty()) {
    // Очередь пакетов на отправку пустая. Нечего делать
    return;
  }

  auto avail = kDefaultSentQueueSize - data_sent_.size();
  assert(avail > 0);
  trlog("Push %u into sent queue\n", (unsigned int)avail);
  for (size_t i = 0; i < avail; ++i) {
    if (data_queue_.empty()) {
      return;
    }

    auto item = data_queue_.front();
    auto curt = std::chrono::steady_clock::now();
    if (curt > item.Deadline) {
      // Пакет ещё не передаавался, но уже устарел
      data_queue_.pop_front();
      // TODO TODO TODO Обработать
      //      trlog("!!!!!!!!!!!!!!!!!!!!!!!!!! Dead packet before sending\n");
      trlog("-- Dead packet: %s:%u\n",
          uuids::to_string(item.info.CtxID).c_str(),
          (unsigned int)item.info.PacketID);

      continue;
    }

    item.FirstSend = curt;
    item.NextSend = curt + std::chrono::milliseconds(kResendTimeout);
    data_sent_.push_back(item);
    data_queue_.pop_front();
    trlog("Packet sent: %s:%u\n", uuids::to_string(item.info.CtxID).c_str(),
        (unsigned int)item.info.PacketID);

    SendPacket(item.info);
  }
}


void TrunkLink::SendData(ConnectID cnt, const void* data, size_t data_size) {
  in_stream_counter_ += data_size;
  SendCmdData(cnt, data, data_size,
      server_side_ ? kTrunkCommandDataIn : kTrunkCommandDataOut);
}


void TrunkLink::SendCmdData(
    ConnectID cnt, const void* data, size_t data_size, TrunkCommand cmd) {
  // trlog("-- Send %u bytes of data into trunk. Connect %s\n",
  //     (unsigned int)data_size, uuids::to_string(cnt).c_str());

  if (data_size > kMaxChunkSize) {
    assert(false);
    std::printf("ERROR: over max chunk size\n");
    return;
  }

  auto pkt_index = GetNextPacketIndex(cnt);
  if (pkt_index == kBadPacketIndex) {
    // TODO ERROR
    std::printf("ERROR: bad packet\n");
    return;
  }


  // Сформируем сам пакет
  auto buf = GetBuffer();
  auto pkt = (PacketData*)(buf.get());
  CopyConnectID(pkt->ConnectID, cnt);
  pkt->PacketCommand = cmd;
  pkt->PacketIndex = pkt_index;
  pkt->DataSize = data_size;
  memcpy(buf.get() + sizeof(PacketData), data, data_size);

  // Сформируем информационный блок для кэширования и т.д.
  PacketInfo info;
  info.CtxID = cnt;
  info.PacketID = pkt_index;
  info.PacketData = buf;
  info.PacketSize = sizeof(PacketData) + data_size;

  PacketDataCache pc;
  pc.info = info;
  auto curt = std::chrono::steady_clock::now();
  pc.FirstSend = curt;
  pc.Deadline = curt + std::chrono::milliseconds(kDeadlineTimeout);
  pc.NextSend = curt + std::chrono::milliseconds(kResendTimeout);

  std::unique_lock<std::mutex> lk(packet_data_cache_lock_);
  data_queue_.push_back(pc);
  trlog("Add packet to queue: %s:%u\n", uuids::to_string(cnt).c_str(),
      (unsigned int)pkt_index);
  PushDataQueueWOLock();
  lk.unlock();
}

void TrunkLink::CloseConnect(ConnectID cnt) {
  SendDisconnectInformation(cnt);
  RemoveOutLink(cnt);
}


void TrunkLink::SendDisconnectInformation(ConnectID cnt) {
  uint8_t fake_buf;
  SendCmdData(cnt, &fake_buf, 0, kTrunkCommandReleaseConnect);

  // trlog("-- Send disconnect information. Id: %s\n",
  //     uuids::to_string(cnt).c_str());
}


StatInfo TrunkLink::GetStat() {
  StatInfo res;

  res.StreamToOutLinks = out_stream_counter_.exchange(0);
  res.StreamFromOutLinks = in_stream_counter_.exchange(0);

  std::unique_lock lk(out_links_lock_);
  res.ConnectAmount = out_links_.size();
  lk.unlock();

  std::unique_lock lks(stat_lock_);
  res.FauldPacket = trunk_packet_fault_;
  if (trunk_ping_count_ == 0) {
    res.MinPing = 0;
    res.MaxPing = 0;
    res.AveragePing = 0;
  } else {
    res.MinPing = trunk_ping_min_;
    res.MaxPing = trunk_ping_max_;
    res.AveragePing = trunk_ping_summ_ / trunk_ping_count_;
  }
  // Сбросим показатели
  trunk_ping_min_ = kUndefinedSizeT;
  trunk_ping_max_ = 0;
  trunk_ping_summ_ = 0;
  trunk_ping_count_ = 0;
  trunk_packet_fault_ = 0;
  lks.unlock();

  return res;
}

void TrunkLink::ProcessTrunkData(
    boost::asio::ip::udp::endpoint client, const void* data, size_t data_size) {
  if (data_size < sizeof(PacketHeader)) {
    // Битый пакет непонятно откуда и от кого
    return;
  }

  auto hdr = static_cast<const PacketHeader*>(data);
  uuids::uuid cnt(hdr->ConnectID, hdr->ConnectID + sizeof(hdr->ConnectID));

  if (server_side_) {
    switch (hdr->PacketCommand) {
      case kTrunkCommandAckCreateConnect:
      case kTrunkCommandDataIn:
      case kTrunkCommandAckDataOut:
        // Это всё ошибочниые команды
        // TODO ERROR
        return;
        break;
    }
  } else {
    // Клиентская сторона
    switch (hdr->PacketCommand) {
      case kTrunkCommandCreateConnect:
      case kTrunkCommandDataOut:
      case kTrunkCommandAckDataIn:
        // Это всё ошибочниые команды
        // TODO ERROR
        return;
        break;
    }
  }

  switch (hdr->PacketCommand) {
    case kTrunkCommandCreateConnect:
      if (data_size < sizeof(PacketConnect)) {
        // Неполный формат
        return;
      }
      ProcessConnectData(cnt, static_cast<const PacketConnect*>(hdr));
      break;
    case kTrunkCommandAckCreateConnect:
      ProcessAckConnectData(cnt, hdr);
      break;
    case kTrunkCommandDataOut:
    case kTrunkCommandDataIn:
      if (data_size < sizeof(PacketData)) {
        // Неполный формат
        return;
      }

      {
        auto pd = static_cast<const PacketData*>(hdr);
        if (data_size != (sizeof(PacketData) + pd->DataSize)) {
          // Ошибка формата
          return;
        }
        ProcessDataToOutlink(cnt, pd, pd + 1);
      }
      break;
    case kTrunkCommandAckDataOut:
    case kTrunkCommandAckDataIn:
      if (data_size != sizeof(PacketAck)) {
        // Неправильный формат
        return;
      }

      {
        auto pa = static_cast<const PacketAck*>(hdr);
        ProcessAckData(cnt, pa->PacketIndex);
      }
      break;
    case kTrunkCommandReleaseConnect:
      if (data_size != sizeof(PacketData)) {
        // Неправильный формат
        return;
      }

      {
        auto pd = static_cast<const PacketData*>(hdr);
        if (pd->DataSize != 0) {
          // Ошибка формата. Данные должны быть пустые
          return;
        }

        ProcessReleaseConnect(cnt, pd->PacketIndex);
      }
      break;
    case kTrunkCommandLive:
      if (data_size != sizeof(PacketLive)) {
        // Неправильный формат
        return;
      } else {
        auto pl = static_cast<const PacketLive*>(hdr);
        ProcessLive(cnt, pl->WrittenOutSize);
      }
      break;
  }
}

void TrunkLink::ProcessDataToOutlink(
    uuids::uuid cnt, const PacketData* info, const void* data) {
  //    trlog("-- Got %u bytes from trunk for connect %s\n",
  //        (unsigned int)info->DataSize, uuids::to_string(cnt).c_str());

  // Отправим подтверждение на получение пакета
  // Подтверждение шлём, даже если соединение уже "умерло"
  assert(sizeof(PacketAck) <= kPacketBufferSize);
  auto buf = GetBuffer();
  auto pkt = (PacketAck*)(buf.get());
  CopyConnectID(pkt->ConnectID, cnt);
  pkt->PacketCommand =
      server_side_ ? kTrunkCommandAckDataOut : kTrunkCommandAckDataIn;
  pkt->PacketIndex = info->PacketIndex;
  PacketInfo pi;
  pi.CtxID = cnt;
  pi.PacketID = kEmptyPacketID;
  pi.PacketData = buf;
  pi.PacketSize = sizeof(PacketAck);
  SendPacket(pi);  // Подтверждение шлём без кэширования. Пока отработает кэш -
                   // придёт новая копия.

  // Выдадим данные на внешний линк
  auto link = GetOutLink(cnt);
  if (link) {
    out_stream_counter_ += info->DataSize;
    link->SendData(info->PacketIndex, data, info->DataSize);
  }
  // else - Нет такого подключения. Уже удалено, закрыто или др.
  // В общем, ничего не делаем
}

void TrunkLink::ProcessAckData(uuids::uuid cnt, uint32_t packet_index) {
  trlog("Delete packet - Ack: %s:%u\n", uuids::to_string(cnt).c_str(),
      (unsigned int)packet_index);

  // TODO IMPLEMENT
  size_t ping = kUndefinedSizeT;

  std::unique_lock lk(packet_data_cache_lock_);
  auto tails = std::remove_if(data_sent_.begin(), data_sent_.end(),
      [cnt, packet_index](PacketDataCache& item) {
        return (item.info.CtxID == cnt) && (item.info.PacketID == packet_index);
      });
  if (tails != data_sent_.end()) {
    // Сосчитаем пинг для статистики
    ping = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - tails->FirstSend)
               .count();
  }
  data_sent_.erase(tails, data_sent_.end());

  // Пакет ещё не отправлен, но уже может прийти подтверждение
  // Такое бывает, если из-за таймаута пакет поставили заново в очередь
  // на  отправку
  auto tailq = std::remove_if(data_queue_.begin(), data_queue_.end(),
      [cnt, packet_index](PacketDataCache& item) {
        return (item.info.CtxID == cnt) && (item.info.PacketID == packet_index);
      });
  data_queue_.erase(tailq, data_queue_.end());

  PushDataQueueWOLock();

  lk.unlock();

  if (ping != kUndefinedSizeT) {
    std::lock_guard lk(stat_lock_);
    if (ping < trunk_ping_min_) {
      trunk_ping_min_ = ping;
    }
    if (ping > trunk_ping_max_) {
      trunk_ping_max_ = ping;
    }
    trunk_ping_summ_ += ping;
    ++trunk_ping_count_;
  }
}


void TrunkLink::ProcessReleaseConnect(uuids::uuid cnt, uint32_t packet_id) {
  auto link = GetOutLink(cnt);
  if (!link) {
    // Нет такого подключения
    // TODO Error process
    return;
  }

  //  trlog("-- Close connect %s with packet %u\n",
  //  uuids::to_string(cnt).c_str(),
  //      packet_id);

  link->Stop(packet_id, OutLink::kStopReleaseCommand);
}

void TrunkLink::ProcessLive(uuids::uuid cnt, uint64_t written) {
  // trlog(">>> Live %s\n", uuids::to_string(cnt).c_str());
  std::lock_guard lk(out_links_lock_);
  for (auto& item : out_links_) {
    if (item.connect_id == cnt) {
      std::chrono::milliseconds intrv{kDeadLinkTimeout};
      item.deadlink_timeout_ = std::chrono::steady_clock::now() + intrv;
    }
  }
}


void TrunkLink::IntAddOutLinkWOLock(
    uuids::uuid cnt, std::shared_ptr<OutLink> link) {
  OutLinkInfo info;
  info.connect_id = cnt;
  info.link = link;
  info.next_index_to_trunk = 0;
  std::chrono::milliseconds intrv{kDeadLinkTimeout};
  info.deadlink_timeout_ = std::chrono::steady_clock::now() + intrv;
  out_links_.push_back(info);

  link->Run(this, cnt);
}

std::shared_ptr<OutLink> TrunkLink::GetOutLinkWOLock(uuids::uuid cnt) {
  for (auto& item : out_links_) {
    if (item.connect_id == cnt) {
      return item.link;
    }
  }
  return std::shared_ptr<OutLink>();
}

std::shared_ptr<OutLink> TrunkLink::GetOutLink(uuids::uuid cnt) {
  std::unique_lock lk(out_links_lock_);
  return GetOutLinkWOLock(cnt);
}

std::shared_ptr<TrunkLink::PacketBuffer> TrunkLink::GetBuffer() {
  return std::make_shared<TrunkClient::PacketBuffer>();
}

void TrunkLink::OnCacheResend() {
  size_t deadp = 0;
  size_t resending = 0;

  std::unique_lock lk(packet_data_cache_lock_);
  auto curt = std::chrono::steady_clock::now();

  auto tail = std::remove_if(data_sent_.begin(), data_sent_.end(),
      [curt](PacketDataCache& item) { return curt > item.Deadline; });
  if (tail != data_sent_.end()) {
    deadp = data_sent_.end() - tail;
    trlog("-- Removing %u deadline packets\n", (unsigned int)deadp);
    data_sent_.erase(tail, data_sent_.end());
  }

  // Перепосылка пакетов
  for (auto it = data_sent_.begin(); it != data_sent_.end(); /* noop */) {
    if (curt > it->NextSend) {
      // Перепосылаем пакет. Точнее заталкиваем его в очередь в начало
      data_queue_.push_front(*it);
      ++resending;
      it = data_sent_.erase(it);
      trlog("Packet resend: %s:%u\n", uuids::to_string(it->info.CtxID).c_str(),
          (unsigned int)it->info.PacketID);
      continue;
    }

    ++it;
  }
  lk.unlock();

  if (deadp > 0) {
    std::lock_guard lk(stat_lock_);
    trunk_packet_fault_ += deadp;
  }

  if (resending > 0) {
    // trlog("-- ReSend %u packets\n", resending);
  }
}


void TrunkLink::RemoveOutLink(uuids::uuid cnt) {
  trlog("-- Remove outlink %s\n", uuids::to_string(cnt).c_str());
  std::lock_guard lk(out_links_lock_);
  auto tail = std::remove_if(out_links_.begin(), out_links_.end(),
      [cnt](OutLinkInfo info) { return cnt == info.connect_id; });
  out_links_.erase(tail, out_links_.end());
}


TrunkClient::TrunkClient(boost::asio::io_context& ctx,
    const std::vector<boost::asio::ip::udp::endpoint>& trpoints)
    : TrunkLink(ctx, false),
      points_(trpoints),
      trunk_socket_(ctx, boost::asio::ip::udp::v4()) {
  // Инициализация генератора uuid
  std::random_device rd;
  auto seed_data = std::array<int, std::mt19937::state_size>{};
  std::generate(std::begin(seed_data), std::end(seed_data), std::ref(rd));
  std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
  generator_ = std::mt19937(seq);

  ReceiveTrunkData();
}

void TrunkClient::SendConnectInformation(
    ConnectID cnt, PointID point, unsigned int timeout) {
  assert(sizeof(PacketConnect) <= kPacketBufferSize);
  auto buf = GetBuffer();
  auto pkt = (PacketConnect*)(buf.get());
  CopyConnectID(pkt->ConnectID, cnt);
  pkt->PacketCommand = kTrunkCommandCreateConnect;
  pkt->PointID = point;
  pkt->Timeout = timeout;

  PacketInfo info;
  info.CtxID = cnt;
  info.PacketID = kEmptyPacketID;
  info.PacketData = buf;
  info.PacketSize = sizeof(PacketConnect);

  PacketConnectCache pc;
  pc.info = info;
  auto curt = std::chrono::steady_clock::now();
  pc.Deadline = curt + std::chrono::milliseconds(kDeadlineTimeout);
  pc.NextSend = curt + std::chrono::milliseconds(kResendTimeout);

  std::unique_lock<std::mutex> lk(connect_cache_lock_);
  connect_cache_.push_back(pc);
  lk.unlock();

  SendPacket(info);

  // trlog("Send connect information. Id: %s, Point %u\n",
  //     uuids::to_string(cnt).c_str(), point);
}

void TrunkClient::OnCacheResend() {
  TrunkLink::OnCacheResend();

  std::lock_guard<std::mutex> lk(connect_cache_lock_);
  auto curt = std::chrono::steady_clock::now();

  auto tail = std::remove_if(connect_cache_.begin(), connect_cache_.end(),
      [curt](PacketConnectCache& item) { return curt > item.Deadline; });
  if (tail != connect_cache_.end()) {
    trlog("-- Removing %u deadline connects\n", connect_cache_.end() - tail);
    connect_cache_.erase(tail, connect_cache_.end());
  }


  for (auto& item : connect_cache_) {
    // TODO process deadline ????

    if (item.NextSend > curt) {
      continue;
    }
    item.NextSend = curt + std::chrono::milliseconds(kResendTimeout);
    SendPacket(item.info);

    // TRACE
    //    trlog("-- ReSend connect information for id %s\n",
    //        uuids::to_string(item.info.CtxID).c_str());
  }
}

TrunkClient::~TrunkClient() {}

void TrunkClient::AddConnect(PointID point, std::shared_ptr<OutLink> link) {
  assert(link);
  uuids::uuid_random_generator gen{generator_};
  uuids::uuid cnt = gen();

  std::unique_lock lk(out_links_lock_);
  auto exist_link = GetOutLinkWOLock(cnt);
  if (exist_link) {
    // Такая внешняя связь уже существует
    // Странно, но теоретически возможно
    // Ничего не делаем, выходим, забываем про эту связь
    return;
  }
  IntAddOutLinkWOLock(cnt, link);
  lk.unlock();

  SendConnectInformation(cnt, point, kResendTimeout);
}


void TrunkClient::ReceiveTrunkData() {
  trunk_socket_.async_receive_from(
      boost::asio::buffer(trunk_read_buffer_, kPacketBufferSize),
      trunk_read_point_,
      [this](boost::system::error_code err, std::size_t data_size) {
        if (err) {
          // TODO Error processing
        } else {
          ProcessTrunkData(trunk_read_point_, trunk_read_buffer_, data_size);
        }

        ReceiveTrunkData();
      });
}

void TrunkClient::SendPacket(PacketInfo pkt) {
  auto pd = pkt.PacketData;
  trunk_socket_.async_send_to(boost::asio::buffer(pd.get(), pkt.PacketSize),
      points_.front(),
      [pd](boost::system::error_code /*ec*/, std::size_t /*bytes_sent*/) {});
}


void TrunkClient::ProcessAckConnectData(
    uuids::uuid cnt, const PacketHeader* info) {
  // trlog("-- Receive ack for connection id %s\n",
  // uuids::to_string(cnt).c_str());

  std::lock_guard<std::mutex> lk(connect_cache_lock_);
  for (auto it = connect_cache_.begin(); it != connect_cache_.end();) {
    if (it->info.CtxID != cnt) {
      ++it;
    } else {
      it = connect_cache_.erase(it);
    }
  }
}


TrunkServer::TrunkServer(boost::asio::io_context& ctx,
    const std::vector<boost::asio::ip::udp::endpoint>& trpoints,
    std::function<std::shared_ptr<OutLink>(PointID)> link_fabric)
    : TrunkLink(ctx, true), asio_context_(ctx), link_fabric_(link_fabric) {
  for (auto& p : trpoints) {
    trunk_sockets_.emplace_back(ServerSocket{{ctx, p}, GetBuffer()});
  }

  for (size_t index = 0; index < trunk_sockets_.size(); ++index) {
    RequestReadingTrunk(index);
  }
}

TrunkServer::~TrunkServer() {}


std::shared_ptr<TrunkServer::PacketBuffer> TrunkServer::GetBuffer() {
  return std::make_shared<TrunkServer::PacketBuffer>();
}

void TrunkServer::RequestReadingTrunk(size_t index) {
  auto& ts = trunk_sockets_[index];
  ts.socket.async_receive_from(
      boost::asio::buffer(ts.buffer.get(), kPacketBufferSize), ts.client_holder,
      [this, index](boost::system::error_code err, std::size_t data_size) {
        if (err) {
          // TODO Error processing
        } else {
          // Получили из канала блок данных
          auto& ts = trunk_sockets_[index];
          uuids::uuid cnt;
          if (!GetPacketConnectID(ts.buffer.get(), data_size, cnt)) {
            // Битый пакет; Формат неправильный
            // Вероятная причина: левый сервис послал тестовый пакет на пробу
            // Пропускаем этот пакет
          } else {
            // Пакет правильного формата. Берём в работу (и запоминаем откуда он
            // пришёл)
            AddClientLink({cnt, index, ts.client_holder});
            ProcessTrunkData(ts.client_holder, ts.buffer.get(), data_size);
          }
        }

        RequestReadingTrunk(index);
      });
}


void TrunkServer::ProcessConnectData(
    uuids::uuid cnt, const PacketConnect* info) {
  //  trlog("-- Request connect to point %u. Id: %s\n",
  //  info->PointID,
  //      uuids::to_string(cnt).c_str());

  // Отправим подтверждение на получение пакета
  assert(sizeof(PacketHeader) <= kPacketBufferSize);
  auto buf = GetBuffer();
  auto pkt = (PacketHeader*)(buf.get());
  CopyConnectID(pkt->ConnectID, cnt);
  pkt->PacketCommand = kTrunkCommandAckCreateConnect;
  PacketInfo pi;
  pi.CtxID = cnt;
  pi.PacketID = kEmptyPacketID;
  pi.PacketData = buf;
  pi.PacketSize = sizeof(PacketHeader);
  SendPacket(pi);

  std::unique_lock lk(out_links_lock_);
  auto exist_link = GetOutLinkWOLock(cnt);
  if (exist_link) {
    // Такая внешняя связь уже существует
    // Такое легко может быть, когда пришёл дубликат сообщения о новом коннекте
    // Ничего не создаём, выходим
    return;
  }
  // Создадим внешний коннект
  auto ol = link_fabric_(info->PointID);
  if (!ol) {
    // TODO ERROR Can't create link
    return;
  }
  IntAddOutLinkWOLock(cnt, ol);
  lk.unlock();
}


void TrunkServer::SendPacket(PacketInfo pkt) {
  // Найдём, куда отправлять
  ConnectInfo info;
  info.connect = pkt.CtxID;

  if (!GetClientLink(info)) {
    // Нет информации о коннекте
    // Неизвестно, куда отправлять данные
    return;
  }

  auto& ts = trunk_sockets_[info.socket_index];
  auto buf = pkt.PacketData;
  ts.socket.async_send_to(boost::asio::buffer(buf.get(), pkt.PacketSize),
      info.client,
      [buf](boost::system::error_code /*ec*/, std::size_t /*bytes_sent*/) {});
}

bool TrunkServer::GetPacketConnectID(
    const void* data, size_t data_size, uuids::uuid& cnt) {
  if (data_size < sizeof(PacketHeader)) {
    // Битый пакет непонятно откуда и от кого
    return false;
  }

  auto hdr = static_cast<const PacketHeader*>(data);
  cnt = uuids::uuid(hdr->ConnectID, hdr->ConnectID + sizeof(hdr->ConnectID));
  return true;
}

void TrunkServer::AddClientLink(ConnectInfo info) {
  std::lock_guard lk(clients_link_lock_);
  for (auto& item : clients_link_) {
    if (item.connect == info.connect) {
      item = info;
      return;
    }
  }
  clients_link_.push_back(info);
}

bool TrunkServer::GetClientLink(ConnectInfo& info) {
  std::lock_guard lk(clients_link_lock_);
  for (auto& item : clients_link_) {
    if (item.connect == info.connect) {
      info = item;
      return true;
    }
  }
  return false;
}

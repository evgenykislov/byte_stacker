#include "trunklink.h"

#include <chrono>
#include <fstream>
#include <iostream>

#include "settings.h"
#include "trace.h"

namespace bai = boost::asio::ip;

// TODO remove after settings log
#ifdef CONNECT_LOG
const char kTrunkErrorLog[] = "/var/log/stacker/trunk_error.txt";
#endif


// TODO Descr
void CopyConnectID(uint8_t dest[16], const uuids::uuid& src) {
  auto cnt_bin = src.as_bytes();
  assert(cnt_bin.size_bytes() == 16);
  memcpy(dest, cnt_bin.data(), 16);
}


TrunkLink::TrunkLink(
    boost::asio::io_context& ctx, bool server_side, const Settings& cfg)
    : cfg_settings_(cfg),
      server_side_(server_side),
      update_timer_(ctx),
      send_queue_timer_(ctx),
      out_stream_counter_(0),
      in_stream_counter_(0),
      trunk_ping_min_(kUndefinedSizeT),
      trunk_ping_max_{0},
      trunk_ping_summ_{0},
      trunk_ping_count_{0},
      trunk_packet_fault_{0} {
  std::chrono::milliseconds intrv{kLiveUpdateTick};
  next_live_update_ = std::chrono::steady_clock::now() + intrv;

  trunk_live_ok.test_and_set();

#ifdef CONNECT_LOG
  error_log_.open(kTrunkErrorLog, std::ios_base::trunc);
#endif

  RequestUpdate();
  RequestSendQueue();
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

    SendLivePacket();

    // TODO Remove stopped,completed outlinks

    OnCacheResend();

    RequestUpdate();
  });
}

void TrunkLink::RequestSendQueue() {
  send_queue_timer_.expires_after(std::chrono::milliseconds(kSendQueueTick));
  send_queue_timer_.async_wait([this](const boost::system::error_code& err) {
    if (err) {
      // Отменили все операции, закрываем приложение
      return;
    }

    SendPacketQueue();

    RequestSendQueue();
  });
}


void TrunkLink::SendLivePacket() {
  auto curt = std::chrono::steady_clock::now();
  if (curt < next_live_update_) {
    return;
  }
  std::chrono::milliseconds intrv{kLiveUpdateTick};
  next_live_update_ = curt + intrv;

  // Рассылаем live-пакеты. И отслеживаем мёртвые соединения
  // trlog("LIVE-LIVE-LIVE\n");
  std::vector<uuids::uuid> dead_cnt;  //!< Мёртвые соединения - на удаление
  std::unique_lock lk(out_links_lock_);
  for (auto& item : out_links_) {
    // Сначала удалим мёртвые соединения
    std::chrono::milliseconds forceto{kForceRemoveLinkTimeout};
    if (curt > (item.deadlink_timeout_ + forceto)) {
      trlog("-- FORCE REMOVE connection %s\n",
          uuids::to_string(item.connect_id).c_str());
      error_log_ << timemark(true) << ": force remove "
                 << uuids::to_string(item.connect_id) << " outlink"
                 << std::endl;
      dead_cnt.push_back(item.connect_id);
      continue;
    }

    assert(item.link.get());
    if (curt > item.deadlink_timeout_) {
      //      trlog("-- Dead connect %s - removing\n",
      //          uuids::to_string(item.connect_id).c_str());
      trunk_live_ok.clear();  // Есть проблемы с live-пакетами
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
    SendPacket(pi);  // Live-пакеты шлются всегда, даже при заполненном буфере
  }
  lk.unlock();

  // Удалим мёртвые соединения
  for (auto& id : dead_cnt) {
    RemoveOutLink(id);
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
  pkt->DataSize = static_cast<uint32_t>(data_size);
  if (data_size > 0) {
    memcpy(buf.get() + sizeof(PacketData), data, data_size);
  }

  // Сформируем пакет в посылку
  PacketInfo info;
  info.CtxID = cnt;
  info.PacketID = pkt_index;
  info.PacketData = buf;
  info.PacketSize = static_cast<uint32_t>(sizeof(PacketData) + data_size);

  // Отправим пакет в очередь (дожидаться свободного буфера)
  std::unique_lock lks(packet_send_queue_lock_);
  packet_send_queue_.push_back(info);
  lks.unlock();
  SendPacketQueue();
}

void TrunkLink::SendPacketQueue() {
  std::unique_lock lks(packet_send_queue_lock_);
  for (auto it = packet_send_queue_.begin(); it != packet_send_queue_.end();
      /* ничего не делаем */) {
    auto info = *it;

    auto ava = GetAvailableBuffer(info.CtxID);
    if (ava < info.PacketSize) {
      ++it;
      continue;
    }

    PacketDataCache pc;
    pc.info = info;
    auto curt = std::chrono::steady_clock::now();
    pc.FirstSend = curt;
    pc.Deadline = curt + std::chrono::milliseconds(kDeadPacketTimeout);
    pc.NextSend = curt + std::chrono::milliseconds(kResendTimeout);

    std::unique_lock<std::mutex> lk(packet_data_cache_lock_);
    packet_data_cache_.push_back(pc);
    lk.unlock();

    SendPacket(info);  // TODO сделать проверку на размер буфера

    it = packet_send_queue_.erase(it);
  }
}


void TrunkLink::CloseConnect(ConnectID cnt) {
  SendDisconnectInformation(cnt);
  RemoveOutLink(cnt);
}


void TrunkLink::SendDisconnectInformation(ConnectID cnt) {
  SendCmdData(cnt, nullptr, 0, kTrunkCommandReleaseConnect);

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
  res.no_live = !trunk_live_ok.test_and_set();
  // Сбросим показатели
  trunk_ping_min_ = kUndefinedSizeT;
  trunk_ping_max_ = 0;
  trunk_ping_summ_ = 0;
  trunk_ping_count_ = 0;
  trunk_packet_fault_ = 0;
  // trunk_live_ok сбрасывается автоматически
  lks.unlock();

  std::unique_lock lkc(packet_data_cache_lock_);
  res.cache_load = packet_data_cache_.size();
  lkc.unlock();

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

  if (cfg_settings_.LogTrunkPacket) {
    std::stringstream s;
    s << "Trunk Connect " << uuids::to_string(cnt) << ": receive packet "
      << data_size << " bytes";
    cfg_settings_.OutputLog(s.str());
  }

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
        if (cfg_settings_.LogFormatError) {
          std::stringstream s;
          s << "Trunk Connect " << uuids::to_string(cnt)
            << ": receive packet 'release connect' with wrong size";
          cfg_settings_.OutputLog(s.str());
        }
        return;
      }

      {
        auto pd = static_cast<const PacketData*>(hdr);
        if (pd->DataSize != 0) {
          // Ошибка формата. Данные должны быть пустые
          if (cfg_settings_.LogFormatError) {
            std::stringstream s;
            s << "Trunk Connect " << uuids::to_string(cnt)
              << ": receive packet 'release connect' with non-empty data "
                 "(error)";
            cfg_settings_.OutputLog(s.str());
          }
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
  SendPacket(pi);  // Ack пакет шлётся всегда, вне зависимости от загрузки
  if (cfg_settings_.LogTrunkPacket) {
    std::stringstream s;
    s << "Trunk Connect " << uuids::to_string(cnt) << ": ack packet packet "
      << info->PacketIndex;
    cfg_settings_.OutputLog(s.str());
  }

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
  // TODO IMPLEMENT

  if (cfg_settings_.LogTrunkPacket) {
    std::stringstream s;
    s << "Trunk Connect " << uuids::to_string(cnt) << ": receive ack packet "
      << packet_index;
    cfg_settings_.OutputLog(s.str());
  }

  size_t ping = kUndefinedSizeT;

  std::unique_lock lk(packet_data_cache_lock_);
  auto tail = std::remove_if(packet_data_cache_.begin(),
      packet_data_cache_.end(), [cnt, packet_index](PacketDataCache& item) {
        return (item.info.CtxID == cnt) && (item.info.PacketID == packet_index);
      });
  if (tail != packet_data_cache_.end()) {
    // Такой пакет в кэше есть
    ping = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - tail->FirstSend)
               .count();
    packet_data_cache_.erase(tail, packet_data_cache_.end());
  }
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
      item.deadlink_timeout_ = std::chrono::steady_clock::now() +
                               std::chrono::milliseconds(kDeadOutLinkTimeout);
      item.link->SetOtherSideWrittenVolume(written);
    }
  }
}


void TrunkLink::IntAddOutLinkWOLock(
    uuids::uuid cnt, std::shared_ptr<OutLink> link) {
  OutLinkInfo info;
  info.connect_id = cnt;
  info.link = link;
  info.next_index_to_trunk = 0;
  info.deadlink_timeout_ = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(kDeadOutLinkTimeout);
  out_links_.push_back(info);
  //  trlog("-- Add outlink %s\n", uuids::to_string(cnt).c_str());
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

  auto tail =
      std::remove_if(packet_data_cache_.begin(), packet_data_cache_.end(),
          [curt](PacketDataCache& item) { return curt > item.Deadline; });
  if (tail != packet_data_cache_.end()) {
    deadp = packet_data_cache_.end() - tail;
    //    trlog("-- Removing %u deadline packets\n", (unsigned int)deadp);
    packet_data_cache_.erase(tail, packet_data_cache_.end());
  }

  for (auto it = packet_data_cache_.begin(); it != packet_data_cache_.end();
       ++it) {
    if (curt > it->NextSend) {
      // Перепосылаем пакет
      if (cfg_settings_.LogResendPacket) {
        std::stringstream s;
        s << "Trunk Connect " << uuids::to_string(it->info.CtxID)
          << ": re-send packet " << it->info.PacketID << ", "
          << it->info.PacketSize << " bytes";
        cfg_settings_.OutputLog(s.str());
      }

      it->NextSend = curt + std::chrono::milliseconds(kResendTimeout);
      SendPacket(it->info);  // Перепосылка из кэша шлётся всегда

      ++resending;
    }
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
  //  trlog("-- Remove outlink %s\n", uuids::to_string(cnt).c_str());
  std::lock_guard lk(out_links_lock_);
  auto tail = std::remove_if(out_links_.begin(), out_links_.end(),
      [cnt](OutLinkInfo info) { return cnt == info.connect_id; });
  out_links_.erase(tail, out_links_.end());
}


TrunkClient::TrunkClient(boost::asio::io_context& ctx,
    const std::vector<boost::asio::ip::udp::endpoint>& trpoints,
    const Settings& cfg)
    : TrunkLink(ctx, false, cfg),
      points_(trpoints),
      trunk_socket_(ctx, boost::asio::ip::udp::v4()) {
  // Инициализация генератора uuid
  std::random_device rd;
  auto seed_data = std::array<int, std::mt19937::state_size>{};
  std::generate(std::begin(seed_data), std::end(seed_data), std::ref(rd));
  std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
  generator_ = std::mt19937(seq);

  // Получим информацию о сокете
  boost::asio::socket_base::receive_buffer_size option;
  trunk_socket_.get_option(option);
  int buf_size = option.value();
  if (buf_size < kMinimalUdpBufferSize) {
    buf_size = kMinimalUdpBufferSize;
  }
  trunk_socket_buffer_size_ = (int)(buf_size * kUdpBufferDataPart);
  std::unique_lock lk(trunk_buffer_lock_);
  trunk_buffer_last_size_ = trunk_socket_buffer_size_;
  trunk_buffer_last_time_ = std::chrono::steady_clock::now();
  lk.unlock();

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
  pc.Deadline = curt + std::chrono::milliseconds(kDeadOutLinkTimeout);
  pc.NextSend = curt + std::chrono::milliseconds(kResendConnectTimeout);

  std::unique_lock<std::mutex> lk(connect_cache_lock_);
  connect_cache_.push_back(pc);
  lk.unlock();

  SendPacket(info);  // Информация о соединении шлётся всегда

  // trlog("Send connect information. Id: %s, Point %u\n",
  //     uuids::to_string(cnt).c_str(), point);
}

void TrunkClient::OnCacheResend() {
  TrunkLink::OnCacheResend();

  std::lock_guard<std::mutex> lk(connect_cache_lock_);
  auto curt = std::chrono::steady_clock::now();

  // Удалим протухшие коннект-пакеты
  auto tail = std::remove_if(connect_cache_.begin(), connect_cache_.end(),
      [curt](PacketConnectCache& item) { return curt > item.Deadline; });
  if (tail != connect_cache_.end()) {
    // trlog("-- Removing %u pre-connect-packets\n", connect_cache_.end() -
    // tail);
    connect_cache_.erase(tail, connect_cache_.end());
  }


  for (auto& item : connect_cache_) {
    if (item.NextSend > curt) {
      continue;
    }
    item.NextSend = curt + std::chrono::milliseconds(kResendConnectTimeout);
    SendPacket(item.info);  // Перепосылка из кэша шлётся всегда

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

int TrunkClient::GetAvailableBuffer(ConnectID ctx) {
  std::unique_lock lk(trunk_buffer_lock_);
  auto curt = std::chrono::steady_clock::now();
  auto intr = std::chrono::duration_cast<std::chrono::microseconds>(
      curt - trunk_buffer_last_time_)
                  .count();

  trunk_buffer_last_size_ +=
      intr * kDefaultUdpTrafficSpeed;  // Учитываем, что со временем буфер
                                       // освобождается
  if (trunk_buffer_last_size_ > trunk_socket_buffer_size_) {
    trunk_buffer_last_size_ = trunk_socket_buffer_size_;
  }
  trunk_buffer_last_time_ = curt;

  return trunk_buffer_last_size_;
}

void TrunkClient::SendPacket(PacketInfo pkt) {
  if (cfg_settings_.LogTrunkPacket) {
    std::stringstream s;
    s << "Trunk Connect " << uuids::to_string(pkt.CtxID) << ": send packet "
      << pkt.PacketID << ", " << pkt.PacketSize << " bytes";
    cfg_settings_.OutputLog(s.str());
  }

  auto pd = pkt.PacketData;
  trunk_socket_.async_send_to(boost::asio::buffer(pd.get(), pkt.PacketSize),
      points_.front(),
      [pd](boost::system::error_code /*ec*/, std::size_t /*bytes_sent*/) {});

  // Пересчитаем свободный буфер
  std::unique_lock lk(trunk_buffer_lock_);
  trunk_buffer_last_size_ -=
      pkt.PacketSize +
      kUdpPacketOverhead;  // Размер свободного места может стать отрицательным
                           // - это нормально/допустимо
  lk.unlock();
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
    const std::vector<std::vector<boost::asio::ip::udp::endpoint>>& trpoints,
    std::function<std::shared_ptr<OutLink>(PointID)> link_fabric,
    const Settings& cfg)
    : TrunkLink(ctx, true, cfg), asio_context_(ctx), link_fabric_(link_fabric) {
  for (size_t i = 0; i < trpoints.size(); ++i) {
    for (auto& p : trpoints[i]) {
      auto& item =
          trunk_sockets_.emplace_back(ServerSocket{i, {ctx, p}, GetBuffer()});
      // Получим информацию о сокете
      boost::asio::socket_base::receive_buffer_size option;
      item.socket.get_option(option);
      int buf_size = option.value();
      if (buf_size < kMinimalUdpBufferSize) {
        buf_size = kMinimalUdpBufferSize;
      }

      // Все вычисления делаются в конструкторе, до старта всех операций.
      // Поэтому блокировка не требуется
      // TODO Точно не требуется???
      item.socket_buffer_size_ = (int)(buf_size * kUdpBufferDataPart);
      item.buffer_last_size_ = item.socket_buffer_size_;
      item.buffer_last_time_ = std::chrono::steady_clock::now();
    }
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


int TrunkServer::GetAvailableBuffer(ConnectID ctx) {
  ConnectInfo info;
  info.connect = ctx;

  if (!GetClientLink(info)) {
    // Нет информации о коннекте
    return kUdpBufferUnavailable;
  }

  auto& ts = trunk_sockets_[info.socket_index];

  std::unique_lock lk(buffer_lock_);
  auto curt = std::chrono::steady_clock::now();
  auto intr = std::chrono::duration_cast<std::chrono::microseconds>(
      curt - ts.buffer_last_time_)
                  .count();

  ts.buffer_last_size_ +=
      intr * kDefaultUdpTrafficSpeed;  // Учитываем, что со временем буфер
                                       // освобождается
  if (ts.buffer_last_size_ > ts.socket_buffer_size_) {
    ts.buffer_last_size_ = ts.socket_buffer_size_;
  }
  ts.buffer_last_time_ = curt;
  int res = ts.buffer_last_size_;
  lk.unlock();

  return res;
}


void TrunkServer::SendPacket(PacketInfo pkt) {
  // Найдём, куда отправлять
  ConnectInfo info;
  info.connect = pkt.CtxID;

  if (!GetClientLink(info)) {
    // Нет информации о коннекте
    // Неизвестно, куда отправлять данные
    if (cfg_settings_.LogTrunkPacket) {
      std::stringstream s;
      s << "Trunk Connect " << uuids::to_string(pkt.CtxID)
        << " doesn't exist: can't send packet " << pkt.PacketID << ", "
        << pkt.PacketSize << " bytes";
      cfg_settings_.OutputLog(s.str());
    }
    return;
  }

  if (cfg_settings_.LogTrunkPacket) {
    std::stringstream s;
    s << "Trunk Connect " << uuids::to_string(pkt.CtxID) << ": send packet "
      << pkt.PacketID << ", " << pkt.PacketSize << " bytes";
    cfg_settings_.OutputLog(s.str());
  }

  auto& ts = trunk_sockets_[info.socket_index];
  auto buf = pkt.PacketData;
  ts.socket.async_send_to(boost::asio::buffer(buf.get(), pkt.PacketSize),
      info.client,
      [buf](boost::system::error_code /*ec*/, std::size_t /*bytes_sent*/) {});

  // Пересчитаем свободный буфер
  std::unique_lock lk(buffer_lock_);
  ts.buffer_last_size_ -=
      pkt.PacketSize +
      kUdpPacketOverhead;  // Размер свободного места может стать отрицательным
                           // - это нормально/допустимо
  lk.unlock();
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

#include "trunklink.h"

#include <chrono>
#include <fstream>
#include <iostream>

#include "settings.h"
#include "trace.h"
#include "tracer.h"

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


TrunkLink::TrunkLink(boost::asio::io_context& ctx, bool server_side,
    const Settings& cfg, Tracer* tracer)
    : cfg_settings_(cfg),
      tracer_(tracer),
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
  my_packet_symbol_ = server_side_ ? '&' : '#';
  other_packet_symbol_ = server_side_ ? '#' : '&';

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
      return res;  // Если номер пакета приблизится к переполнению, то он
                   // однозначно напорется на kOverflowError
    }
  }
  return kConnectionAbsentError;
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

    ClearDataOrphans();

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

void TrunkLink::ClearDataOrphans() {
  std::unique_lock lk(out_links_lock_);
  auto curt = std::chrono::steady_clock::now();
  auto tail1 = std::remove_if(data_cache_.begin(), data_cache_.end(),
      [curt](const DataInfo& info) { return curt > info.deadline; });
  data_cache_.erase(tail1, data_cache_.end());
  auto tail2 = std::remove_if(release_cache_.begin(), release_cache_.end(),
      [curt](const ReleaseInfo& info) { return curt > info.deadline; });
  release_cache_.erase(tail2, release_cache_.end());
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
      if (tracer_) {
        tracer_->Message(item.connect_id, "LONG-DEAD CONNECTION. Force remove");
      }

      dead_cnt.push_back(item.connect_id);
      continue;
    }

    assert(item.link.get());
    if (curt > item.deadlink_timeout_) {
      if (tracer_) {
        tracer_->Message(item.connect_id, "no-live connect");
      }

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
    if (tracer_) {
      tracer_->Message(item.connect_id, "      live");
    }
  }
  lk.unlock();

  // Удалим мёртвые соединения
  for (auto& id : dead_cnt) {
    RemoveOutLink(id);
  }
}

bool TrunkLink::AddOutLink(uuids::uuid cnt, std::shared_ptr<OutLink> link) {
  std::lock_guard lk(out_links_lock_);

  auto exist_link = GetOutLinkWOLock(cnt);
  if (exist_link) {
    // Такая внешняя связь уже существует
    // Странно, но теоретически возможно
    // Ничего не делаем, выходим, забываем про эту связь
    std::cerr << "??: uuid generator creates double: " << uuids::to_string(cnt)
              << std::endl;
    return false;
  }

  OutLinkInfo info;
  info.connect_id = cnt;
  info.link = link;
  info.next_index_to_trunk = 0;
  info.deadlink_timeout_ = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(kDeadOutLinkTimeout);
  out_links_.push_back(info);
  return true;
}

void TrunkLink::RunOutLink(uuids::uuid cnt) {
  auto link = GetOutLink(cnt);
  assert(link);
  if (!link) {
    return;
  }

  link->Run(this, cnt);

  // Выгребем кэш данных и закрытий соединений. Вдруг есть недоставленные данные
  for (auto it = data_cache_.begin(); it != data_cache_.end(); /* nothing */) {
    if (it->CtxID != cnt) {
      // Данные неподходящие. Пропускаем
      ++it;
    } else {
      // Данные для этого соединения
      out_stream_counter_ += it->size;
      link->SendData(it->PacketID, it->data.get(), it->size);
      it = data_cache_.erase(it);
    }
  }

  for (auto it = release_cache_.begin(); it != release_cache_.end();
      /* nothing */) {
    if (it->CtxID != cnt) {
      // Закрытие неподходящее. Пропускаем
      ++it;
    } else {
      // Закрытие для этого соединения
      link->Stop(it->PacketID, OutLink::kStopReleaseCommand);
      it = release_cache_.erase(it);
    }
  }
}


void TrunkLink::SendData(ConnectID cnt, const void* data, size_t data_size) {
  if (tracer_) {
    tracer_->Message(cnt, "-> trunk");
  }

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
  if (pkt_index == kConnectionAbsentError) {
    // Соединения уже нет (дубликаты подтверждений). Ничего не делаем
    return;
  } else if (pkt_index == kOverflowError) {
    // По соединению переслали очень много данных. Закрыть бы его
    // TODO Закрыть соедидение в таком немного экзотическом случае
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
  if (tracer_) {
    std::stringstream ss;
    ss << "Form packet #" << pkt_index << " to trunk";
    tracer_->Message(cnt, ss.str());
  }

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
    if (ava < static_cast<int>(info.PacketSize)) {
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

void TrunkLink::TracerMessage(uuids::uuid id, const std::string& msg) {
  if (tracer_) {
    tracer_->Message(id, msg);
  }
}

void TrunkLink::ProcessTrunkData(size_t socket_index,
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
      ProcessConnectData(
          cnt, static_cast<const PacketConnect*>(hdr), socket_index, client);
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
        SendDataAck(cnt, pd->PacketIndex, socket_index, client);
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

        SendDataAck(cnt, pd->PacketIndex, socket_index, client);
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
  if (tracer_) {
    std::stringstream ss;
    ss << "Receive packet " << other_packet_symbol_ << info->PacketIndex;
    tracer_->Message(cnt, ss.str());
  }

  // Выдадим данные на внешний линк
  auto link = GetOutLink(cnt);
  if (link) {
    out_stream_counter_ += info->DataSize;
    link->SendData(info->PacketIndex, data, info->DataSize);
  } else {
    // Сохраним данные в кэше
    std::unique_lock lk(out_links_lock_);
    DataInfo d;
    d.CtxID = cnt;
    d.PacketID = info->PacketIndex;
    d.size = info->DataSize;
    d.data = GetBuffer();
    if (d.size > 0) {
      memcpy(d.data.get(), data, d.size);
    }
    d.deadline = std::chrono::steady_clock::now() +
                 std::chrono::milliseconds(kOrphanDataTimeout);

    data_cache_.push_back(d);
  }
}

void TrunkLink::ProcessAckData(uuids::uuid cnt, uint32_t packet_index) {
  if (tracer_) {
    std::stringstream ss;
    ss << "  Ack packet " << my_packet_symbol_ << packet_index;
    tracer_->Message(cnt, ss.str());
  }

  if (cfg_settings_.LogTrunkPacket) {
    std::stringstream s;
    s << "Trunk Connect " << uuids::to_string(cnt) << ": receive ack packet "
      << packet_index;
    cfg_settings_.OutputLog(s.str());
  }

  size_t ping = kUndefinedSizeT;

  std::unique_lock lk(packet_data_cache_lock_);
  auto tail = std::remove_if(packet_data_cache_.begin(),
      packet_data_cache_.end(),
      [cnt, packet_index, &ping](const PacketDataCache& item) {
        if ((item.info.CtxID == cnt) && (item.info.PacketID == packet_index)) {
          ping = std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - item.FirstSend)
                     .count();
          return true;
        }
        return false;
      });
  if (tail != packet_data_cache_.end()) {
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
  if (link) {
    link->Stop(packet_id, OutLink::kStopReleaseCommand);
  } else {
    // Сохраним данные в кэше
    std::unique_lock lk(out_links_lock_);
    ReleaseInfo d;
    d.CtxID = cnt;
    d.PacketID = packet_id;
    d.deadline = std::chrono::steady_clock::now() +
                 std::chrono::milliseconds(kOrphanDataTimeout);

    release_cache_.push_back(d);
  }
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

  auto tracer = tracer_;

  auto tail = std::remove_if(packet_data_cache_.begin(),
      packet_data_cache_.end(), [curt, tracer](PacketDataCache& item) {
        if (curt > item.Deadline) {
          if (tracer) {
            std::stringstream ss;
            ss << "Remove deadline packet #" << item.info.PacketID;
            tracer->Message(item.info.CtxID, ss.str());
          }

          return true;
        }
        return false;
      });
  if (tail != packet_data_cache_.end()) {
    deadp = packet_data_cache_.end() - tail;
    packet_data_cache_.erase(tail, packet_data_cache_.end());
  }

  for (auto it = packet_data_cache_.begin(); it != packet_data_cache_.end();
       ++it) {
    if (curt > it->NextSend) {
      // Перепосылаем пакет
      if (tracer_) {
        std::stringstream ss;
        ss << "Resend packet #" << it->info.PacketID;
        tracer_->Message(it->info.CtxID, ss.str());
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

  if (tracer_) {
    tracer_->FinishTrace(cnt);
  }
}

void TrunkLink::SendDataAck(ConnectID cnt, uint32_t packet_id,
    size_t socket_index, boost::asio::ip::udp::endpoint target) {
  // Подтверждение шлём, даже если соединение уже "умерло"
  if (tracer_) {
    std::stringstream ss;
    ss << "Send data ack for packet " << other_packet_symbol_ << packet_id;
    tracer_->Message(cnt, ss.str());
  }
  assert(sizeof(PacketAck) <= kPacketBufferSize);
  auto buf = GetBuffer();
  auto pkt = (PacketAck*)(buf.get());
  CopyConnectID(pkt->ConnectID, cnt);
  pkt->PacketCommand =
      server_side_ ? kTrunkCommandAckDataOut : kTrunkCommandAckDataIn;
  pkt->PacketIndex = packet_id;
  PacketInfo pi;
  pi.CtxID = cnt;
  pi.PacketID = packet_id;
  pi.PacketData = buf;
  pi.PacketSize = sizeof(PacketAck);
  SendPacket(socket_index, target, pi);
}


TrunkClient::TrunkClient(boost::asio::io_context& ctx,
    const std::vector<boost::asio::ip::udp::endpoint>& trpoints,
    const Settings& cfg, Tracer* tracer)
    : TrunkLink(ctx, false, cfg, tracer),
      points_(trpoints),
      trunk_socket_(ctx, boost::asio::ip::udp::v4()) {
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
  TracerMessage(cnt, "Send connection information");

  // trlog("Send connect information. Id: %s, Point %u\n",
  //     uuids::to_string(cnt).c_str(), point);
}

void TrunkClient::OnCacheResend() {
  TrunkLink::OnCacheResend();

  std::lock_guard<std::mutex> lk(connect_cache_lock_);
  auto curt = std::chrono::steady_clock::now();

  // Удалим протухшие коннект-пакеты
  auto tail = std::remove_if(connect_cache_.begin(), connect_cache_.end(),
      [this, curt](PacketConnectCache& item) {
        if (curt <= item.Deadline) {
          return false;
        }
        std::stringstream ss;
        ss << "! Connect information for " << uuids::to_string(item.info.CtxID)
           << " is outdated";
        TracerMessage(item.info.CtxID, "! Connect information is outdated");
        return true;
      });
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
    TracerMessage(item.info.CtxID, "ReSend connection information");
  }
}

TrunkClient::~TrunkClient() {}

void TrunkClient::AddConnect(PointID point, std::shared_ptr<OutLink> link) {
  assert(link);

  auto cnt = link->GetConnectID();
  if (!AddOutLink(cnt, link)) {
    return;
  }

  SendConnectInformation(cnt, point, kResendTimeout);
  RunOutLink(cnt);
}


void TrunkClient::ReceiveTrunkData() {
  trunk_socket_.async_receive_from(
      boost::asio::buffer(trunk_read_buffer_, kPacketBufferSize),
      trunk_read_point_,
      [this](boost::system::error_code err, std::size_t data_size) {
        if (err) {
          // TODO Error processing
        } else {
          ProcessTrunkData(0, trunk_read_point_, trunk_read_buffer_, data_size);
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

  trunk_buffer_last_size_ += static_cast<int>(
      intr * kDefaultUdpTrafficSpeed);  // Учитываем, что со временем буфер
                                        // освобождается
  if (trunk_buffer_last_size_ > trunk_socket_buffer_size_) {
    trunk_buffer_last_size_ = trunk_socket_buffer_size_;
  }
  trunk_buffer_last_time_ = curt;

  return trunk_buffer_last_size_;
}

void TrunkClient::SendPacket(PacketInfo pkt) {
  // На клиентской части всего один транковый сокет, поэтому всё отправляем на
  // 0-ой индекс
  SendPacket(
      0, points_.front(), pkt);  // TODO Избавиться от ещё одной виртуализации
}


void TrunkClient::SendPacket(size_t socket_index,
    boost::asio::ip::udp::endpoint target, PacketInfo pkt) {
  auto pd = pkt.PacketData;
  trunk_socket_.async_send_to(boost::asio::buffer(pd.get(), pkt.PacketSize),
      target,
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
  TracerMessage(cnt, "Receive connection ack");

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
    std::function<std::shared_ptr<OutLink>(PointID, ConnectID)> link_fabric,
    const Settings& cfg, Tracer* tracer)
    : TrunkLink(ctx, true, cfg, tracer),
      asio_context_(ctx),
      link_fabric_(link_fabric) {
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
            ProcessTrunkData(
                index, ts.client_holder, ts.buffer.get(), data_size);
          }
        }

        RequestReadingTrunk(index);
      });
}


void TrunkServer::ProcessConnectData(uuids::uuid cnt, const PacketConnect* info,
    size_t socket_index, boost::asio::ip::udp::endpoint target) {
  bool created = false;
  std::unique_lock lk(create_outlink_lock_);
  if (!GetOutLink(cnt)) {
    // Соединения такого нет - будем создавать
    if (tracer_) {
      tracer_->CreateTrace(cnt);
    }

    // Создадим внешний коннект
    auto ol = link_fabric_(info->PointID, cnt);
    if (!ol) {
      // TODO ERROR Can't create link
      std::cerr << "ERROR: Can't create outlink from fabric" << std::endl;
      return;
    }
    if (!AddOutLink(cnt, ol)) {
      assert(false);  // Вообще по логике такого быть не должно
      return;
    }
    created = true;
  }
  // else
  // Коннект уже существует: возможно пришёл дубликат команды на создание
  // соединения - штатная ситуация
  lk.unlock();

  // Отправим подтверждение на получение пакета. Даже если это дубликат
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
  SendPacket(socket_index, target, pi);
  TracerMessage(cnt, "  Ack connection creation");

  if (created) {
    // Запустимся
    RunOutLink(cnt);
  }
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

  ts.buffer_last_size_ += static_cast<int>(
      intr * kDefaultUdpTrafficSpeed);  // Учитываем, что со временем буфер
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

  SendPacket(
      info.socket_index, info.client, pkt);  // TODO Убрать лишнюю виртуализацию
}


void TrunkServer::SendPacket(size_t socket_index,
    boost::asio::ip::udp::endpoint target, PacketInfo pkt) {
  auto& ts = trunk_sockets_[socket_index];
  auto buf = pkt.PacketData;
  ts.socket.async_send_to(boost::asio::buffer(buf.get(), pkt.PacketSize),
      target,
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

#include "trunklink.h"

#include <chrono>
#include <iostream>

namespace bai = boost::asio::ip;

// TODO Descr
void CopyConnectID(uint8_t dest[16], const uuids::uuid& src) {
  auto cnt_bin = src.as_bytes();
  assert(cnt_bin.size_bytes() == 16);
  memcpy(dest, cnt_bin.data(), 16);
}


TrunkLink::TrunkLink(boost::asio::io_context& ctx, bool server_side)
    : server_side_(server_side),
      cache_timer_(ctx)

{
  RequestCacheResend();
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


void TrunkLink::RequestCacheResend() {
  std::chrono::milliseconds intrv{kResendTick};
  cache_timer_.expires_after(intrv);
  cache_timer_.async_wait([this](const boost::system::error_code& err) {
    // TODO Error checking

    OnCacheResend();

    RequestCacheResend();
  });
}


void TrunkLink::SendLivePacket() {
  // TODO Implemntation

  // TODO call this every 1-3-5 minutes
}


void TrunkLink::SendData(ConnectID cnt, const void* data, size_t data_size) {
  //  std::printf(
  //      "TRACE: Send %u bytes of data into trunk\n", (unsigned int)data_size);

  if (data_size > kMaxChunkSize) {
    assert(false);
    return;
  }

  auto pkt_index = GetNextPacketIndex(cnt);
  if (pkt_index == kBadPacketIndex) {
    // TODO ERROR
    return;
  }

  // Сформируем сам пакет
  auto buf = GetBuffer();
  auto pkt = (PacketData*)(buf.get());
  CopyConnectID(pkt->ConnectID, cnt);
  pkt->PacketCommand =
      server_side_ ? kTrunkCommandDataIn : kTrunkCommandDataOut;
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
  pc.Deadline = curt + std::chrono::milliseconds(kDeadlineTimeout);
  pc.NextSend = curt + std::chrono::milliseconds(kResendTimeout);

  std::unique_lock<std::mutex> lk(packet_data_cache_lock_);
  packet_data_cache_.push_back(pc);
  lk.unlock();

  SendPacket(info);
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
      if (data_size < sizeof(PacketAck)) {
        // Неполный формат
        return;
      }

      {
        auto pa = static_cast<const PacketAck*>(hdr);
        ProcessAckData(cnt, pa);
      }
      break;
  }
}

void TrunkLink::ProcessDataToOutlink(
    uuids::uuid cnt, const PacketData* info, const void* data) {
  std::printf("TRACE: -- Got %u bytes from trunk for connect %s\n",
      (unsigned int)info->DataSize, uuids::to_string(cnt).c_str());
  auto link = GetOutLink(cnt);
  if (!link) {
    // Нет такого подключения
    // TODO Error process
    return;
  }

  // Отправим подтверждение на получение пакета
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
  pi.PacketSize = sizeof(PacketHeader);
  SendPacket(pi);

  // Выдадим данные на внешний линк
  link->SendData(info->PacketIndex, data, info->DataSize);
}

void TrunkLink::ProcessAckData(uuids::uuid cnt, const PacketAck* info) {
  // TODO IMPLEMENT
}

void TrunkLink::IntAddOutLinkWOLock(
    uuids::uuid cnt, std::shared_ptr<OutLink> link) {
  OutLinkInfo info;
  info.connect_id = cnt;
  info.link = link;
  info.next_index_to_trunk = 0;
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
  // TODO IMPLEMENT
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

  // TRACE
  std::printf("-- Send connect information. Id: %s, Point %u\n",
      uuids::to_string(cnt).c_str(), point);
}

void TrunkClient::OnCacheResend() {
  TrunkLink::OnCacheResend();

  auto curt = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lk(connect_cache_lock_);
  for (auto& item : connect_cache_) {
    // TODO process deadline ????

    if (item.NextSend > curt) {
      continue;
    }
    item.NextSend = curt + std::chrono::milliseconds(kResendTimeout);
    SendPacket(item.info);

    // TRACE
    std::printf("-- ReSend connect information for id %s\n",
        uuids::to_string(item.info.CtxID).c_str());
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
  // TRACE
  std::printf(
      "-- Receive ack for connection id %s\n", uuids::to_string(cnt).c_str());

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
  //  std::printf("TRACE: -- Request connect to point %u. Id: %s\n",
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

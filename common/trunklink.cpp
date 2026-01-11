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


TrunkLink::TrunkLink() {}

void TrunkLink::ProcessTrunkData(
    boost::asio::ip::udp::endpoint client, const void* data, size_t data_size) {
  if (data_size < sizeof(PacketHeader)) {
    // Битый пакет непонятно откуда и от кого
    return;
  }

  auto hdr = static_cast<const PacketHeader*>(data);
  uuids::uuid cnt(hdr->ConnectID, hdr->ConnectID + sizeof(hdr->ConnectID));
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
  }
}

void TrunkLink::AddOutLink(uuids::uuid cnt, std::shared_ptr<OutLink> link) {
  OutLinkInfo info;
  info.connect_id = cnt;
  info.link = link;

  std::unique_lock lk(out_links_lock_);
  out_links_.push_back(info);
  lk.unlock();

  link->Run(this, cnt);
}


TrunkClient::TrunkClient(boost::asio::io_context& ctx,
    const std::vector<boost::asio::ip::udp::endpoint>& trpoints)
    : points_(trpoints),
      trunk_socket_(ctx, boost::asio::ip::udp::v4()),
      cache_timer_(ctx) {
  // Инициализация генератора uuid
  std::random_device rd;
  auto seed_data = std::array<int, std::mt19937::state_size>{};
  std::generate(std::begin(seed_data), std::end(seed_data), std::ref(rd));
  std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
  generator_ = std::mt19937(seq);

  ReceiveTrunkData();

  RequestCacheResend();
}

TrunkClient::~TrunkClient() {}

void TrunkClient::AddConnect(PointID point, std::shared_ptr<OutLink> link) {
  ConnectInfo ci;

  uuids::uuid_random_generator gen{generator_};
  uuids::uuid id = gen();
  assert(id.as_bytes().size() == kConnectIDSize);
  ci.ID = id;
  ci.Link = link;
  ci.NextIndex = 0;

  std::unique_lock<std::mutex> lk(connects_lock_);
  connects_.push_back(ci);
  lk.unlock();

  SendConnect(id, point, kResendTimeout);
  ci.Link->Run(this, ci.ID);
}

void TrunkClient::ReleaseConnect(ConnectID cnt) noexcept {
  ConnectInfo ci;
  bool find = false;
  std::unique_lock<std::mutex> lk(connects_lock_);
  for (auto i = connects_.begin(); i != connects_.end(); ++i) {
    if (i->ID == cnt) {
      ci = *i;
      find = true;
      connects_.erase(i);
      break;
    }
  }
  lk.unlock();

  if (!find) {
    // Нет такого коннекта
    return;
  }
}

void TrunkClient::SendConnect(
    ConnectID cnt, PointID point, unsigned int timeout) {
  auto buf = GetBuffer();
  PacketConnectCache pc;
  pc.PacketData = buf;
  pc.PacketSize = sizeof(PacketConnect);
  pc.CtxID = cnt;

  auto curt = std::chrono::steady_clock::now();
  pc.Deadline = curt + std::chrono::milliseconds(kDeadlineTimeout);
  pc.NextSend = curt + std::chrono::milliseconds(kResendTimeout);

  assert(sizeof(PacketConnect) <= kPacketBufferSize);
  auto pkt = (PacketConnect*)(pc.PacketData.get());
  auto cnt_bin = cnt.as_bytes();
  assert(cnt_bin.size_bytes() == kConnectIDSize);
  memcpy(pkt->ConnectID, cnt_bin.data(), kConnectIDSize);
  pkt->PacketCommand = kTrunkCommandCreateConnect;
  pkt->PointID = point;
  pkt->Timeout = timeout;

  std::unique_lock<std::mutex> lk(packet_cache_lock_);
  packet_connect_cache_.push_back(pc);
  lk.unlock();

  trunk_socket_.async_send_to(boost::asio::buffer(buf.get(), pc.PacketSize),
      points_.front(),
      [buf](boost::system::error_code /*ec*/, std::size_t /*bytes_sent*/) {});
}

std::shared_ptr<TrunkClient::PacketBuffer> TrunkClient::GetBuffer() {
  return std::make_shared<TrunkClient::PacketBuffer>();
}

uint32_t TrunkClient::GetPacketIndex(ConnectID cnt) {
  std::lock_guard<std::mutex> lk(connects_lock_);
  for (auto& item : connects_) {
    if (item.ID == cnt) {
      auto res = item.NextIndex;
      ++item.NextIndex;
      return res;
    }
  }
  return kBadPacketIndex;
}

void TrunkClient::CacheResend() {
  auto curt = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lk(packet_cache_lock_);
  for (auto& item : packet_connect_cache_) {
    // TODO process deadline ????

    if (item.NextSend > curt) {
      continue;
    }
    item.NextSend = curt + std::chrono::milliseconds(kResendTimeout);
    auto pd = item.PacketData;
    trunk_socket_.async_send_to(
        boost::asio::buffer(item.PacketData.get(), item.PacketSize),
        points_.front(),
        [pd](boost::system::error_code /*ec*/, std::size_t /*bytes_sent*/) {});
  }
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

void TrunkClient::RequestCacheResend() {
  std::chrono::milliseconds intrv{kResendTick};
  cache_timer_.expires_after(intrv);
  cache_timer_.async_wait([this](const boost::system::error_code& err) {
    // TODO Error checking

    CacheResend();

    RequestCacheResend();
  });
}


void TrunkClient::ProcessAckConnectData(
    uuids::uuid cnt, const PacketHeader* info) {
  std::lock_guard<std::mutex> lk(packet_cache_lock_);
  for (auto it = packet_connect_cache_.begin();
       it != packet_connect_cache_.end();) {
    if (it->CtxID != cnt) {
      ++it;
    } else {
      it = packet_connect_cache_.erase(it);
    }
  }
}


bool TrunkClient::SendData(ConnectID cnt, void* data, size_t data_size) {
  std::printf("DEBUG: send %u bytes to connect %s\n", (unsigned int)data_size,
      uuids::to_string(cnt).c_str());

  auto buf = GetBuffer();
  PacketDataCache pd;
  pd.PacketData = buf;
  pd.PacketSize = sizeof(PacketData) + data_size;
  pd.CtxID = cnt;

  assert((sizeof(PacketData) + data_size) <= kPacketBufferSize);
  auto pkt = (PacketData*)(pd.PacketData.get());
  auto cnt_bin = cnt.as_bytes();
  assert(cnt_bin.size_bytes() == kConnectIDSize);
  memcpy(pkt->ConnectID, cnt_bin.data(), kConnectIDSize);
  pkt->PacketCommand = kTrunkCommandDataOut;
  pkt->PacketIndex = GetPacketIndex(cnt);
  if (pkt->PacketIndex == kBadPacketIndex) {
    return false;
  }
  pkt->DataSize = data_size;

  std::unique_lock<std::mutex> lk(packet_cache_lock_);
  packet_data_cache_.push_back(pd);
  lk.unlock();

  trunk_socket_.async_send_to(boost::asio::buffer(buf.get(), pd.PacketSize),
      points_.front(),
      [buf](boost::system::error_code /*ec*/, std::size_t /*bytes_sent*/) {});

  return false;
}

TrunkServer::TrunkServer(boost::asio::io_context& ctx,
    const std::vector<boost::asio::ip::udp::endpoint>& trpoints,
    std::function<std::shared_ptr<OutLink>(PointID)> link_fabric)
    : asio_context_(ctx), link_fabric_(link_fabric) {
  for (auto& p : trpoints) {
    trunk_sockets_.emplace_back(ServerSocket{{ctx, p}, GetBuffer()});
  }

  for (size_t index = 0; index < trunk_sockets_.size(); ++index) {
    ReceiveTrunkData(index);
  }
}

TrunkServer::~TrunkServer() {}


std::shared_ptr<TrunkServer::PacketBuffer> TrunkServer::GetBuffer() {
  return std::make_shared<TrunkServer::PacketBuffer>();
}

void TrunkServer::ReceiveTrunkData(size_t index) {
  auto& ts = trunk_sockets_[index];
  ts.socket.async_receive_from(
      boost::asio::buffer(ts.buffer.get(), kPacketBufferSize), ts.client_holder,
      [this, index](boost::system::error_code err, std::size_t data_size) {
        if (err) {
          // TODO Error processing
        } else {
          auto& ts = trunk_sockets_[index];
          uuids::uuid cnt;
          if (!GetPacketConnectID(ts.buffer.get(), data_size, cnt)) {
            // Битый пакет
            // TODO что делать
          } else {
            AddClientLink({cnt, index, ts.client_holder});
            ProcessTrunkData(ts.client_holder, ts.buffer.get(), data_size);
          }
        }

        ReceiveTrunkData(index);
      });
}


void TrunkServer::ProcessConnectData(
    uuids::uuid cnt, const PacketConnect* info) {
  // TRACE
  std::printf("-- Request connect to point %u. Id: %s\n", info->PointID,
      uuids::to_string(cnt).c_str());
  std::cout.flush();

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

  // Создадим внешний коннект
  auto ol = link_fabric_(info->PointID);
  if (!ol) {
    // TODO ERROR Can't create link
    return;
  }

  AddOutLink(cnt, ol);
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

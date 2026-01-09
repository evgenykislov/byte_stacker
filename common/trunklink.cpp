#include "trunklink.h"

#include <iostream>


const size_t kConnectIDSize = 16;
const unsigned int kTimeout = 300;

struct PacketHeader {
  uint8_t ConnectID[kConnectIDSize];
  TrunkCommand PacketCommand;
};

struct PacketConnect: PacketHeader {
  uint32_t PointID;
  uint32_t Timeout;
};


struct PacketData: PacketHeader {
  uint32_t PacketIndex;
  uint32_t DataSize;
  // uint8_t Data[DataSize];
};


struct PacketAck: PacketHeader {
  uint32_t PacketIndex;
};

TrunkClient::TrunkClient(boost::asio::io_context& ctx,
    const std::vector<boost::asio::ip::udp::endpoint>& trpoints)
    : points_(trpoints), trunk_socket_(ctx, boost::asio::ip::udp::v4()) {
  // Инициализация генератора uuid
  std::random_device rd;
  auto seed_data = std::array<int, std::mt19937::state_size>{};
  std::generate(std::begin(seed_data), std::end(seed_data), std::ref(rd));
  std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
  generator_ = std::mt19937(seq);
}

TrunkClient::~TrunkClient() {}

ConnectID TrunkClient::CreateConnect(PointID point,
    std::function<void(ConnectID)> on_disconnect,
    std::function<void(ConnectID, void*, size_t)> on_data) {
  ConnectInfo ci;

  uuids::uuid_random_generator gen{generator_};
  uuids::uuid id = gen();
  assert(id.as_bytes().size() == kConnectIDSize);
  ci.ID = id;
  ci.OnDisconnect = on_disconnect;
  ci.OnData = on_data;
  ci.NextIndex = 0;
  // ci.Status = // TODO Remove status ???

  std::unique_lock<std::mutex> lk(connects_lock_);
  connects_.push_back(ci);
  lk.unlock();

  SendConnect(id, point, kTimeout);

  //  trunk_socket_.async_send_to()

  return id;
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

  ci.OnDisconnect(ci.ID);
}

void TrunkClient::SendConnect(
    ConnectID cnt, PointID point, unsigned int timeout) {
  auto buf = GetBuffer();
  PacketConnectCache pc;
  pc.PacketData = buf;
  pc.PacketSize = sizeof(PacketConnect);
  pc.CtxID = cnt;

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

TrunkServer::TrunkServer(boost::asio::io_context& ctx): asio_context_(ctx) {}

TrunkServer::~TrunkServer() {}

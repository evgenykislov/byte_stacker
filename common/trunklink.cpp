#include "trunklink.h"

#include <iostream>

enum Command : uint32_t {
  kCommandCreateConnect = 1,
  kCommandReleaseConnect = 2,
  kCommandAckCreateConnect = 3,
  kCommandDataOut = 11,
  kCommandDataIn = 12,
  kCommandAckDataOut = 21,
  kCommandAckDataIn = 22
};

const size_t kConnectIDSize = 16;

struct PacketHeader {
  Command PacketCommand;
  uint8_t ConnectID[kConnectIDSize];
};

struct PacketConnect: PacketHeader {
  uint32_t PointID;
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
    : points_(trpoints) {
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

  std::unique_lock<std::mutex> lk(connect_lock_);
  connects_.push_back(ci);
  lk.unlock();


  return id;
}

void TrunkClient::ReleaseConnect(ConnectID cnt) noexcept {
  ConnectInfo ci;
  bool find = false;
  std::unique_lock<std::mutex> lk(connect_lock_);
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

bool TrunkClient::SendData(ConnectID cnt, void* data, size_t data_size) {
  std::printf("DEBUG: send %u bytes to connect %s\n", data_size,
      uuids::to_string(cnt).c_str());
  return false;
}

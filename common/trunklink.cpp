#include "trunklink.h"

#include <iostream>


TrunkClient::TrunkClient(boost::asio::io_context& ctx) {
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

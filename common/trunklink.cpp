#include "trunklink.h"


TrunkClient::TrunkClient(boost::asio::io_context& ctx) {}

TrunkClient::~TrunkClient() {}

ConnectID TrunkClient::CreateConnect(PointID point,
    std::function<void(ConnectID)> OnDisconnect,
    std::function<void(ConnectID, void*, size_t)> OnData) {
  return uuids::uuid();
}

void TrunkClient::ReleaseConnect(ConnectID cnt) {}

bool TrunkClient::SendData(ConnectID cnt, void* data, size_t data_size) {
  return false;
}

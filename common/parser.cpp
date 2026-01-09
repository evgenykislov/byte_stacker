#include "parser.h"


bool ParsePoint(std::string arg_wo_prefix, unsigned int& id,
    std::string& address, uint16_t& port) {
  auto p = arg_wo_prefix.find('=');
  if (p == std::string::npos || p == 0) {
    return false;
  }
  auto sid = arg_wo_prefix.substr(0, p);
  try {
    std::size_t s;
    id = static_cast<unsigned int>(std::stoul(sid, &s));
    if (s != (std::size_t)p) {
      throw std::runtime_error("bad format of id");
    }
  } catch (std::exception&) {
    return false;
  }

  auto adr = arg_wo_prefix.substr(p + 1);
  auto p1 = adr.find(':');
  if (p1 == std::string::npos || p1 == 0) {
    return false;
  }

  address = adr.substr(0, p1);
  auto sport = adr.substr(p1 + 1);
  try {
    std::size_t s;
    port = (unsigned short)std::stoul(sport, &s);
    if (s != sport.size()) {
      throw std::runtime_error("bad format of port");
    }

    return true;
  } catch (std::exception&) {
    return false;
  }
  return false;
}


bool ParsePoint(std::string arg_wo_prefix, unsigned int& id,
    boost::asio::ip::tcp::endpoint& point) {
  std::string adr;
  uint16_t port;
  if (!ParsePoint(arg_wo_prefix, id, adr, port)) {
    return false;
  }

  try {
    point.address(boost::asio::ip::make_address_v4(adr));
    point.port(port);
    return true;
  } catch (std::exception&) {
    return false;
  }
  return false;
}


bool ParseTrunkPoint(std::string arg_wo_prefix,
    std::vector<boost::asio::ip::udp::endpoint>& points) {
  points.clear();
  auto p1 = arg_wo_prefix.find(':');
  if (p1 == std::string::npos || p1 == 0) {
    return false;
  }

  auto sip = arg_wo_prefix.substr(0, p1);
  auto sports = arg_wo_prefix.substr(p1 + 1);
  try {
    auto ip = boost::asio::ip::make_address_v4(sip);
    while (!sports.empty()) {
      std::string chunk;
      auto p2 = sports.find(',');
      if (p2 == std::string::npos) {
        chunk = sports;
        sports.clear();
      } else {
        chunk = sports.substr(0, p2);
        sports = sports.substr(p2 + 1);
      }

      if (chunk.empty()) {
        return false;
      }

      std::size_t s;
      unsigned short iport = (unsigned short)std::stoul(chunk, &s);
      if (s != chunk.size()) {
        throw std::runtime_error("bad format of port");
      }

      boost::asio::ip::udp::endpoint pt;
      pt.address(ip);
      pt.port(iport);
      points.push_back(pt);
    }
    return true;
  } catch (std::exception&) {
    return false;
  }
  return false;
}

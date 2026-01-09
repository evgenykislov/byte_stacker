#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <utility>

#include <boost/asio.hpp>

// TODO descr
bool ParsePoint(std::string arg_wo_prefix, unsigned int& id,
    std::string& address, uint16_t& port);

// TODO descr
bool ParsePoint(std::string arg_wo_prefix, unsigned int& id,
    boost::asio::ip::tcp::endpoint& point);

// TODO Descr
bool ParseTrunkPoint(std::string arg_wo_prefix,
    std::vector<boost::asio::ip::udp::endpoint>& points);

#endif  // PARSER_H

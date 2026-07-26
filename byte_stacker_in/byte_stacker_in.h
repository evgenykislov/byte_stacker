// byte_stacker_in.h : Include file for standard system include files,
// or project specific include files.

#ifndef BYTE_STACKER_IN_H
#define BYTE_STACKER_IN_H

#include <iostream>
#include <map>
#include <utility>
#include <vector>

#include <boost/asio.hpp>

struct Settings;
class Tracer;


int RunClient(
    std::map<unsigned int, boost::asio::ip::tcp::endpoint> local_points,
    std::vector<boost::asio::ip::udp::endpoint> trunk_points,
    const Settings& cfg);

#endif

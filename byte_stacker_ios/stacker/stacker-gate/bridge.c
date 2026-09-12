//
//  bridge.c
//  stacker-gate
//
//  Created by Евгений on 12.09.2026.
//


extern "C" {
  #include "bridge.h"
}

#include "stacker-lib/byte_stacker_in.h"
#include "stacker-lib/settings.h"


extern "C" void RunStackerLib() {

  std::map<unsigned int, boost::asio::ip::tcp::endpoint> local_points;
  std::vector<boost::asio::ip::udp::endpoint> trunk_points;
  Settings cfg;
  std::shared_ptr<Tracer> tracer;
  
  RunClient(local_points, trunk_points, cfg, tracer);
}

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
#include "stacker-lib/parser.h"
#include "stacker-lib/settings.h"


std::thread thread_;

extern "C" void RunStackerLib() {
  std::thread t([](){
    std::map<unsigned int, boost::asio::ip::tcp::endpoint> local_points;
    std::vector<boost::asio::ip::udp::endpoint> trunk_points;
    Settings cfg;
    std::shared_ptr<Tracer> tracer;
    
    
    boost::asio::ip::tcp::endpoint ep;
    unsigned int id;
    if (ParsePoint("1=127.0.0.1:3128", id, ep)) {
      local_points[id] = ep;
    }
    if (ParsePoint("2=127.0.0.1:3130", id, ep)) {
      local_points[id] = ep;
    }
    if (!ParseTrunkPoint("", trunk_points)) {
      return;
    }
    
    RunClient(local_points, trunk_points, cfg, tracer);
  });
  
  std::swap(thread_, t);
}

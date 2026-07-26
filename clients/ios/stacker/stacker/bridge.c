//
//  bridge.c
//  stacker
//
//  Created by Евгений on 25.07.2026.
//

#include "bridge.h"

#include "byte_stacker_in.h"
#include "parser.h"
#include "settings.h"


int run_stacker_in(void) {
  std::map<unsigned int, boost::asio::ip::tcp::endpoint> local_points;
  std::vector<boost::asio::ip::udp::endpoint> trunk_points;
  Settings cfg;
  unsigned int id;
  boost::asio::ip::tcp::endpoint point;

  ParseTrunkPoint("72.56.40.138:40041", trunk_points);
    
  ParsePoint("2=127.0.0.1:3130", id, point);
  
  local_points[id] = point;
    
  RunClient(local_points, trunk_points, cfg);
  return 1;
}


void stop_stacker_in(void) {
  
}

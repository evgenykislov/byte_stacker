#include "outlink.h"

#include <iostream>


IOutLink* CreateOutLink(PointID point) {
  std::cout << "Debug: create link to point " << point << std::endl;
  return nullptr;
}

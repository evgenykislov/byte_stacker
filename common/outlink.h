#ifndef OUTLINK_H
#define OUTLINK_H

#include <cstdint>
#include <string>

#include "../common/data.h"

struct AddressPortPoint {
  std::string Address;
  uint16_t Port;
};

// TODO Descr
IOutLink* CreateOutLink(PointID point);

#endif  // OUTLINK_H

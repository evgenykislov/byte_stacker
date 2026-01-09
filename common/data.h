#ifndef DATA_H
#define DATA_H

#include "uuid.h"


using PointID = unsigned int;
using ConnectID = uuids::uuid;


class IOutLink {
 public:
  virtual ~IOutLink() = 0;
};


#endif  // DATA_H

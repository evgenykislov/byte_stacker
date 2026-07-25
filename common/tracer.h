#ifndef TRACER_H
#define TRACER_H

#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

#include "uuid.h"


/*! Класс для сохранения трассировки пакетов и соединений */
class Tracer {
 public:
  Tracer();
  virtual ~Tracer() {}

  void CreateTrace(uuids::uuid id);
  void FinishTrace(uuids::uuid id);

 private:
  Tracer(const Tracer&) = delete;
  Tracer& operator=(const Tracer&) = delete;
  Tracer(Tracer&&) = delete;
  Tracer& operator=(Tracer&&) = delete;

  struct StreamInfo {
    std::ofstream file;
    std::chrono::steady_clock::time_point creation;
  };

  std::filesystem::path base_;
  std::map<uuids::uuid, StreamInfo> storage_;
  std::mutex trace_lock_;
};

#endif  // TRACER_H

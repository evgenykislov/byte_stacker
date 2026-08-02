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
  Tracer(std::filesystem::path storagepath, std::filesystem::path successpath);
  virtual ~Tracer() {}

  void CreateTrace(uuids::uuid id);
  void FinishTrace(uuids::uuid id);

  void Message(uuids::uuid id, const std::string& msg);

  //! Выдать общее сообщение, не привязанное к соединению
  void CommonMessage(const std::string& msg);

 private:
  Tracer() = delete;
  Tracer(const Tracer&) = delete;
  Tracer& operator=(const Tracer&) = delete;
  Tracer(Tracer&&) = delete;
  Tracer& operator=(Tracer&&) = delete;

  struct StreamInfo {
    std::ofstream file;
    std::chrono::steady_clock::time_point creation;
    std::filesystem::path path;
    std::filesystem::path success_path;
  };

  std::filesystem::path storage_path_;
  std::filesystem::path success_path_;
  std::map<uuids::uuid, StreamInfo> storage_;
  std::mutex trace_lock_;
};

#endif  // TRACER_H

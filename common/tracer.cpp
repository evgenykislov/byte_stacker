#include "tracer.h"
#include "tracer.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <string>


/*! Выдать метку текущего локального (не-UTC) времени как строку
\param precise признак выдачи времени с миллисекундами
\return текстовая метка времени */
static std::string timemark() {
  // Get the current time point
  auto now = std::chrono::system_clock::now();

  // Get the milliseconds part of the current second
  // (remainder after division into seconds)
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;

  // Convert to std::time_t and then to std::tm (broken time)
  std::time_t timer = std::chrono::system_clock::to_time_t(now);
  std::tm bt = *std::localtime(&timer);

  // Use std::ostringstream to format the time string
  std::ostringstream oss;
  oss << std::put_time(&bt, "%H:%M:%S");  // Format: HH:MM:SS
  oss << '.' << std::setfill('0') << std::setw(3)
      << ms.count();  // Append milliseconds with leading zeros

  return oss.str();
}


Tracer::Tracer(
    std::filesystem::path storagepath, std::filesystem::path successpath)
    : storage_path_(storagepath), success_path_(successpath) {
  assert(!storage_path_.empty());
}

void Tracer::CreateTrace(uuids::uuid id) {
  std::lock_guard lk(trace_lock_);
  if (storage_.find(id) != storage_.end()) {
    // Ошибка логики. Уже всё отслеживается
    std::cerr << "ERROR!: trace has existed yet!" << std::endl;
    return;
  }

  storage_[id].creation = std::chrono::steady_clock::now();
  auto it = storage_.find(id);
  assert(it != storage_.end());
  std::string name = uuids::to_string(id);
  it->second.path = storage_path_ / name;
  if (!success_path_.empty()) {
    it->second.success_path = success_path_ / name;
  }
  it->second.file.open(it->second.path, std::ios_base::trunc);
  if (!it->second.file) {
    // Ошибка создания файла
    std::cerr << "ERROR!: Can't create trace file " << it->second.path
              << std::endl;
    storage_.erase(it);
    return;
  }

  it->second.file << "Создание соединения: " << timemark() << std::endl;
}

void Tracer::FinishTrace(uuids::uuid id) {
  std::lock_guard lk(trace_lock_);
  auto it = storage_.find(id);
  if (it == storage_.end()) {
    // Ошибка логики. Уже всё отслеживается
    std::cerr << "ERROR!: there isn't trace " << uuids::to_string(id)
              << " for finish." << std::endl;
    return;
  }

  it->second.file << "--------- Завершение соединения: " << timemark()
                  << std::endl;

  if (!it->second.success_path.empty()) {
    std::filesystem::rename(it->second.path, it->second.success_path);
  }

  storage_.erase(it);
}


void Tracer::Message(uuids::uuid id, const std::string& msg) {
  std::lock_guard lk(trace_lock_);
  auto it = storage_.find(id);
  if (it == storage_.end()) {
    // Ошибка логики. Уже всё отслеживается
    std::cerr << "ERROR!: there isn't trace " << uuids::to_string(id)
              << ". Message: " << msg << std::endl;
    return;
  }

  auto curt = std::chrono::steady_clock::now();
  auto mcs = std::chrono::duration_cast<std::chrono::microseconds>(
      curt - it->second.creation)
                 .count();
  auto sec = mcs / 1000000L;
  auto part = mcs % 1000000L;
  it->second.file << std::setfill('0') << std::setw(4) << sec << "."
                  << std::setfill('0') << std::setw(6) << part << ": " << msg
                  << std::endl;
}

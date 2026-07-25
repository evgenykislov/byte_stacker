#include "tracer.h"

#include <cassert>
#include <iostream>

#include "trace.h"


Tracer::Tracer(): base_("./") {}

void Tracer::CreateTrace(uuids::uuid id) {
  std::lock_guard lk(trace_lock_);
  if (storage_.find(id) != storage_.end()) {
    // Ошибка логики. Уже всё отслеживается
    std::cerr << "ERROR!: trace has existed yet!" << std::endl;
    return;
  }

  auto p = base_ / uuids::to_string(id);
  storage_[id].creation = std::chrono::steady_clock::now();
  auto it = storage_.find(id);
  assert(it != storage_.end());
  it->second.file.open(p, std::ios_base::trunc);
  if (!it->second.file) {
    // Ошибка создания файла
    std::cerr << "ERROR!: Can't create trace file " << p << std::endl;
    storage_.erase(it);
    return;
  }

  it->second.file << "Создание соединения: " << timemark(true) << std::endl;
}

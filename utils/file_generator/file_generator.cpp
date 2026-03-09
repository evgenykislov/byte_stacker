#include <cstddef>
#include <fstream>
#include <iostream>
#include <string>

const std::string kFilePrefix = "--file=";
const std::string kSizePrefix = "--size=";

void PrintHelp() {
  std::cout << "File Generator utility. Evgeny Kislov, 2026" << std::endl;
  std::cout << "Usage:" << std::endl;
  std::cout << "  file_generator --file=<filename> --size=<filesize>"
            << std::endl;
}


bool CheckPrefix(
    const std::string prefix, std::string arg, std::string& value) {
  if (arg.substr(0, prefix.size()) == prefix) {
    value = arg.substr(prefix.size());
    return true;
  }
  return false;
}


int main(int argc, char** argv) {
  const size_t kArraySize = 4096;
  char ar[kArraySize];

  if (argc == 1) {
    PrintHelp();
    return 0;
  }

  bool hasfname = false;
  std::string fname;
  bool hasfsize = false;
  size_t fsize;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    std::string v;
    if (CheckPrefix(kFilePrefix, a, v)) {
      fname = v;
      hasfname = true;
    }
    if (CheckPrefix(kSizePrefix, a, v)) {
      size_t sv;
      if (std::sscanf(v.c_str(), "%zu", &sv)) {
        fsize = sv;
        hasfsize = true;
      }
    }
  }

  if (!hasfname || !hasfsize) {
    PrintHelp();
    return 1;
  }

  for (size_t i = 0; i < kArraySize; ++i) {
    ar[i] = char(i & 0xff);
  }

  std::ofstream f(fname, std::ios_base::binary | std::ios_base::trunc);
  if (!f) {
    std::cerr << "Can't create file for writing" << std::endl;
    return 1;
  }

  size_t wr = fsize;  // Сколько осталось записать
  while (wr > 0) {
    auto ws = std::min(kArraySize, wr);
    f.write(ar, ws);
    if (!f) {
      std::cerr << "Error of writing file" << std::endl;
      return 1;
    }
    wr -= ws;
  }

  f.close();
  return 0;
}

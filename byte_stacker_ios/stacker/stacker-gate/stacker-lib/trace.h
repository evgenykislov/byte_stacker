#ifndef TRACE_H
#define TRACE_H

#include <chrono>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <string>


/*! Выдать метку текущего локального (не-UTC) времени как строку
\param precise признак выдачи времени с миллисекундами
\return текстовая метка времени */
inline std::string timemark(bool precise = true) {
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
  if (precise) {
    oss << '.' << std::setfill('0') << std::setw(3)
        << ms.count();  // Append milliseconds with leading zeros
  }

  return oss.str();
}


// TODO Descr
template <typename... Types>
void trlog(const char* format, Types... args) {
  std::printf("%s: TRACE: ", timemark(true).c_str());
  std::printf(format, args...);
  std::cout.flush();
}


// TODO Descr
template <typename... Types>
void tout(const char* format, Types... args) {
  std::printf("%s", timemark(false).c_str());
  std::printf(format, args...);
  std::cout.flush();
}


#endif  // TRACE_H

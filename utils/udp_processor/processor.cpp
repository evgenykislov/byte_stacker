#include "processor.h"

#include <utility>

#include "processor_skip.h"


ProcessorPtr CreateProcessor(
    std::shared_ptr<Processor> next, std::string prefix, std::string value) {
  if (prefix == "--skip=") {
    return std::make_shared<ProcessorSkip>(next, value);
  }

  return ProcessorPtr();
}

#include "processor.h"

#include <utility>

#include "processor_delay.h"
#include "processor_skip.h"


ProcessorPtr CreateProcessor(boost::asio::io_context& ctx,
    std::shared_ptr<Processor> next, std::string prefix, std::string value) {
  if (prefix == "--skip=") {
    return std::make_shared<ProcessorSkip>(next, value);
  } else if (prefix == "--delay=") {
    return std::make_shared<ProcessorDelay>(ctx, next, value);
  }


  return ProcessorPtr();
}

#include "selfdrive/ui/bluepilot/bp_logging.h"
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <chrono>
#include <ctime>

namespace {
// Null stream that discards all output
class NullStream : public std::ostream {
  class NullBuffer : public std::streambuf {
  public:
    int overflow(int c) override { return c; }
  } null_buffer;
public:
  NullStream() : std::ostream(&null_buffer) {}
};

static NullStream null_stream;

// Get current timestamp string
std::string getTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()) % 1000;

  std::stringstream ss;
  ss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
  ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
  return ss.str();
}

// Helper to output prefix with timestamp
void outputPrefix(std::ostream& stream, const std::string& prefix, const std::string& color = "") {
  if (!color.empty()) {
    stream << color;
  }
  stream << "[" << getTimestamp() << "] " << prefix << " ";
  if (!color.empty()) {
    stream << "\033[0m";  // Reset color
  }
}

}  // namespace

// BPLog implementation
bool BPLog::isEnvVarSet(const std::string& var_name) {
  const char* value = std::getenv(var_name.c_str());
  return value != nullptr && std::strlen(value) > 0;
}

std::ostream& BPLog::bpInfo() {
  outputPrefix(std::cout, "[INFO]", "\033[37m");   // White
  return std::cout;
}

std::ostream& BPLog::bpDebug() {
  outputPrefix(std::cout, "[DEBUG]", "\033[36m");  // Cyan
  return std::cout;
}

std::ostream& BPLog::bpWarn() {
  outputPrefix(std::cerr, "[WARN]", "\033[33m");   // Yellow
  return std::cerr;
}

std::ostream& BPLog::bpError() {
  outputPrefix(std::cerr, "[ERROR]", "\033[31m");  // Red
  return std::cerr;
}

std::ostream& BPLog::bpDebugVideo() {
  if (isEnvVarSet("BP_DEBUG_VIDEO")) {
    outputPrefix(std::cout, "[VIDEO]", "\033[35m");  // Magenta
    return std::cout;
  }
  return null_stream;
}

std::ostream& BPLog::bpDebugRoutes() {
  if (isEnvVarSet("BP_DEBUG_ROUTES")) {
    outputPrefix(std::cout, "[ROUTES]", "\033[34m");  // Blue
    return std::cout;
  }
  return null_stream;
}

std::ostream& BPLog::bpDebugGeneral() {
  if (isEnvVarSet("BP_DEBUG")) {
    outputPrefix(std::cout, "[DEBUG]", "\033[32m");  // Green
    return std::cout;
  }
  return null_stream;
}

std::ostream& BPLog::bpDebugOnroadDebug() {
  if (isEnvVarSet("BP_DEBUG_ONROAD_DEBUG")) {
    outputPrefix(std::cout, "[ONROAD]", "\033[32m");  // Green
    return std::cout;
  }
  return null_stream;
}

#pragma once
// CrashHooks.h - Minimal in-process crash dumper for BluePilot Qt UI
// - Installs signal handlers to dump backtraces for *all* threads to a log file
// - Triggered by SIGUSR1 from manager watchdog before process kill
//
// Usage (in your UI main.cpp):
//   #include "CrashHooks.h"
//   int main(int argc, char** argv) {
//     QApplication app(argc, argv);
//     CrashHooks::install("/data/crashlogs/ui_crash.log");
//     return app.exec();
//   }
//
// Manager watchdog sends SIGUSR1 before kill to force a dump.
//
// Build deps: glibc (execinfo.h), pthread, QtCore.
// Link with: -ldl -rdynamic (for richer symbols), and make sure binaries have symbols.
//
// NOTE: backtrace() is not guaranteed async-signal-safe but is widely used in practice.
//       For production-grade capture consider Crashpad/Breakpad.

#include <atomic>
#include <string>

namespace CrashHooks {

// Install handlers. logPath may be on /data/crashlogs.
void install(const std::string& logPath);

// Manually trigger a dump of all thread stacks.
void dumpAllThreads(const char* reason);

// Enable/disable extra logging to stdout/stderr
void setVerbose(bool v);

} // namespace CrashHooks

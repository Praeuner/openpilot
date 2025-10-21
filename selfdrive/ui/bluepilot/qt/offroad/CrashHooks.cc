#include "CrashHooks.h"
#include <QtCore/QCoreApplication>

#include <dirent.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif

namespace {

std::atomic<bool> g_verbose {false};
int g_log_fd = -1;
std::string g_log_path;

pid_t gettid_() {
#ifdef __linux__
  return (pid_t)syscall(SYS_gettid);
#else
  return getpid();  // On macOS, fallback to process ID
#endif
}

void safe_write(const char* s) {
  if (g_log_fd >= 0 && s) { (void)::write(g_log_fd, s, strlen(s)); (void)::fsync(g_log_fd); }
  if (s) (void)::write(STDERR_FILENO, s, strlen(s));
}

void safe_printf(const char* fmt, ...) {
  char buf[2048];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  safe_write(buf);
}

void open_log() {
  if (g_log_fd >= 0) return;
  if (!g_log_path.empty()) {
    char path_ts[1024];
    time_t t = time(nullptr);
    struct tm tmv; localtime_r(&t, &tmv);
    snprintf(path_ts, sizeof(path_ts), "%s.%04d%02d%02d-%02d%02d%02d",
             g_log_path.c_str(), tmv.tm_year+1900, tmv.tm_mon+1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    int fd = ::open(path_ts, O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd >= 0) { g_log_fd = fd; return; }
  }
  g_log_fd = -1;
}

void dump_one_thread(const pid_t tid) {
  if (tid <= 0) return;
  safe_printf("\n=== Backtrace for tid %d ===\n", tid);
  void* bt[128];
  int n = ::backtrace(bt, 128);
  if (n > 0) {
    ::backtrace_symbols_fd(bt, n, (g_log_fd >= 0 ? g_log_fd : STDERR_FILENO));
    (void)::write((g_log_fd >= 0 ? g_log_fd : STDERR_FILENO), "\n", 1);
  }
}


void enumerate_threads_and_signal(int signo) {
  DIR* d = ::opendir("/proc/self/task");
  if (!d) return;
  struct dirent* de;
  while ((de = ::readdir(d)) != nullptr) {
    if (de->d_name[0] == '.') continue;
    pid_t tid = (pid_t)::atoi(de->d_name);
    if (tid <= 0) continue;
#ifdef __linux__
    (void)syscall(SYS_tgkill, getpid(), tid, signo);
#else
    (void)kill(tid, signo);  // On macOS, use regular kill
#endif
  }
  ::closedir(d);
}

void dump_all_threads_internal(const char* reason) {
  time_t now = time(nullptr);
  safe_printf("\n[CrashHooks] ===== Thread dump @ %ld (%s) =====\n",
              (long)now, reason ? reason : "no-reason");
  enumerate_threads_and_signal(SIGUSR2);
  dump_one_thread(gettid_());
  safe_write("[CrashHooks] ===== End dump =====\n");
}

void crash_signal_handler(int sig, siginfo_t*, void*) {
  const char* sname = strsignal(sig);
  safe_printf("\n[CrashHooks] Caught %s (%d), tid=%d\n", sname ? sname : "signal", sig, gettid_());
  dump_all_threads_internal("crash");
  signal(sig, SIG_DFL);
  raise(sig);
}

void usr1_handler(int, siginfo_t*, void*) { dump_all_threads_internal("SIGUSR1"); }

void install_handlers() {
  struct sigaction sa {}; sa.sa_sigaction = crash_signal_handler;
  sigemptyset(&sa.sa_mask); sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
  int crash_sigs[] = { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS, SIGQUIT };
  for (int s : crash_sigs) sigaction(s, &sa, nullptr);

  struct sigaction sa2 {}; sa2.sa_sigaction = usr1_handler;
  sigemptyset(&sa2.sa_mask); sa2.sa_flags = SA_SIGINFO | SA_RESTART;
  sigaction(SIGUSR1, &sa2, nullptr);
}


} // namespace

namespace CrashHooks {

void setVerbose(bool v) { g_verbose.store(v); }

void dumpAllThreads(const char* reason) { dump_all_threads_internal(reason); }

void install(const std::string& logPath) {
  g_log_path = logPath;
  open_log();
  install_handlers();

  safe_printf("[CrashHooks] installed. log=%s\n", g_log_path.c_str());
}

} // namespace CrashHooks

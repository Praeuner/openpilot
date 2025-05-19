#include <sys/resource.h>

#include <QApplication>
#include <QTranslator>

#include "system/hardware/hw.h"
#include "selfdrive/ui/qt/util.h"
#include "selfdrive/ui/qt/window.h"

#ifdef SUNNYPILOT
#include "selfdrive/ui/sunnypilot/qt/window.h"
#define MainWindow MainWindowSP
#else
#include "selfdrive/ui/qt/qt_window.h"
#endif

#ifdef SENTRY_ENABLED
#include "third_party/sentry/include/sentry.h"
#endif

int main(int argc, char *argv[]) {
  setpriority(PRIO_PROCESS, 0, -20);

  qInstallMessageHandler(swagLogMessageHandler);
  initApp(argc, argv);

  QTranslator translator;
  QString translation_file = QString::fromStdString(Params().get("LanguageSetting"));
  if (!translator.load(QString(":/%1").arg(translation_file)) && translation_file.length()) {
    qCritical() << "Failed to load translation file:" << translation_file;
  }

  QApplication a(argc, argv);
  a.installTranslator(&translator);

#ifdef SENTRY_ENABLED
  // Initialize Sentry on supported platforms (Ubuntu larch64, macOS arm64)
  sentry_options_t *options = sentry_options_new();
  sentry_options_set_dsn(options, "https://6d2e9c1eb33ebc10e339bbc0b3945c4d@o4509128983117824.ingest.us.sentry.io/4509321931915264");
  sentry_options_set_database_path(options, ".sentry-native");
  sentry_options_set_release(options, "bluepilot-qt@0.9.0");
  sentry_options_set_traces_sample_rate(options, 1.0);
  #if defined(__APPLE__)
    sentry_options_set_handler_path(options, "../../third_party/sentry/macos_arm64/crashpad_handler");
  #elif defined(__linux__)
    sentry_options_set_handler_path(options, "../../third_party/sentry/ubuntu_larch64/crashpad_handler");
  #endif
  // sentry_options_set_debug(options, 1);
  sentry_init(options);

  // Test the integration with a sample event
  sentry_capture_event(sentry_value_new_message_event(SENTRY_LEVEL_INFO, "custom", "It works!"));

  // Ensure Sentry flushes events on exit
  auto sentryClose = qScopeGuard([] { sentry_close(); });
#endif

  MainWindow w;
  setMainWindow(&w);
  a.installEventFilter(&w);
  int result = a.exec();

  return result;
}

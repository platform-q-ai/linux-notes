#include "composition_root.hpp"
#include "presentation/qt/qml_support.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtGlobal>

#include <iostream>
#include <memory>

int main(int argc, char* argv[]) {
  qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  QGuiApplication app(argc, argv);
  QGuiApplication::setApplicationName(QStringLiteral("Linux Notes"));
  QGuiApplication::setOrganizationName(QStringLiteral("platform-q-ai"));
  QGuiApplication::setApplicationVersion(QStringLiteral("0.1.0"));

  const auto paths = notes::adapters::system::resolve_app_paths("linux-notes");

  std::unique_ptr<notes::CompositionRoot> root;
  try {
    root = std::make_unique<notes::CompositionRoot>(paths);
  } catch (const std::exception& ex) {
    std::cerr << "Failed to start: " << ex.what() << "\n";
    return 1;
  }

  // aboutToQuit cannot veto exit; AppExitGate/requestClose is the real gate.
  // This handler only drains the dispatcher after an authorized close, or
  // makes a best-effort flush if something forced the event loop to stop.
  QObject::connect(&app, &QGuiApplication::aboutToQuit, [&] {
    if (!root) {
      return;
    }
    if (auto* gate = root->exit_gate()) {
      if (!gate->quitAuthorized()) {
        (void)gate->flushBestEffort();
      }
      gate->completeShutdown();
      return;
    }
    root->flush_and_shutdown();
  });

  QQmlApplicationEngine engine;
  root->register_qml(engine);

  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

  engine.load(notes::presentation::mainQmlUrl());
  if (engine.rootObjects().isEmpty()) {
    const QString qml_dir = QStringLiteral(NOTES_QML_DIR);
    engine.addImportPath(qml_dir);
    engine.load(QUrl::fromLocalFile(qml_dir + QStringLiteral("/Main.qml")));
  }
  if (engine.rootObjects().isEmpty()) {
    std::cerr << "Failed to load QML UI\n";
    return 2;
  }
  notes::presentation::bindRootViewModels(
      engine.rootObjects().constFirst(), root->folders(), root->notes(),
      root->editor(), root->exit_gate());
  return app.exec();
}

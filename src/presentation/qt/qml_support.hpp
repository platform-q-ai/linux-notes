#pragma once

// Helpers for composition_root: register presentation types and resolve Main.qml.

#include "presentation/qt/app_exit_gate.hpp"
#include "presentation/qt/models/folder_tree_model.hpp"
#include "presentation/qt/models/note_list_model.hpp"
#include "presentation/qt/view_models/editor_view_model.hpp"
#include "presentation/qt/view_models/folder_tree_view_model.hpp"
#include "presentation/qt/view_models/note_list_view_model.hpp"

#include <QQmlContext>
#include <QQmlEngine>
#include <QUrl>
#include <QVariant>

namespace notes::presentation {

inline void registerQmlTypes() {
  qmlRegisterUncreatableType<FolderTreeModel>(
      "Notes", 1, 0, "FolderTreeModel",
      QStringLiteral("Obtained from FolderTreeViewModel"));
  qmlRegisterUncreatableType<NoteListModel>(
      "Notes", 1, 0, "NoteListModel",
      QStringLiteral("Obtained from NoteListViewModel"));
  qmlRegisterUncreatableType<FolderTreeViewModel>(
      "Notes", 1, 0, "FolderTreeViewModel",
      QStringLiteral("Provided by composition root"));
  qmlRegisterUncreatableType<NoteListViewModel>(
      "Notes", 1, 0, "NoteListViewModel",
      QStringLiteral("Provided by composition root"));
  qmlRegisterUncreatableType<EditorViewModel>(
      "Notes", 1, 0, "EditorViewModel",
      QStringLiteral("Provided by composition root"));
  qmlRegisterUncreatableType<AppExitGate>(
      "Notes", 1, 0, "AppExitGate",
      QStringLiteral("Provided by composition root"));
}

inline void exposeToQml(QQmlEngine& engine, FolderTreeViewModel* folders,
                        NoteListViewModel* notes, EditorViewModel* editor,
                        AppExitGate* exit_gate = nullptr) {
  auto* ctx = engine.rootContext();
  ctx->setContextProperty(QStringLiteral("folderVm"), folders);
  ctx->setContextProperty(QStringLiteral("noteListVm"), notes);
  ctx->setContextProperty(QStringLiteral("editorVm"), editor);
  ctx->setContextProperty(QStringLiteral("exitGate"), exit_gate);
  engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
}

inline QUrl mainQmlUrl() {
  // Embedded via presentation_qml.qrc prefix /notes
  return QUrl(QStringLiteral("qrc:/notes/Main.qml"));
}

// After engine.load(mainQmlUrl()), call to push VMs onto the ApplicationWindow.
inline void bindRootViewModels(QObject* root, FolderTreeViewModel* folders,
                               NoteListViewModel* notes,
                               EditorViewModel* editor,
                               AppExitGate* exit_gate = nullptr) {
  if (!root) {
    return;
  }
  root->setProperty("folderVm", QVariant::fromValue(folders));
  root->setProperty("noteListVm", QVariant::fromValue(notes));
  root->setProperty("editorVm", QVariant::fromValue(editor));
  root->setProperty("exitGate", QVariant::fromValue(exit_gate));
}

}  // namespace notes::presentation

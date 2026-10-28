#!/usr/bin/env python3
"""Apply the opt-in native-session proof to an exact PDF4QT checkout.

No renderer changes. Never copy V1/private source into this public experiment.
"""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess

PIN = '67b6b9903cb4e318803342f2d428f02e1bb124e3'


def prepare(root, proof=True):
    root = root.resolve(strict=True)
    actual = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != PIN:
        raise ValueError('Wrong PDF4QT revision')
    planned = {}

    def replace(path, old, new):
        target = root / path
        if path not in planned:
            baseline = subprocess.check_output(['git', '-C', str(root), 'show', f'{PIN}:{path}'])
            if target.read_bytes().replace(b'\r\n', b'\n') != baseline.replace(b'\r\n', b'\n'):
                raise ValueError('Unexpected upstream edits: ' + path)
            planned[path] = target.read_text(encoding='utf-8')
        text = planned[path]
        if text.count(old) != 1:
            raise ValueError('Patch anchor is not unique: ' + path)
        planned[path] = text.replace(old, new, 1)

    # The DLL root is a factory prototype; never bind it to a document.
    # Q_INVOKABLE adds constructor metadata, without changing PDFPlugin's ABI.
    headers = sorted(root.glob('Pdf4QtEditorPlugins/*/*plugin.h'))
    if len(headers) != 9:
        raise ValueError('Unexpected bundled plugin inventory')
    for header in headers:
        text = header.read_text(encoding='utf-8')
        cls = re.search(r'class (\w+) : public pdf::PDFPlugin', text).group(1)
        replace(header.relative_to(root).as_posix(), f'    {cls}();', f'    Q_INVOKABLE {cls}();')

    controller = 'Pdf4QtLibGui/pdfprogramcontroller.cpp'
    replace(controller,
        '            pdf::PDFPlugin* plugin = qobject_cast<pdf::PDFPlugin*>(loader.instance());',
        '''            QObject* root = loader.instance();
            QObject* instance = root;
            if (qApp->property("lunaNativeSessions").toBool())
            {
                // Fail closed: an old DLL must never fall back to shared state.
                instance = root ? root->metaObject()->newInstance() : nullptr;
                if (!qobject_cast<pdf::PDFPlugin*>(instance) || instance == root)
                    qFatal("LUNA: plugin lacks independent session factory");
                instance->setParent(this);
                instance->setProperty("lunaSessionPlugin", true);
            }
            pdf::PDFPlugin* plugin = qobject_cast<pdf::PDFPlugin*>(instance);''')
    replace(controller,
        '    return !(m_futureWatcher && m_futureWatcher->isRunning()) || !m_isBusy;',
        '    return !(m_futureWatcher && m_futureWatcher->isRunning()) && !m_isBusy;')
    replace('Pdf4QtLibGui/pdfprogramcontroller.h',
        '    bool askForSaveDocumentBeforeClose();',
        '    bool askForSaveDocumentBeforeClose();\n    bool hasUnsavedChanges() const;\n    bool trySave();')
    replace('Pdf4QtLibGui/pdfprogramcontroller.h',
        '    void saveDocument(const QString& fileName);',
        '    bool saveDocument(const QString& fileName);')
    replace(controller, 'void PDFProgramController::saveDocument(const QString& fileName)',
        'bool PDFProgramController::saveDocument(const QString& fileName)')
    replace(controller,
        '    updateFileWatcher();\n}\n\nvoid PDFProgramController::savePageLayoutPerDocument()',
        '    updateFileWatcher();\n    return bool(result);\n}\n\nvoid PDFProgramController::savePageLayoutPerDocument()')
    replace(controller, '''void PDFProgramController::performSave()
{
    if (!askForWriteUnwrittenChanges())
    {
        return;
    }

    saveDocument(m_fileInfo.originalFileName);
}''', '''void PDFProgramController::performSave()
{
    trySave();
}

bool PDFProgramController::trySave()
{
    if (!askForWriteUnwrittenChanges()) return false;
    return saveDocument(m_fileInfo.originalFileName);
}''')
    replace(controller,
        'bool PDFProgramController::askForSaveDocumentBeforeClose()\n{',
        '''bool PDFProgramController::hasUnsavedChanges() const
{
    if (m_undoRedoManager && !m_undoRedoManager->isCurrentSaved()) return true;
    for (const auto& plugin : m_loadedPlugins)
        if (plugin.second->hasUnwrittenChanges()) return true;
    return false;
}

bool PDFProgramController::askForSaveDocumentBeforeClose()
{''')
    # LUNA's registry validates/reserves Save As destinations without changing
    # the native writer. With no listeners (the proof/legacy path), save is unchanged.
    replace('Pdf4QtLibGui/pdfprogramcontroller.h', 'signals:',
        'signals:\n    void lunaOpenRequested(const QString& fileName, bool* handled);\n    void lunaDocumentLoadFinished(bool success);\n    void lunaSavePathRequested(const QString& fileName, bool* allowed);\n    void lunaSavePathFinished(const QString& fileName, bool success);')
    replace(controller, 'bool PDFProgramController::saveDocument(const QString& fileName)\n{',
        'bool PDFProgramController::saveDocument(const QString& fileName)\n{\n    bool allowed = true;\n    Q_EMIT lunaSavePathRequested(fileName, &allowed);\n    if (!allowed) return false;')
    replace(controller, '    return bool(result);\n}\n\nvoid PDFProgramController::savePageLayoutPerDocument()',
        '    Q_EMIT lunaSavePathFinished(fileName, bool(result));\n    return bool(result);\n}\n\nvoid PDFProgramController::savePageLayoutPerDocument()')
    replace(controller, 'void PDFProgramController::openDocument(const QString& fileName)\n{',
        'void PDFProgramController::openDocument(const QString& fileName)\n{\n    bool handled = false;\n    Q_EMIT lunaOpenRequested(fileName, &handled);\n    if (handled) return;')
    replace(controller, '    updateActionsAvailability();\n}\n\nvoid PDFProgramController::onDocumentModified',
        '    updateActionsAvailability();\n    Q_EMIT lunaDocumentLoadFinished(bool(m_pdfDocument));\n}\n\nvoid PDFProgramController::onDocumentModified')
    # Limit replacements to the close prompt, not other dirty checks.
    old = '''    if (m_undoRedoManager && !m_undoRedoManager->isCurrentSaved())
    {
        QString title = tr("Save Document");'''
    replace(controller, old, '''    if (hasUnsavedChanges())
    {
        QString title = tr("Save Document");''')
    replace(controller, '                performSave();\n                return m_undoRedoManager->isCurrentSaved();',
            '                return trySave();')
    replace('Pdf4QtLibGui/pdfeditormainwindow.cpp',
        '        if (!m_programController->isFactorySettingsBeingRestored())',
        '        if (isWindow() && !m_programController->isFactorySettingsBeingRestored())')
    # Qt::Popup is a top-level window: move() takes global screen coordinates.
    replace('Pdf4QtLibWidgets/sources/pdfwidgettool.cpp',
        '        QPoint topRightParent = m_parentDialog->mapFromGlobal(topRight);\n', '')
    replace('Pdf4QtLibWidgets/sources/pdfwidgettool.cpp',
        '        m_dialog->move(topRightParent - QPoint(m_dialog->width() * 1.1, 0));',
        '''        const QRect available = widget->screen()->availableGeometry().adjusted(2, 2, -2, -2);
        QPoint position = topRight - QPoint(qRound(m_dialog->width() * 1.1), 0);
        position.setX(qBound(available.left(), position.x(), qMax(available.left(), available.right() - m_dialog->frameGeometry().width() + 1)));
        position.setY(qBound(available.top(), position.y(), qMax(available.top(), available.bottom() - m_dialog->frameGeometry().height() + 1)));
        m_dialog->move(position);''')
    replace('Pdf4QtLibWidgets/sources/pdfwidgettool.cpp', '#include <QApplication>', '#include <QApplication>\n#include <QScreen>')
    cmake = root / 'Pdf4QtEditor/CMakeLists.txt'
    addition = '''
# Opt-in architecture proof. Not installed or included in LUNA portable.
add_executable(LunaNativeTabs luna_native_tabs.cpp app.qrc)
target_link_libraries(LunaNativeTabs PRIVATE Pdf4QtLibCore Pdf4QtLibWidgets Pdf4QtLibGui
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport Qt6::Network)
if(MSVC)
    target_compile_options(LunaNativeTabs PRIVATE /utf-8)
endif()
set_target_properties(LunaNativeTabs PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR})
'''
    destination = root / 'Pdf4QtEditor/luna_native_tabs.cpp'
    if destination.exists() or 'LunaNativeTabs' in cmake.read_text(encoding='utf-8'):
        raise ValueError('Native proof already applied')
    for path, text in planned.items():
        (root / path).write_bytes(text.replace('\n', '\r\n').encode('utf-8'))
    if proof:
        destination.write_bytes(Path(__file__).with_name('main.cpp').read_bytes().replace(b'\r\n', b'\n').replace(b'\n', b'\r\n'))
        with cmake.open('ab') as output:
            output.write(addition.replace('\n', '\r\n').encode('utf-8'))
    print(f'[PASS] Native-session overlay applied to {len(planned)} pinned files; runtime NOT tested')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('root', type=Path)
    parser.add_argument('--backend-only', action='store_true')
    args = parser.parse_args()
    prepare(args.root, proof=not args.backend_only)

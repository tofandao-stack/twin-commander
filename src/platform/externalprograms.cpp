#include "platform/externalprograms.h"
#include "filesystem/nativefs.h"
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

void ExternalPrograms::launch(const QString &program, const QString &parameters, const QString &directory) {
    const auto executable =
        QFileInfo(program).isAbsolute() ? program : QStandardPaths::findExecutable(program);
    if (executable.isEmpty() || !QFileInfo(executable).isExecutable())
        throw NativeFs::Error("External button program is missing or not executable");
    if (!directory.isEmpty() && !QFileInfo(directory).isDir())
        throw NativeFs::Error("Start directory does not exist");
    if (!QProcess::startDetached(executable, QProcess::splitCommand(parameters), directory))
        throw NativeFs::Error("External button program could not start");
}

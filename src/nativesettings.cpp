#include "nativesettings.h"
#include "directorymodel.h"
#include "extendedtools.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

namespace NativeSettings {
bool editRecords(QWidget *parent, const QString &title, const QString &key, const QStringList &columns,
                 const QString &fixedKind) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.resize(820, 440);
    dialog.setObjectName("recordEditor");
    auto *layout = new QVBoxLayout(&dialog);
    if (key == "nativePlugins") {
        auto *help = new QLabel(
            "Native executables only. content returns text; filesystem returns a local mount path; viewer "
            "opens {path}; packer writes {archive}. Windows TC DLLs cannot be loaded.",
            &dialog);
        help->setWordWrap(true);
        layout->addWidget(help);
    }
    auto *table = new QTableWidget(&dialog);
    table->setObjectName("records");
    table->setColumnCount(columns.size());
    table->setHorizontalHeaderLabels(columns);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(table);
    const auto records = QSettings().value(key).toList();
    const int kindColumn = columns.indexOf("kind");
    if (!fixedKind.isEmpty() && kindColumn >= 0)
        table->hideColumn(kindColumn);
    for (const auto &record : records) {
        if (!fixedKind.isEmpty() && record.toMap().value("kind") != fixedKind)
            continue;
        const int row = table->rowCount();
        table->insertRow(row);
        for (int col = 0; col < columns.size(); ++col)
            table->setItem(row, col, new QTableWidgetItem(record.toMap().value(columns[col]).toString()));
    }
    auto *row = new QHBoxLayout;
    for (const auto &label : {QString("Add"), QString("Delete"), QString("Copy")}) {
        auto *button = new QPushButton(label, &dialog);
        row->addWidget(button);
        QObject::connect(button, &QPushButton::clicked, &dialog, [table, label, fixedKind, kindColumn] {
            const int selected = table->currentRow();
            if (label == "Delete") {
                if (selected >= 0)
                    table->removeRow(selected);
                return;
            }
            const int added = table->rowCount();
            if (added >= 1000)
                return;
            table->insertRow(added);
            for (int col = 0; col < table->columnCount(); ++col)
                table->setItem(
                    added, col,
                    new QTableWidgetItem(label == "Copy" && selected >= 0 && table->item(selected, col)
                                             ? table->item(selected, col)->text()
                                             : QString()));
            if (!fixedKind.isEmpty() && kindColumn >= 0)
                table->item(added, kindColumn)->setText(fixedKind);
            table->setCurrentCell(added, 0);
        });
    }
    layout->addLayout(row);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    QVariantList result;
    for (int r = 0; r < table->rowCount(); ++r) {
        QVariantMap record;
        for (int c = 0; c < columns.size(); ++c)
            record.insert(columns[c], table->item(r, c) ? table->item(r, c)->text().trimmed() : QString());
        if (!record.value(columns.front()).toString().isEmpty())
            result.append(record);
    }
    if (!fixedKind.isEmpty()) {
        QVariantList merged;
        for (const auto &entry : records)
            if (entry.toMap().value("kind") != fixedKind)
                merged.append(entry);
        merged.append(result);
        result = merged;
    }
    QSettings().setValue(key, result);
    return true;
}
QVariantList plugins(const QString &kind) {
    QVariantList result;
    for (const auto &entry : QSettings().value("nativePlugins").toList())
        if (kind.isEmpty() || entry.toMap().value("kind") == kind)
            result.append(entry);
    return result;
}
void configurePlugins(QWidget *parent, const QString &kind) {
    const auto title = kind.isEmpty() ? "Native Linux extensions" : "Native Linux extensions — " + kind;
    editRecords(parent, title, "nativePlugins", {"name", "kind", "mask", "program", "arguments"}, kind);
}
static QStringList arguments(const QString &value, const std::filesystem::path &path) {
    if (nativePath(displayPath(path)) != path)
        throw std::runtime_error("Executable extensions require decodable paths");
    auto args = QProcess::splitCommand(value);
    for (auto &arg : args)
        arg.replace("{path}", displayPath(path));
    if (!value.contains("{path}"))
        args << displayPath(path);
    return args;
}
bool launchAssociated(const std::filesystem::path &path, const QString &kind) {
    auto entries = QSettings().value("associations").toList();
    entries.append(plugins(kind));
    for (const auto &entry : entries) {
        const auto record = entry.toMap();
        if ((record.value("kind").toString().isEmpty() ? QString("open") : record.value("kind").toString()) !=
            kind)
            continue;
        if (!QDir::match(record.value("mask", "*").toString(), displayPath(path.filename())))
            continue;
        const auto program = record.value("program").toString();
        if (program.isEmpty())
            continue;
        if (!QProcess::startDetached(program, arguments(record.value("arguments").toString(), path),
                                     displayPath(path.parent_path())))
            throw std::runtime_error("The configured application could not be launched");
        return true;
    }
    const auto program =
        QSettings().value(kind == "editor" ? "tools/IdConfed" : "tools/IdConfview").toString();
    if ((kind == "viewer" || kind == "editor") && !program.isEmpty()) {
        if (!QProcess::startDetached(program, {displayPath(path)}, displayPath(path.parent_path())))
            throw std::runtime_error("The configured application could not be launched");
        return true;
    }
    return false;
}
QString mountUrl(const QString &uri) {
    for (const auto &entry : plugins("filesystem")) {
        const auto plugin = entry.toMap();
        if (!QDir::match(plugin.value("mask", "*").toString(), uri))
            continue;
        auto args = QProcess::splitCommand(plugin.value("arguments").toString());
        bool inserted = false;
        for (auto &arg : args)
            if (arg.contains("{url}")) {
                arg.replace("{url}", uri);
                inserted = true;
            }
        if (!inserted)
            args << uri;
        QProcess process;
        process.start(plugin.value("program").toString(), args);
        if (!process.waitForStarted(2000))
            throw std::runtime_error("Filesystem extension could not start");
        QByteArray output;
        QByteArray errors;
        QElapsedTimer timer;
        timer.start();
        while (process.state() != QProcess::NotRunning) {
            process.waitForFinished(50);
            output += process.readAllStandardOutput();
            errors += process.readAllStandardError();
            if (timer.elapsed() > 30000 || output.size() + errors.size() > 65536) {
                process.kill();
                process.waitForFinished();
                throw std::runtime_error("Filesystem extension time/output limit exceeded");
            }
        }
        output += process.readAllStandardOutput();
        const auto directory = QString::fromUtf8(output).trimmed();
        if (process.exitCode() != 0 || process.exitStatus() != QProcess::NormalExit ||
            !QDir::isAbsolutePath(directory) || !QDir(directory).exists())
            throw std::runtime_error(
                "Filesystem extension must return an existing absolute local mount path");
        return directory;
    }
    return {};
}
QString field(const std::filesystem::path &path, const QString &name) {
    if (name == "comment")
        return ExtendedTools::comment(path);
    if (name == "parent")
        return displayPath(path.parent_path().filename());
    if (name == "size") {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        return error ? QString() : QString::number(size);
    }
    bool known = false;
    for (const auto &entry : plugins("content")) {
        const auto plugin = entry.toMap();
        if (plugin.value("name") != name)
            continue;
        known = true;
        if (!QDir::match(plugin.value("mask", "*").toString(), displayPath(path.filename())))
            continue;
        QProcess process;
        process.start(plugin.value("program").toString(),
                      arguments(plugin.value("arguments").toString(), path));
        if (!process.waitForStarted(2000))
            throw std::runtime_error("Content extension could not start");
        QByteArray output, error;
        QElapsedTimer timer;
        timer.start();
        while (process.state() != QProcess::NotRunning) {
            process.waitForFinished(50);
            output += process.readAllStandardOutput();
            error += process.readAllStandardError();
            if (output.size() + error.size() > 65536 || timer.elapsed() > 5000) {
                process.kill();
                process.waitForFinished();
                throw std::runtime_error("Content extension output/time limit exceeded");
            }
        }
        output += process.readAllStandardOutput();
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || output.size() > 65536)
            throw std::runtime_error("Content extension failed");
        return QString::fromUtf8(output).trimmed();
    }
    if (known)
        return {};
    throw std::runtime_error("Unknown native content field");
}
} // namespace NativeSettings

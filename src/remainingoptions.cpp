#include "classicui.h"
#include "mainwindow.h"
#include "nativesettings.h"
#include "preferences.h"
#include "productivitytools.h"
#include "toolwindows.h"
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QIntValidator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

void MainWindow::applyUserCommands() {
    const auto actions = findChildren<QAction *>();
    for (auto *action : actions)
        if (!action->objectName().isEmpty()) {
            if (!action->property("factoryShortcuts").isValid()) {
                QStringList shortcuts;
                for (const auto &key : action->shortcuts())
                    shortcuts << key.toString(QKeySequence::PortableText);
                action->setProperty("factoryShortcuts", shortcuts);
            }
            QList<QKeySequence> keys;
            for (const auto &key : action->property("factoryShortcuts").toStringList())
                keys << QKeySequence(key);
            action->setShortcuts(keys);
        }
    for (const auto &entry : QSettings().value("keyBindings").toList()) {
        const auto record = entry.toMap();
        auto *target = commands_.get(record.value("command").toString());
        const QKeySequence key(record.value("key").toString());
        if (!target || key.isEmpty())
            continue;
        for (auto *action : actions) {
            auto keys = action->shortcuts();
            if (keys.removeAll(key))
                action->setShortcuts(keys);
        }
        auto keys = target->shortcuts();
        keys << key;
        target->setShortcuts(keys);
    }
    const auto aliases = QSettings().value("commandAliases").toMap();
    for (auto it = aliases.begin(); it != aliases.end(); ++it)
        if (auto *action = commands_.get(it.value().toString()))
            commands_.addUserAlias(it.key(), action);
}
void MainWindow::configureRemainingOptions(QDialog *dialog) {
    const auto draft = [dialog](const QString &key, const QVariant &value) {
        auto data = dialog->property("appearanceDraft").toMap();
        data.insert(key, value);
        dialog->setProperty("appearanceDraft", data);
    };
    const auto enable = [dialog](const QString &name, const QString &tip = QString()) {
        auto *button = dialog->findChild<QAbstractButton *>(name);
        if (button) {
            button->setEnabled(true);
            button->setToolTip(tip);
        }
        return button;
    };
    const auto field = [dialog, draft](const QString &name, const QString &key,
                                       const QString &fallback = QString()) {
        auto *edit = dialog->findChild<QLineEdit *>(name);
        if (!edit)
            return edit;
        edit->setEnabled(true);
        edit->setToolTip({});
        edit->setText(QSettings().value(key, fallback).toString());
        QObject::connect(edit, &QLineEdit::textChanged, dialog,
                         [draft, key](const QString &text) { draft(key, text); });
        return edit;
    };
    const QMap<QString, QString> toolFields{{"z7_find", "z7_packer"},        {"id_zip_find1", "zip_packer"},
                                            {"id_zip_find2", "zip_extract"}, {"id_zip_find3", "arj_packer"},
                                            {"id_zip_find4", "lha_packer"},  {"id_zip_find5", "rar_packer"},
                                            {"id_zip_find6", "uc2_packer"},  {"Button1", "ace_packer"},
                                            {"id_conf_find1", "IdConfview"}, {"id_conf_find2", "IdConfed"}};
    for (auto it = toolFields.begin(); it != toolFields.end(); ++it) {
        auto *edit = field(it.value(), "tools/" + it.value());
        auto *button = enable(
            it.key(),
            "Choose a Linux executable. External archive formats appear in the Pack dialog after Apply.");
        if (button && edit)
            connect(button, &QAbstractButton::clicked, dialog, [dialog, edit] {
                const auto file =
                    QFileDialog::getOpenFileName(dialog, "Choose Linux executable", edit->text());
                if (!file.isEmpty()) {
                    if (!QFileInfo(file).isExecutable()) {
                        QMessageBox::warning(dialog, "Executable", "Choose an executable program.");
                        return;
                    }
                    edit->setText(file);
                }
            });
    }
    auto *menuFile = field("id_main_name", "menu/file");
    connect(enable("id_main_search"), &QAbstractButton::clicked, dialog, [dialog, menuFile] {
        const auto file =
            QFileDialog::getOpenFileName(dialog, "Choose native menu JSON", {}, "Menu (*.json)");
        if (!file.isEmpty())
            menuFile->setText(file);
    });
    connect(enable("id_main_edit"), &QAbstractButton::clicked, dialog, [dialog, menuFile] {
        if (menuFile->text().isEmpty())
            return;
        (new FileWindow(nativePath(menuFile->text()), true, dialog))->show();
    });
    connect(enable("Button7"), &QAbstractButton::clicked, dialog,
            [] { QDesktopServices::openUrl(QUrl("https://www.ghisler.com/languages.htm")); });
    for (const auto &pair : {std::pair<QString, QString>{"ButtonWCX", "packer"},
                             {"ButtonWFX", "filesystem"},
                             {"ButtonWLX", "viewer"},
                             {"ButtonWDX", "content"},
                             {"ConfigurePlugins", "packer"}})
        connect(enable(pair.first, "Configure native Linux executable extensions. Windows TC DLLs are not "
                                   "loadable in this process."),
                &QAbstractButton::clicked, dialog,
                [dialog, kind = pair.second] { NativeSettings::configurePlugins(dialog, kind); });
    connect(enable("ButtonDownload"), &QAbstractButton::clicked, dialog,
            [] { QDesktopServices::openUrl(QUrl("https://www.ghisler.com/plugins.htm")); });
    connect(enable("IdConfAssociateViewer"), &QAbstractButton::clicked, dialog,
            [this] { showAssociations(); });
    connect(enable("Button2"), &QAbstractButton::clicked, dialog, [dialog] {
        QDialog options(dialog);
        options.setWindowTitle("Internal viewer");
        auto *layout = new QFormLayout(&options);
        auto *limit = new QSpinBox(&options);
        limit->setRange(1, 64);
        limit->setValue(QSettings().value("viewer/maxMiB", 8).toInt());
        layout->addRow("Maximum file size (MiB):", limit);
        auto *wrap = new QCheckBox("Wrap text", &options);
        wrap->setChecked(QSettings().value("viewer/wrap").toBool());
        layout->addRow(wrap);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &options);
        layout->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &options, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &options, &QDialog::reject);
        if (options.exec() == QDialog::Accepted) {
            QSettings().setValue("viewer/maxMiB", limit->value());
            QSettings().setValue("viewer/wrap", wrap->isChecked());
        }
    });
    for (const auto &pair : {std::pair<QString, QString>{"FrequentlyUsedSaveNowBtn", "save"},
                             {"FrequentlyUsedDeleteNowBtn", "delete"}})
        connect(enable(pair.first), &QAbstractButton::clicked, dialog, [action = pair.second] {
            if (action == "save")
                Preferences::saveFrequent();
            else {
                qApp->setProperty("frequentUsage", QVariantMap{});
                QSettings().remove("frequent/saved");
            }
        });
    auto *saveFrequency = dialog->findChild<QCheckBox *>("SaveFrequentlyUsedOnExitChk");
    saveFrequency->setEnabled(true);
    saveFrequency->setChecked(QSettings().value("frequent/saveOnExit").toBool());
    connect(saveFrequency, &QCheckBox::toggled, dialog,
            [draft](bool value) { draft("frequent/saveOnExit", value); });
    auto *ignore = dialog->findChild<QPlainTextEdit *>("W_IgnoreMemo");
    ignore->setEnabled(true);
    ignore->setPlainText(QSettings().value("ignore/patterns").toStringList().join('\n'));
    connect(ignore, &QPlainTextEdit::textChanged, dialog, [ignore, draft] {
        draft("ignore/patterns", ignore->toPlainText().split('\n', Qt::SkipEmptyParts));
    });
    auto *ignoreEnabled = dialog->findChild<QCheckBox *>("IgnoreEnableCheck");
    ignoreEnabled->setEnabled(true);
    ignoreEnabled->setChecked(QSettings().value("ignore/enabled").toBool());
    connect(ignoreEnabled, &QCheckBox::toggled, dialog, [draft](bool on) { draft("ignore/enabled", on); });
    for (const auto &pair : {std::pair<QString, bool>{"AddSelButton", false}, {"AddSelWithPathButton", true}})
        connect(enable(pair.first), &QAbstractButton::clicked, dialog, [this, ignore, full = pair.second] {
            auto lines = ignore->toPlainText().split('\n', Qt::SkipEmptyParts);
            for (const auto &path : active_->selectedPaths())
                lines << displayPath(full ? path : path.filename());
            lines.removeDuplicates();
            ignore->setPlainText(lines.join('\n'));
        });
    auto *ignoreFile = field("IgnoreListSaveIn", "ignore/file");
    connect(enable("Button9"), &QAbstractButton::clicked, dialog, [dialog, ignoreFile, ignore] {
        const auto file = QFileDialog::getOpenFileName(dialog, "Import ignore patterns");
        if (file.isEmpty())
            return;
        try {
            ignore->setPlainText(QString::fromUtf8(Productivity::readSmall(nativePath(file))));
            ignoreFile->setText(file);
        } catch (const std::exception &e) {
            QMessageBox::warning(dialog, "Ignore list", QString::fromLocal8Bit(e.what()));
        }
    });
    for (const auto &pair : {std::pair<QString, QString>{"FileLogButton", "FileLogEdit"},
                             {"FtpLogFileButton", "FtpLogFileName"}}) {
        const auto key = pair.first == "FileLogButton" ? "logging/file" : "logging/ftpFile";
        auto *edit = field(pair.second, key);
        connect(enable(pair.first), &QAbstractButton::clicked, dialog, [dialog, edit] {
            const auto file = QFileDialog::getSaveFileName(dialog, "Log file", edit->text());
            if (!file.isEmpty())
                edit->setText(file);
        });
    }
    for (const auto &pair : {std::pair<QString, QString>{"FileLogCheck", "logging/enabled"},
                             {"FtpCreateLogFile", "logging/ftpEnabled"}}) {
        auto *check = dialog->findChild<QCheckBox *>(pair.first);
        check->setEnabled(true);
        check->setChecked(QSettings().value(pair.second).toBool());
        connect(check, &QCheckBox::toggled, dialog, [draft, key = pair.second](bool on) { draft(key, on); });
    }
    const auto cacheDefault = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbnails";
    auto *cache = field("ThumbsDbLocationEdit", "thumbnail/cache");
    connect(enable("Button5"), &QAbstractButton::clicked, dialog,
            [cache, cacheDefault] { cache->setText(cacheDefault); });
    connect(enable("Button4"), &QAbstractButton::clicked, dialog, [cache, dialog] {
        const auto directory = QFileDialog::getExistingDirectory(dialog, "Thumbnail cache", cache->text());
        if (!directory.isEmpty())
            cache->setText(directory);
    });
    auto *width = field("ThumbsWidthEdit", "thumbnail/width", "128"),
         *height = field("ThumbsHeightEdit", "thumbnail/height", "96");
    width->setValidator(new QIntValidator(16, 512, width));
    height->setValidator(new QIntValidator(16, 512, height));
    auto *deleted = dialog->findChild<QCheckBox *>("RemoveThumbsForDeleted");
    deleted->setEnabled(true);
    deleted->setChecked(true);
    auto *inaccessible = dialog->findChild<QCheckBox *>("RemoveThumbsForInaccessible");
    inaccessible->setEnabled(true);
    auto *thumbFields = field("W_ThumbsCustomFieldEdit", "thumbnail/fields");
    connect(enable("Button6"), &QAbstractButton::clicked, dialog, [dialog, thumbFields] {
        QStringList fields{"comment", "parent", "size", "path"};
        for (const auto &entry : NativeSettings::plugins("content"))
            fields << entry.toMap().value("name").toString();
        bool ok = false;
        const auto value = QInputDialog::getItem(dialog, "Thumbnail field", "Field:", fields, 0, false, &ok);
        if (ok)
            thumbFields->setText(thumbFields->text() + (thumbFields->text().isEmpty() ? "" : ";") + value);
    });
    connect(enable("Button12"), &QAbstractButton::clicked, dialog, [dialog] {
        NativeSettings::editRecords(dialog, "Thumbnail fields by filename mask", "thumbnail/typeFields",
                                    {"mask", "fields"});
    });
    connect(
        enable("CompactDatabase"), &QAbstractButton::clicked, dialog, [dialog, cache, deleted, inaccessible] {
            int removed = 0;
            QDir directory(cache->text());
            for (const auto &name : directory.entryList({"tc-thumb-*.json"}, QDir::Files)) {
                if (!QRegularExpression("^tc-thumb-[0-9a-f]{64}\\.json$").match(name).hasMatch())
                    continue;
                try {
                    const auto object =
                        QJsonDocument::fromJson(Productivity::readSmall(nativePath(directory.filePath(name))))
                            .object();
                    if (object.value("format") != "TwinCommander-thumbnail-1")
                        continue;
                    const auto path = std::filesystem::path(
                        QByteArray::fromBase64(object.value("path").toString().toLatin1()).toStdString());
                    std::error_code error;
                    if (!std::filesystem::is_regular_file(path, error) &&
                        (error ? inaccessible->isChecked() : deleted->isChecked())) {
                        QFile::remove(directory.filePath(name.left(name.size() - 5) + ".png"));
                        if (QFile::remove(directory.filePath(name)))
                            ++removed;
                    }
                } catch (const std::exception &) {
                }
            }
            QMessageBox::information(dialog, "Thumbnail cache",
                                     QString("Removed %1 obsolete cache records").arg(removed));
        });
    connect(enable("Button3"), &QAbstractButton::clicked, dialog, [dialog] {
        NativeSettings::editRecords(dialog, "File colors — HTML color names or #RRGGBB", "colorRules",
                                    {"mask", "foreground", "background"});
    });
    connect(enable("Idconf_AddCustomHints"), &QAbstractButton::clicked, dialog, [dialog] {
        NativeSettings::editRecords(dialog, "Hints — {name}, {path}, {size}, {comment}", "hintRules",
                                    {"mask", "template"});
    });
    connect(enable("Button8"), &QAbstractButton::clicked, dialog,
            [draft] { draft("appearance/iconDirectory", QString()); });
    connect(enable("id_icondetailsbtn",
                   "Choose a directory with file.png, folder.png and command-name.png icons."),
            &QAbstractButton::clicked, dialog, [dialog, draft] {
                const auto directory = QFileDialog::getExistingDirectory(dialog, "Icon directory");
                if (!directory.isEmpty())
                    draft("appearance/iconDirectory", directory);
            });
    auto *custom = dialog->findChild<QListWidget *>("W_CustomColumnList");
    custom->setEnabled(true);
    const auto updateColumns = [custom] {
        custom->clear();
        for (const auto &entry : QSettings().value("customColumns").toList())
            custom->addItem(entry.toMap().value("name").toString());
    };
    updateColumns();
    for (const auto &name : {QString("CustomColumnsAdd"), QString("CustomEdit"), QString("CustomColumnsCopy"),
                             QString("CustomColumnsDelete")})
        connect(enable(name), &QAbstractButton::clicked, dialog, [dialog, custom, updateColumns, name] {
            auto rows = QSettings().value("customColumns").toList();
            const int selected = custom->currentRow();
            if (name == "CustomColumnsDelete") {
                if (selected >= 0) {
                    rows.removeAt(selected);
                    QSettings().setValue("customColumns", rows);
                    updateColumns();
                }
                return;
            }
            if (name != "CustomColumnsAdd" && selected < 0)
                return;
            const auto original = selected >= 0 ? rows[selected].toMap() : QVariantMap{};
            bool ok = false;
            const auto title = QInputDialog::getText(
                                   dialog, "Custom columns", "Configuration name:", QLineEdit::Normal,
                                   name == "CustomColumnsCopy" ? original.value("name").toString() + " copy"
                                                               : original.value("name").toString(),
                                   &ok)
                                   .trimmed();
            if (!ok || title.isEmpty())
                return;
            const auto fields = QInputDialog::getText(
                dialog, "Custom columns",
                "Fields separated by semicolons: comment;path;permissions;owner;parent;size or native "
                "content field names",
                QLineEdit::Normal, original.value("fields", "comment;path").toString(), &ok);
            if (!ok)
                return;
            const QVariantMap record{{"name", title}, {"fields", fields}};
            if (name == "CustomEdit")
                rows[selected] = record;
            else
                rows.append(record);
            QSettings().setValue("customColumns", rows);
            updateColumns();
        });
    auto *modeList = dialog->findChild<QListWidget *>("ViewModeList");
    auto *columns = dialog->findChild<QComboBox *>("ColumnsCombo");
    auto *sort = dialog->findChild<QComboBox *>("SortOrderCombo");
    auto *descending = dialog->findChild<QCheckBox *>("SortDescendingCheck");
    columns->setEnabled(true);
    columns->clear();
    columns->addItems({"Full", "Brief"});
    for (const auto &entry : QSettings().value("customColumns").toList())
        columns->addItem(entry.toMap().value("name").toString(), entry.toMap().value("fields"));
    sort->setEnabled(true);
    sort->clear();
    sort->addItems({"Name", "Extension", "Size", "Date"});
    descending->setEnabled(true);
    const auto storeMode = [dialog, modeList, columns, sort, descending] {
        if (!modeList->currentItem())
            return;
        auto modes = dialog->property("viewModesDraft").toMap();
        const auto name = modeList->currentItem()->text();
        auto mode = modes.value(name).toMap();
        mode["brief"] = columns->currentIndex() == 1;
        mode["fields"] = columns->currentIndex() > 1 ? columns->currentData().toString() : QString();
        mode["sort"] = sort->currentIndex();
        mode["order"] = descending->isChecked() ? 1 : 0;
        modes[name] = mode;
        dialog->setProperty("viewModesDraft", modes);
    };
    connect(columns, &QComboBox::activated, dialog, [storeMode](int) { storeMode(); });
    connect(sort, &QComboBox::activated, dialog, [storeMode](int) { storeMode(); });
    connect(descending, &QCheckBox::toggled, dialog, [storeMode](bool) { storeMode(); });
    connect(modeList, &QListWidget::currentRowChanged, dialog,
            [dialog, modeList, columns, sort, descending](int) {
                if (!modeList->currentItem())
                    return;
                const auto mode =
                    dialog->property("viewModesDraft").toMap().value(modeList->currentItem()->text()).toMap();
                const QSignalBlocker a(columns), b(sort), c(descending);
                int index = mode.value("brief").toBool() ? 1 : 0;
                for (int i = 2; i < columns->count(); ++i)
                    if (columns->itemData(i) == mode.value("fields"))
                        index = i;
                columns->setCurrentIndex(index);
                sort->setCurrentIndex(mode.value("sort").toInt());
                descending->setChecked(mode.value("order").toInt() == 1);
            });
    const auto modeEdit = [dialog, modeList](const QString &key, const QString &title) {
        if (!modeList || !modeList->currentItem())
            return;
        auto modes = dialog->property("viewModesDraft").toMap();
        const auto name = modeList->currentItem()->text();
        auto mode = modes.value(name).toMap();
        bool ok = false;
        const auto value =
            QInputDialog::getText(dialog, title, title, QLineEdit::Normal, mode.value(key).toString(), &ok);
        if (ok) {
            mode[key] = value;
            modes[name] = mode;
            dialog->setProperty("viewModesDraft", modes);
        }
    };
    connect(enable("AdditionalSortButton"), &QAbstractButton::clicked, dialog,
            [modeEdit] { modeEdit("secondary", "Secondary sort field: name, extension, size or date"); });
    connect(enable("AutoRunAdd"), &QAbstractButton::clicked, dialog,
            [modeEdit] { modeEdit("command", "Internal command to run after switching view mode"); });
    for (const auto &pair : {std::pair<QString, QString>{"id_TabColorBgBtn", "background"},
                             {"id_TabColorBg2Btn", "alternate"},
                             {"id_TabColorBtn", "tabColor"}})
        connect(enable(pair.first), &QAbstractButton::clicked, dialog, [dialog, modeList, key = pair.second] {
            if (!modeList || !modeList->currentItem())
                return;
            const auto color = QColorDialog::getColor(Qt::white, dialog);
            if (!color.isValid())
                return;
            auto modes = dialog->property("viewModesDraft").toMap();
            const auto name = modeList->currentItem()->text();
            auto mode = modes.value(name).toMap();
            mode[key] = color.name();
            modes[name] = mode;
            dialog->setProperty("viewModesDraft", modes);
        });
    connect(enable("IconChooseButton"), &QAbstractButton::clicked, dialog, [dialog, modeList] {
        if (!modeList || !modeList->currentItem())
            return;
        const auto file =
            QFileDialog::getOpenFileName(dialog, "View-mode icon", {}, "Images (*.png *.svg *.ico)");
        if (file.isEmpty())
            return;
        auto modes = dialog->property("viewModesDraft").toMap();
        const auto name = modeList->currentItem()->text();
        auto mode = modes.value(name).toMap();
        mode["icon"] = file;
        modes[name] = mode;
        dialog->setProperty("viewModesDraft", modes);
    });
    auto *ruleList = dialog->findChild<QListWidget *>("ModeChangeList");
    ruleList->setEnabled(true);
    const auto refreshRules = [ruleList] {
        ruleList->clear();
        for (const auto &entry : QSettings().value("autoViewRules").toList()) {
            const auto row = entry.toMap();
            ruleList->addItem(row.value("path").toString() + " | " + row.value("masks").toString() + " → " +
                              row.value("mode").toString());
        }
    };
    refreshRules();
    for (const auto &name : {QString("AddRulesButton"), QString("DeleteRulesButton"),
                             QString("MoreRulesButton"), QString("LessRulesButton")})
        connect(enable(name), &QAbstractButton::clicked, dialog, [dialog, ruleList, refreshRules, name] {
            auto rules = QSettings().value("autoViewRules").toList();
            const int selected = ruleList->currentRow();
            if (name == "AddRulesButton") {
                bool ok = false;
                const auto path = QInputDialog::getText(dialog, "Auto view mode",
                                                        "Directory path glob:", QLineEdit::Normal, "*", &ok);
                if (!ok)
                    return;
                const auto modes = dialog->property("viewModesDraft").toMap();
                const auto mode = QInputDialog::getItem(dialog, "Auto view mode", "View mode:", modes.keys(),
                                                        0, false, &ok);
                if (!ok || mode.isEmpty())
                    return;
                rules.append(QVariantMap{{"path", path}, {"masks", QString()}, {"mode", mode}});
            } else {
                if (selected < 0)
                    return;
                if (name == "DeleteRulesButton")
                    rules.removeAt(selected);
                else {
                    auto rule = rules[selected].toMap();
                    auto masks = rule.value("masks").toString().split(';', Qt::SkipEmptyParts);
                    if (name == "MoreRulesButton") {
                        bool ok = false;
                        const auto mask = QInputDialog::getText(
                            dialog, "Add sub-rule",
                            "A matching filename must exist (glob):", QLineEdit::Normal, "*", &ok);
                        if (!ok || mask.isEmpty())
                            return;
                        masks << mask;
                    } else if (!masks.isEmpty())
                        masks.removeLast();
                    rule["masks"] = masks.join(';');
                    rules[selected] = rule;
                }
            }
            QSettings().setValue("autoViewRules", rules);
            refreshRules();
            if (ruleList->count())
                ruleList->setCurrentRow(qBound(0, selected, ruleList->count() - 1));
        });
    auto *keys = dialog->findChild<QComboBox *>("HOT_Keys");
    keys->setEnabled(true);
    keys->setEditable(true);
    keys->clear();
    for (int i = 1; i <= 12; ++i)
        keys->addItem("F" + QString::number(i));
    for (char c = 'A'; c <= 'Z'; ++c)
        keys->addItem(QString(QChar(c)));
    auto *command = field("HOT_Command_Edit", "hotkey/lastCommand");
    auto *alias = dialog->findChild<QComboBox *>("AliasCombo");
    alias->setEnabled(true);
    alias->setEditable(true);
    for (const auto &name :
         {"HOT_Control", "HOT_Alt", "HOT_Shift", "HOT_Win", "HotkeyRadioBtn", "AliasRadioBtn"})
        if (auto *button = dialog->findChild<QAbstractButton *>(name))
            button->setEnabled(true);
    const auto chooseCommand = [this, dialog, command] {
        QStringList names;
        for (auto *action : findChildren<QAction *>())
            if (!action->objectName().isEmpty() && action->isEnabled())
                names << action->objectName();
        names.removeDuplicates();
        names.sort();
        bool ok = false;
        const auto name =
            QInputDialog::getItem(dialog, "Choose internal command", "Command:", names, 0, false, &ok);
        if (ok)
            command->setText(name);
    };
    connect(enable("hot_command_button"), &QAbstractButton::clicked, dialog, chooseCommand);
    const auto sequence = [dialog, keys] {
        QString text;
        for (const auto &pair : {std::pair<QString, QString>{"HOT_Control", "Ctrl+"},
                                 {"HOT_Alt", "Alt+"},
                                 {"HOT_Shift", "Shift+"},
                                 {"HOT_Win", "Meta+"}})
            if (dialog->findChild<QAbstractButton *>(pair.first)->isChecked())
                text += pair.second;
        return QKeySequence(text + keys->currentText()).toString(QKeySequence::PortableText);
    };
    connect(enable("HOT_OKBTN", "Assign hotkey or alias"), &QAbstractButton::clicked, dialog,
            [this, dialog, command, alias, sequence] {
                auto *target = commands_.get(command->text());
                if (!target) {
                    QMessageBox::warning(dialog, "Hotkey", "Choose a registered internal command.");
                    return;
                }
                if (dialog->findChild<QAbstractButton *>("AliasRadioBtn")->isChecked()) {
                    const auto name = alias->currentText().trimmed().toCaseFolded();
                    if (!QRegularExpression("^[A-Za-z][A-Za-z0-9_]*$").match(name).hasMatch())
                        return;
                    if (!commands_.addUserAlias(name, target)) {
                        QMessageBox::warning(dialog, "Alias",
                                             "Aliases cannot replace built-in command names.");
                        return;
                    }
                    auto values = QSettings().value("commandAliases").toMap();
                    values[name] = command->text();
                    QSettings().setValue("commandAliases", values);
                } else {
                    const auto key = sequence();
                    auto bindings = QSettings().value("keyBindings").toList();
                    for (int i = bindings.size() - 1; i >= 0; --i)
                        if (bindings[i].toMap().value("key") == key)
                            bindings.removeAt(i);
                    bindings.append(QVariantMap{{"key", key}, {"command", command->text()}});
                    QSettings().setValue("keyBindings", bindings);
                }
                applyUserCommands();
            });
    connect(enable("HOT_list_button", "List and edit key bindings"), &QAbstractButton::clicked, dialog,
            [this, dialog] {
                NativeSettings::editRecords(dialog, "Keyboard bindings", "keyBindings", {"key", "command"});
                applyUserCommands();
            });
    connect(enable("HOT_CANCELBTN", "Remove hotkey or alias"), &QAbstractButton::clicked, dialog,
            [this, dialog, alias, sequence] {
                if (dialog->findChild<QAbstractButton *>("AliasRadioBtn")->isChecked()) {
                    auto values = QSettings().value("commandAliases").toMap();
                    commands_.removeAlias(alias->currentText());
                    values.remove(alias->currentText().trimmed().toCaseFolded());
                    QSettings().setValue("commandAliases", values);
                } else {
                    auto bindings = QSettings().value("keyBindings").toList();
                    for (int i = bindings.size() - 1; i >= 0; --i)
                        if (bindings[i].toMap().value("key") == sequence())
                            bindings.removeAt(i);
                    QSettings().setValue("keyBindings", bindings);
                }
                applyUserCommands();
            });
    connect(enable("SND_Configure"), &QAbstractButton::clicked, dialog, [dialog] {
        const auto sound = QFileDialog::getOpenFileName(
            dialog, "Completion sound (Cancel keeps current sound)", {}, "Audio (*.wav *.ogg *.flac)");
        if (!sound.isEmpty())
            QSettings().setValue("sound/file", sound);
    });
}

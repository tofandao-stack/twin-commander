#include "classicui.h"
#include "mainwindow.h"
#include "productivitytools.h"
#include "referenceforms.h"
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QFontDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
namespace {
QString barKey(bool vertical) { return vertical ? "customBars/vertical" : "customBars/horizontal"; }
QJsonArray barDefinitions(bool vertical) {
    const auto data = QSettings().value(barKey(vertical)).toByteArray();
    return data.isEmpty() ? commanderUiReference().value(vertical ? "verticalToolbar" : "toolbar").toArray()
                          : QJsonDocument::fromJson(data).array();
}
QIcon slotIcon(const QJsonObject &slot) {
    const auto file = slot.value("iconFile").toString();
    return file.isEmpty() ? commanderIcon(slot.value("command").toString() == "notepad.exe"
                                              ? "editor"
                                              : slot.value("command").toString())
                          : QIcon(file);
}
} // namespace
void MainWindow::rebuildButtonBar(bool vertical) {
    const auto definitions = barDefinitions(vertical);
    if (!vertical) {
        const auto old = buttonBar_->actions();
        buttonBar_->clear();
        for (auto *action : old)
            if (action->parent() == buttonBar_)
                delete action;
    } else {
        auto *layout = verticalBar_->layout();
        while (auto *item = layout->takeAt(0)) {
            delete item->widget();
            delete item;
        }
    }
    int position = 0;
    for (const auto &value : definitions) {
        auto slot = value.toObject();
        ++position;
        const auto name = slot.value("command").toString();
        if (slot.value("separator").toBool() || name.isEmpty()) {
            if (!vertical) {
                auto *separator = buttonBar_->addSeparator();
                separator->setProperty("referenceSlot", position);
            } else {
                auto *space = new QWidget(verticalBar_);
                space->setFixedHeight(position <= 2 && (name.isEmpty() || name == "-2") ? 24 : 5);
                verticalBar_->layout()->addWidget(space);
            }
            continue;
        }
        auto *target = commands_.get(name == "notepad.exe" ? "cm_Edit" : name);
        if (!target)
            for (auto *candidate : findChildren<QAction *>())
                if (slot.value("nativeId").toInt() != 0 &&
                    candidate->property("tcCommandId").toInt() == slot.value("nativeId").toInt()) {
                    target = candidate;
                    break;
                }
        if (!target) {
            target = commands_.get("bar_" + name);
            if (!target) {
                target = commands_.add("bar_" + name, name, {}, [] {});
                target->setEnabled(false);
            }
        }
        if (!vertical) {
            auto *proxy =
                new QAction(slotIcon(slot), slot.value("tooltip").toString(target->text()), buttonBar_);
            proxy->setEnabled(target->isEnabled());
            proxy->setCheckable(target->isCheckable());
            proxy->setChecked(target->isChecked());
            proxy->setProperty("referenceSlot", position);
            proxy->setProperty("referenceToolbarCommand", name);
            proxy->setToolTip(slot.value("tooltip").toString(target->text().section('\t', 0, 0).remove('&')));
            connect(proxy, &QAction::triggered, target, [proxy, target] {
                target->trigger();
                proxy->setChecked(target->isChecked());
            });
            connect(target, &QAction::changed, proxy, [proxy, target] {
                proxy->setEnabled(target->isEnabled());
                proxy->setChecked(target->isChecked());
            });
            buttonBar_->addAction(proxy);
        } else {
            auto *button = new QToolButton(verticalBar_);
            button->setDefaultAction(target);
            button->setIcon(slotIcon(slot));
            if (slot.contains("tooltip"))
                button->setToolTip(slot.value("tooltip").toString());
            button->setIconSize(QSize(20, 20));
            button->setAutoRaise(true);
            button->setFixedSize(26, 26);
            button->setProperty("referenceSlot", position);
            verticalBar_->layout()->addWidget(button);
        }
    }
    if (vertical)
        qobject_cast<QVBoxLayout *>(verticalBar_->layout())->addStretch();
}
void MainWindow::showButtonBarEditor(bool vertical) {
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName(vertical ? "verticalBarEditor" : "horizontalBarEditor");
    dialog->setWindowTitle("Change button bar");
    dialog->setFixedSize(565, 283);
    const QSet<QString> enabled{"id_btn_bar",
                                "Ok",
                                "Cancel",
                                "id_Globalhelp",
                                "id_btn_barfilesearch",
                                "id_btn_delete",
                                "id_btn_add",
                                "id_btn_addcmd",
                                "id_btn_iconfile",
                                "BitBtn1",
                                "id_btn_saveicon",
                                "W_btn_command",
                                "W_btn_iconfilename",
                                "W_btn_ToolTip",
                                "id_btn_icon"};
    for (const auto &child : referenceDialogMetadata("BUTTONCHANGEDLG").value("children").toArray())
        renderReferenceControls(child.toObject(), dialog, enabled);
    auto *buttons = dialog->findChild<QListWidget *>("id_btn_bar");
    buttons->setViewMode(QListView::IconMode);
    buttons->setFlow(QListView::LeftToRight);
    buttons->setWrapping(false);
    buttons->setIconSize(QSize(24, 24));
    buttons->setDragDropMode(QAbstractItemView::InternalMove);
    const auto move = [buttons](int delta) {
        const int row = buttons->currentRow(), next = row + delta;
        if (row < 0 || next < 0 || next >= buttons->count())
            return;
        auto *item = buttons->takeItem(row);
        buttons->insertItem(next, item);
        buttons->setCurrentItem(item);
    };
    auto *moveLeft = new QShortcut(QKeySequence("Alt+Left"), dialog),
         *moveRight = new QShortcut(QKeySequence("Alt+Right"), dialog);
    connect(moveLeft, &QShortcut::activated, dialog, [move] { move(-1); });
    connect(moveRight, &QShortcut::activated, dialog, [move] { move(1); });
    buttons->setToolTip("Reorder with Alt+Left / Alt+Right, or use the context menu.");
    buttons->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(buttons, &QListWidget::customContextMenuRequested, dialog, [buttons, move](const QPoint &point) {
        auto *menu = new QMenu(buttons);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        connect(menu->addAction("Move left (Alt+Left)"), &QAction::triggered, buttons, [move] { move(-1); });
        connect(menu->addAction("Move right (Alt+Right)"), &QAction::triggered, buttons, [move] { move(1); });
        menu->popup(buttons->viewport()->mapToGlobal(point));
    });
    auto *barLabel = new QLabel(vertical ? "VERTICAL.BAR (user settings)" : "DEFAULT.BAR (user settings)",
                                dialog->findChild<QWidget *>("id_btn_barfile"));
    barLabel->setGeometry(2, 0, 300, 19);
    auto *command = dialog->findChild<QLineEdit *>("W_btn_command"),
         *icon = dialog->findChild<QLineEdit *>("W_btn_iconfilename"),
         *tip = dialog->findChild<QLineEdit *>("W_btn_ToolTip");
    auto *iconPreview = dialog->findChild<QListWidget *>("id_btn_icon");
    iconPreview->setViewMode(QListView::IconMode);
    iconPreview->setIconSize(QSize(32, 32));
    const auto populate = [buttons](const QJsonArray &barSlots) {
        buttons->clear();
        for (const auto &value : barSlots) {
            auto slot = value.toObject();
            if (slot.value("separator").toBool())
                slot["command"] = "";
            auto *item =
                new QListWidgetItem(slot.value("separator").toBool() ? QIcon() : slotIcon(slot), "", buttons);
            item->setData(Qt::UserRole, slot.toVariantMap());
            item->setToolTip(slot.value("command").toString());
            item->setSizeHint(QSize(30, 30));
        }
        buttons->setCurrentRow(0);
    };
    connect(buttons, &QListWidget::currentRowChanged, dialog,
            [buttons, command, icon, tip, iconPreview](int row) {
                const QSignalBlocker a(command), b(icon), c(tip);
                const auto slot = row >= 0 ? buttons->item(row)->data(Qt::UserRole).toMap() : QVariantMap{};
                command->setText(slot.value("command").toString());
                icon->setText(slot.value("iconFile").toString());
                tip->setText(slot.value("tooltip").toString());
                iconPreview->clear();
                if (row >= 0)
                    new QListWidgetItem(buttons->item(row)->icon(), "", iconPreview);
            });
    const auto edit = [buttons, command, icon, tip] {
        auto *item = buttons->currentItem();
        if (!item)
            return;
        auto slot = item->data(Qt::UserRole).toMap();
        slot["command"] = command->text().trimmed();
        slot["separator"] = command->text().trimmed().isEmpty();
        slot["iconFile"] = icon->text();
        slot["tooltip"] = tip->text();
        slot.remove("nativeId");
        item->setData(Qt::UserRole, slot);
        item->setIcon(slot.value("separator").toBool() ? QIcon()
                                                       : slotIcon(QJsonObject::fromVariantMap(slot)));
        item->setToolTip(command->text());
    };
    for (auto *field : {command, icon, tip})
        connect(field, &QLineEdit::textEdited, dialog, [edit](const auto &) { edit(); });
    const auto choose = [this, dialog, command, edit] {
        QStringList names;
        for (const auto &id : commands_.ids()) {
            auto *action = commands_.get(id);
            if (action && (action->isEnabled() || action->property("referenceAvailable").toBool()))
                names << id;
        }
        names.removeDuplicates();
        names.sort();
        names.prepend("(separator)");
        bool ok = false;
        const auto value =
            QInputDialog::getItem(dialog, "Choose command", "Internal command:", names, 0, false, &ok);
        if (ok) {
            command->setText(value == "(separator)" ? QString() : value);
            edit();
        }
    };
    connect(dialog->findChild<QPushButton *>("id_btn_addcmd"), &QPushButton::clicked, dialog, choose);
    connect(dialog->findChild<QToolButton *>("BitBtn1"), &QToolButton::clicked, dialog, choose);
    connect(dialog->findChild<QPushButton *>("id_btn_add"), &QPushButton::clicked, dialog, [buttons, choose] {
        if (buttons->count() >= 200)
            return;
        auto *item = new QListWidgetItem;
        item->setSizeHint(QSize(30, 30));
        buttons->insertItem(buttons->currentRow() + 1, item);
        buttons->setCurrentItem(item);
        choose();
    });
    connect(dialog->findChild<QPushButton *>("id_btn_delete"), &QPushButton::clicked, dialog,
            [buttons] { delete buttons->takeItem(buttons->currentRow()); });
    connect(dialog->findChild<QPushButton *>("id_btn_iconfile"), &QPushButton::clicked, dialog,
            [dialog, icon, edit] {
                const auto file = QFileDialog::getOpenFileName(dialog, "Choose icon", {},
                                                               "Images (*.png *.ico *.svg *.xpm)");
                if (!file.isEmpty()) {
                    icon->setText(file);
                    edit();
                }
            });
    connect(dialog->findChild<QPushButton *>("id_btn_saveicon"), &QPushButton::clicked, dialog,
            [dialog, buttons] {
                auto *item = buttons->currentItem();
                if (!item)
                    return;
                const auto file =
                    QFileDialog::getSaveFileName(dialog, "Export icon", {}, "Icon (*.ico);;PNG (*.png)");
                if (!file.isEmpty() && !item->icon().pixmap(32, 32).save(file))
                    QMessageBox::warning(dialog, "Icon", "The selected icon could not be exported.");
            });
    connect(dialog->findChild<QPushButton *>("id_btn_barfilesearch"), &QPushButton::clicked, dialog,
            [this, dialog, populate] {
                const auto file = QFileDialog::getOpenFileName(dialog, "Import button bar", {},
                                                               "Button bars (*.bar *.json)");
                if (file.isEmpty())
                    return;
                try {
                    QJsonArray barSlots;
                    if (file.endsWith(".json", Qt::CaseInsensitive)) {
                        QJsonParseError error;
                        const auto document =
                            QJsonDocument::fromJson(Productivity::readSmall(nativePath(file)), &error);
                        if (error.error != QJsonParseError::NoError || !document.isArray())
                            throw NativeFs::Error("Invalid button-bar JSON array");
                        barSlots = document.array();
                    } else {
                        QSettings settings(file, QSettings::IniFormat);
                        settings.beginGroup("Buttonbar");
                        const int count = settings.value("Buttoncount").toInt();
                        if (count < 1 || count > 200)
                            throw NativeFs::Error("Invalid button count");
                        for (int i = 1; i <= count; ++i) {
                            const auto name = settings.value("cmd" + QString::number(i)).toString();
                            barSlots.append(QJsonObject{
                                {"command", name},
                                {"separator", name.isEmpty()},
                                {"tooltip", settings.value("menu" + QString::number(i)).toString()}});
                        }
                    }
                    if (barSlots.size() > 200)
                        throw NativeFs::Error("At most 200 buttons supported");
                    for (const auto &value : barSlots) {
                        const auto name = value.toObject().value("command").toString();
                        if (!name.isEmpty() && !commands_.get(name))
                            throw NativeFs::Error("Only registered internal commands are supported");
                    }
                    populate(barSlots);
                } catch (const std::exception &e) {
                    QMessageBox::warning(dialog, "Import button bar", QString::fromLocal8Bit(e.what()));
                }
            });
    connect(dialog->findChild<QPushButton *>("Ok"), &QPushButton::clicked, dialog,
            [this, vertical, dialog, buttons] {
                QJsonArray barSlots;
                for (int i = 0; i < buttons->count(); ++i) {
                    auto slot = QJsonObject::fromVariantMap(buttons->item(i)->data(Qt::UserRole).toMap());
                    const auto name = slot.value("command").toString();
                    if (!slot.value("separator").toBool() && !name.isEmpty() && name != "notepad.exe" &&
                        !commands_.get(name) && !slot.value("nativeId").toInt()) {
                        QMessageBox::warning(dialog, "Button bar", "Unknown internal command: " + name);
                        return;
                    }
                    barSlots.append(slot);
                }
                QSettings().setValue(barKey(vertical),
                                     QJsonDocument(barSlots).toJson(QJsonDocument::Compact));
                rebuildButtonBar(vertical);
                dialog->accept();
            });
    connect(dialog->findChild<QPushButton *>("Cancel"), &QPushButton::clicked, dialog, &QDialog::reject);
    connect(dialog->findChild<QPushButton *>("id_Globalhelp"), &QPushButton::clicked, dialog, [dialog] {
        QMessageBox::information(
            dialog, "Button bar",
            "Add/delete buttons, choose an internal command, set its icon and tooltip, or drag to reorder. "
            "OK persists the bar; Cancel discards edits. Empty commands create separators. Imports support "
            "JSON and the internal-command subset of .BAR files. External commands and parameters remain "
            "unavailable.");
    });
    populate(barDefinitions(vertical));
    dialog->show();
}

void MainWindow::configureAppearance(QDialog *dialog) {
    const auto draft = [dialog](const QString &key, const QVariant &value) {
        auto changes = dialog->property("appearanceDraft").toMap();
        changes.insert(key, value);
        dialog->setProperty("appearanceDraft", changes);
    };
    for (const auto &pair : {std::pair<QString, QString>{"ChangeFont1", "fileFont"},
                             {"ChangeFont2", "mainFont"},
                             {"ChangeFont3", "dialogFont"}}) {
        auto *button = dialog->findChild<QPushButton *>(pair.first);
        button->setEnabled(true);
        button->setToolTip({});
        connect(button, &QPushButton::clicked, dialog, [dialog, draft, key = pair.second] {
            QFont font = qApp->font();
            font.fromString(
                dialog->property("appearanceDraft")
                    .toMap()
                    .value("appearance/" + key, QSettings().value("appearance/" + key, font.toString()))
                    .toString());
            bool ok = false;
            const auto selected = QFontDialog::getFont(&ok, font, dialog);
            if (ok)
                draft("appearance/" + key, selected.toString());
        });
    }
    const QMap<QString, QString> colors{{"id_ForeChooseColor", "textColor"},
                                        {"id_BackChooseColor", "baseColor"},
                                        {"id_MarkChooseColor", "markColor"},
                                        {"id_CursorChooseColor", "cursorColor"},
                                        {"id_CursorFontChooseColor", "cursorTextColor"},
                                        {"id_BackChooseColor2", "alternateColor"}};
    for (auto it = colors.begin(); it != colors.end(); ++it) {
        auto *button = dialog->findChild<QPushButton *>(it.key());
        button->setEnabled(true);
        button->setToolTip({});
        connect(button, &QPushButton::clicked, dialog, [dialog, draft, key = it.value()] {
            const auto initial =
                dialog->property("appearanceDraft")
                    .toMap()
                    .value("appearance/" + key, QSettings().value("appearance/" + key, "#000080"))
                    .toString();
            const auto color = QColorDialog::getColor(QColor(initial), dialog, "Choose color");
            if (color.isValid())
                draft("appearance/" + key, color.name());
        });
    }
    auto *other = dialog->findChild<QPushButton *>("id_OtherChooseColor");
    other->setEnabled(true);
    other->setToolTip("Choose the classic button background color.");
    connect(other, &QPushButton::clicked, dialog, [dialog, draft] {
        const auto color =
            QColorDialog::getColor(qApp->palette().color(QPalette::Button), dialog, "Button background");
        if (color.isValid())
            draft("appearance/buttonColor", color.name());
    });
    auto *defaults = dialog->findChild<QPushButton *>("id_TabDefaults");
    defaults->setEnabled(true);
    defaults->setToolTip("Restore default widths for Name, Ext, Size, Date and Attr when Apply is pressed.");
    connect(defaults, &QPushButton::clicked, dialog, [dialog] { dialog->setProperty("resetColumns", true); });
    for (const auto &pair :
         {std::pair<QString, bool>{"HistorySaveNowBtn", true}, {"HistoryDeleteNowBtn", false}}) {
        auto *button = dialog->findChild<QPushButton *>(pair.first);
        button->setEnabled(true);
        button->setToolTip({});
        connect(button, &QPushButton::clicked, dialog, [this, save = pair.second] {
            if (save)
                active_->storeHistory();
            else
                for (auto *panel : findChildren<FilePanel *>())
                    panel->clearHistory();
        });
    }
    auto *history = dialog->findChild<QPushButton *>("ConfHistoryButton");
    history->setEnabled(true);
    history->setToolTip({});
    connect(history, &QPushButton::clicked, dialog, [this] { active_->showHistory(); });
    configureViewModes(dialog);
}
void MainWindow::configureViewModes(QDialog *dialog) {
    auto *list = dialog->findChild<QListWidget *>("id_viewmodes");
    // The original mode list is dynamically populated, so locate it by its page.
    if (!list) {
        auto *add = dialog->findChild<QPushButton *>("id_addmenu");
        list = add->parentWidget()->findChild<QListWidget *>();
    }
    if (!list)
        return;
    auto modes = QSettings().value("viewModes").toMap();
    dialog->setProperty("viewModesDraft", modes);
    list->setEnabled(true);
    list->clear();
    list->addItems(modes.keys());
    const auto refresh = [dialog, list](const QVariantMap &value) {
        dialog->setProperty("viewModesDraft", value);
        list->clear();
        list->addItems(value.keys());
    };
    for (const auto &name : {QString("id_addmenu"), QString("id_deletemenu"), QString("id_changetitle")}) {
        auto *button = dialog->findChild<QPushButton *>(name);
        button->setEnabled(true);
        button->setToolTip({});
        connect(button, &QPushButton::clicked, dialog, [this, dialog, list, refresh, name] {
            auto modes = dialog->property("viewModesDraft").toMap();
            const auto selected = list->currentItem() ? list->currentItem()->text() : QString();
            if (name == "id_deletemenu") {
                if (!selected.isEmpty()) {
                    modes.remove(selected);
                    refresh(modes);
                }
                return;
            }
            bool ok = false;
            const auto title = QInputDialog::getText(
                                   dialog, name == "id_addmenu" ? "Save current view mode" : "Change title",
                                   "View mode name:", QLineEdit::Normal, selected, &ok)
                                   .trimmed();
            if (!ok || title.isEmpty() || modes.contains(title))
                return;
            if (name == "id_changetitle") {
                if (selected.isEmpty())
                    return;
                modes.insert(title, modes.take(selected));
            } else
                modes.insert(
                    title,
                    QVariantMap{{"brief", active_->isBrief()},
                                {"sort", active_->table()->horizontalHeader()->sortIndicatorSection()},
                                {"order", static_cast<int>(
                                              active_->table()->horizontalHeader()->sortIndicatorOrder())}});
            refresh(modes);
            list->setCurrentRow(list->count() - 1);
        });
    }
}

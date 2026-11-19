#include "advancedwindows.h"
#include "backgroundpool.h"
#include "directorymodel.h"
#include "nativesettings.h"
#include "referenceforms.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace {
void setupTable(QTableWidget *table, const QStringList &headers, int stretch) {
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setSectionResizeMode(stretch, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
}
template <class Result, class Work, class Done> void background(QObject *owner, Work work, Done done) {
    auto *watcher = new QFutureWatcher<Result>(owner);
    QObject::connect(watcher, &QFutureWatcher<Result>::finished, owner, [watcher, done] {
        auto result = watcher->result();
        watcher->deleteLater();
        done(std::move(result));
    });
    watcher->setFuture(QtConcurrent::run(ioPool(), std::move(work)));
}
} // namespace
ReportDialog::ReportDialog(const QString &title,
                           std::function<QString(const std::shared_ptr<LocalFiles::Control> &)> task,
                           QWidget *parent)
    : QDialog(parent), control_(std::make_shared<LocalFiles::Control>()) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(title);
    resize(900, 540);
    auto *layout = new QVBoxLayout(this);
    auto *text = new QPlainTextEdit("Working…", this);
    text->setReadOnly(true);
    text->setObjectName("toolReport");
    layout->addWidget(text);
    auto *status = new QLabel(this);
    layout->addWidget(status);
    auto *stop = new QPushButton("Stop", this);
    layout->addWidget(stop);
    connect(stop, &QPushButton::clicked, this, [control = control_] { control->cancelled = true; });
    auto *timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [control = control_, status] {
        status->setText(QString("%1 entries · %2 MiB processed")
                            .arg(control->completed.load())
                            .arg(control->bytes.load() / (1024.0 * 1024), 0, 'f', 1));
    });
    timer->start();
    background<QString>(
        this,
        [task = std::move(task), control = control_] {
            try {
                return task(control);
            } catch (const LocalFiles::Cancelled &) {
                return QString("Cancelled");
            } catch (const std::exception &error) {
                return QString::fromLocal8Bit(error.what());
            }
        },
        [this, text, stop, timer](QString result) {
            text->setPlainText(result);
            stop->setEnabled(false);
            timer->stop();
            emit ready();
        });
}
ReportDialog::~ReportDialog() { control_->cancelled = true; }
ArchiveDialog::ArchiveDialog(const std::filesystem::path &path, const std::filesystem::path &destination,
                             JobQueue *queue, QWidget *parent)
    : QDialog(parent), path_(path), destination_(destination), queue_(queue),
      control_(std::make_shared<LocalFiles::Control>()), filter_(new QLineEdit(this)),
      table_(new QTableWidget(this)), status_(new QLabel("Reading archive…", this)),
      all_(new QPushButton("Extract all to new folder…", this)),
      selected_(new QPushButton("Extract selected…", this)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Archive — " + displayPath(path));
    resize(1000, 600);
    auto *layout = new QVBoxLayout(this);
    filter_->setPlaceholderText("Filter archive paths");
    filter_->setClearButtonEnabled(true);
    layout->addWidget(filter_);
    table_->setObjectName("archiveEntries");
    setupTable(table_, {"Path", "Bytes", "Type", "Extraction restriction"}, 0);
    table_->setColumnWidth(1, 110);
    table_->setColumnWidth(2, 120);
    table_->setColumnWidth(3, 260);
    layout->addWidget(table_);
    layout->addWidget(status_);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(all_);
    buttons->addWidget(selected_);
    layout->addLayout(buttons);
    all_->setObjectName("extractAll");
    selected_->setObjectName("extractSelected");
    all_->setEnabled(false);
    selected_->setEnabled(false);
    connect(filter_, &QLineEdit::textChanged, this, &ArchiveDialog::populate);
    connect(all_, &QPushButton::clicked, this, [this] { extract(false); });
    connect(selected_, &QPushButton::clicked, this, [this] { extract(true); });
    background<Archives::Listing>(
        this, [path, control = control_] { return Archives::list(path, control); },
        [this](Archives::Listing listing) {
            listing_ = std::move(listing);
            populate();
            const bool complete = listing_.error.isEmpty() && !listing_.cancelled;
            all_->setEnabled(complete);
            selected_->setEnabled(complete);
            status_->setText(complete ? QString("%1 entries · archives containing links or special entries "
                                                "cannot be extracted · view limited to 10000 rows")
                                            .arg(listing_.entries.size())
                             : listing_.cancelled ? "Cancelled"
                                                  : listing_.error);
            if (!listing_.warning.isEmpty())
                status_->setText(status_->text() + "\n" + listing_.warning);
        });
}
ArchiveDialog::~ArchiveDialog() { control_->cancelled = true; }
void ArchiveDialog::populate() {
    table_->setRowCount(0);
    for (size_t index = 0; index < listing_.entries.size() && table_->rowCount() < 10000; ++index) {
        const auto &entry = listing_.entries[index];
        const auto name = displayPath(std::filesystem::path(entry.name));
        if (!name.contains(filter_->text(), Qt::CaseInsensitive))
            continue;
        const auto row = table_->rowCount();
        table_->insertRow(row);
        const QStringList values{name, QString::number(entry.size),
                                 entry.directory ? "Directory" : "File / other", entry.unsafe};
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values[column]);
            item->setToolTip(values[column]);
            item->setData(Qt::UserRole, static_cast<qulonglong>(index));
            table_->setItem(row, column, item);
        }
    }
}
void ArchiveDialog::extract(bool selected) {
    std::vector<std::string> names;
    if (selected) {
        for (const auto &index : table_->selectionModel()->selectedRows())
            names.push_back(
                listing_.entries.at(table_->item(index.row(), 0)->data(Qt::UserRole).toULongLong()).name);
        if (names.empty()) {
            status_->setText("Select archive entries first");
            return;
        }
    }
    auto destination = destination_ / (path_.stem().native() + "-unpacked");
    bool ok = false;
    const auto text = QInputDialog::getText(this, "Extract archive",
                                            "New destination folder (must not exist):", QLineEdit::Normal,
                                            displayPath(destination), &ok);
    if (!ok)
        return;
    if (text != displayPath(destination))
        destination = nativePath(text);
    const auto path = path_;
    QPointer<ArchiveDialog> self(this);
    queue_->enqueue(
        "Extract " + displayPath(path.filename()),
        [path, destination, names](const auto &control) {
            return Archives::extract(path, destination, names, control);
        },
        [self](const auto &result) {
            if (self)
                self->status_->setText(result.cancelled ? "Extraction cancelled; see background operations"
                                       : result.items.empty() || !result.items.front().error.isEmpty()
                                           ? "Extraction failed; see background operations"
                                           : "Extraction completed");
        });
    status_->setText("Extraction queued · inspect progress in Tools → Background operations");
}
RenameDialog::RenameDialog(const std::vector<std::filesystem::path> &paths, JobQueue *queue,
                           const QString &journalDirectory, QWidget *parent)
    : QDialog(parent), paths_(paths), queue_(queue), journalDirectory_(journalDirectory),
      pattern_(new QLineEdit("[N]", this)), extension_(new QLineEdit("[E]", this)),
      find_(new QLineEdit(this)), replace_(new QLineEdit(this)),
      regex_(new QCheckBox("Regular expression", this)), case_(new QComboBox(this)),
      start_(new QSpinBox(this)), step_(new QSpinBox(this)), width_(new QSpinBox(this)),
      table_(new QTableWidget(this)), status_(new QLabel(this)),
      apply_(new QPushButton("Apply preview", this)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Multi-Rename Tool");
    resize(707, 434);
    setMinimumWidth(707);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *top = new QWidget(this);
    top->setFixedHeight(124);
    top->setMinimumWidth(707);
    layout->addWidget(top);
    const auto reference = referenceDialogMetadata("MultiRename");
    const QSet<QString> active{"W_RenameBox",     "W_RenameExtBox", "W_SearchEdit",   "W_ReplaceWithEdit",
                               "RegExBox",        "UpperLowerBox",  "CounterStartAt", "CounterStepBy",
                               "CounterWidthBox", "AddName",        "AddCounter",     "AddExt",
                               "AddCounterE",     "RegExHelp",      "AddRangeN",      "AddRangeE",
                               "AddRangeOther",   "AddDate",        "AddTime",        "AddOther",
                               "MoreButton",      "PluginButton",   "RespectCaseBox", "AlsoExtensionBox"};
    for (const auto &child : reference.value("children").toArray())
        if (child.toObject().value("name") == "Panel1") {
            const auto node = child.toObject();
            for (const auto &control : node.value("children").toArray())
                if (control.toObject().value("name") != "HeaderClick1")
                    renderReferenceControls(control.toObject(), top, active);
        }
    const auto comboEditor = [top](const QString &name, QLineEdit *edit) {
        auto *combo = top->findChild<QComboBox *>(name);
        combo->setEditable(true);
        combo->setLineEdit(edit);
    };
    comboEditor("W_RenameBox", pattern_);
    comboEditor("W_SearchEdit", find_);
    comboEditor("W_ReplaceWithEdit", replace_);
    const auto replaceControl = [top](const QString &name, QWidget *replacement) {
        auto *control = top->findChild<QWidget *>(name);
        const auto geometry = control->geometry();
        replacement->setParent(control->parentWidget());
        replacement->setGeometry(geometry);
        control->deleteLater();
        replacement->show();
    };
    replaceControl("W_RenameExtBox", extension_);
    replaceControl("RegExBox", regex_);
    regex_->setText("RegE&x");
    replaceControl("UpperLowerBox", case_);
    case_->addItems({"Unchanged", "All lowercase", "All UPPERCASE", "First letter uppercase",
                     "First of each word uppercase"});
    start_->setRange(0, 1000000000);
    start_->setValue(1);
    step_->setRange(1, 1000000);
    width_->setRange(1, 12);
    width_->setValue(3);
    width_->hide();
    replaceControl("CounterStartAt", start_);
    start_->resize(start_->width() + 17, start_->height());
    replaceControl("CounterStepBy", step_);
    step_->resize(step_->width() + 17, step_->height());
    auto *digits = top->findChild<QComboBox *>("CounterWidthBox");
    digits->setObjectName("CounterWidthBox");
    digits->setEditable(false);
    digits->setCurrentText("3");
    connect(width_, &QSpinBox::valueChanged, digits, [digits](int value) {
        if (digits->currentText() != "a")
            digits->setCurrentText(QString::number(value));
    });
    connect(digits, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        bool ok = false;
        const auto value = text.toInt(&ok);
        if (ok)
            width_->setValue(value);
    });
    for (const auto &name : {QString("internalzip_scroller"), QString("ScrollBar1")})
        if (auto *control = top->findChild<QWidget *>(name))
            control->hide();
    if (auto *respect = top->findChild<QCheckBox *>("RespectCaseBox"))
        respect->setChecked(true);
    if (auto *extensionReplace = top->findChild<QCheckBox *>("AlsoExtensionBox"))
        extensionReplace->setChecked(false);
    pattern_->setObjectName("renamePattern");
    extension_->setObjectName("renameExtension");
    const auto insert = [top](const QString &name, QLineEdit *edit, const QString &token) {
        auto *button = top->findChild<QToolButton *>(name);
        QObject::connect(button, &QToolButton::clicked, edit, [edit, token] {
            edit->insert(token);
            edit->setFocus();
        });
    };
    insert("AddName", pattern_, "[N]");
    insert("AddCounter", pattern_, "[C]");
    insert("AddExt", extension_, "[E]");
    insert("AddCounterE", extension_, "[C]");
    insert("AddDate", pattern_, "[YMD]");
    insert("AddTime", pattern_, "[hms]");
    const auto range = [this, top](const QString &button, QLineEdit *field, const QString &prefix) {
        connect(top->findChild<QToolButton *>(button), &QToolButton::clicked, this, [this, field, prefix] {
            bool ok = false;
            const auto text =
                QInputDialog::getText(this, "Insert filename range", "Range (1-based): 2-5, 2,5 or -5-",
                                      QLineEdit::Normal, "1-3", &ok);
            if (ok && QRegularExpression("^-?[1-9][0-9]*(?:[-,]-?[1-9][0-9]*)?-?$").match(text).hasMatch())
                field->insert("[" + prefix + text + "]");
        });
    };
    range("AddRangeN", pattern_, "N");
    range("AddRangeE", extension_, "E");
    range("AddRangeOther", pattern_, "");
    connect(top->findChild<QToolButton *>("AddOther"), &QToolButton::clicked, this, [this] {
        bool ok = false;
        const auto token = QInputDialog::getItem(
            this, "Insert mask",
            "Field:", {"[P]", "[S]", "[Y]", "[M]", "[D]", "[h]", "[m]", "[s]", "[[]", "[]]"}, 0, false, &ok);
        if (ok)
            pattern_->insert(token);
    });
    connect(top->findChild<QToolButton *>("PluginButton"), &QToolButton::clicked, this, [this] {
        QStringList fields{"comment", "parent", "size"};
        for (const auto &value : NativeSettings::plugins("content"))
            fields << value.toMap().value("name").toString();
        bool ok = false;
        const auto name =
            QInputDialog::getItem(this, "Content field", "Native field:", fields, 0, false, &ok);
        if (ok)
            pattern_->insert("[=" + name + "]");
    });
    auto *presets = top->findChild<QToolButton *>("MoreButton");
    if (presets)
        connect(presets, &QToolButton::clicked, this, [this, presets] {
            auto *menu = new QMenu(this);
            menu->setAttribute(Qt::WA_DeleteOnClose);
            const auto all = QSettings().value("renamePresets").toMap();
            for (auto it = all.begin(); it != all.end(); ++it) {
                auto *action = menu->addAction(it.key());
                connect(action, &QAction::triggered, this, [this, data = it.value().toMap()] {
                    pattern_->setText(data.value("name").toString());
                    extension_->setText(data.value("extension").toString());
                    find_->setText(data.value("find").toString());
                    replace_->setText(data.value("replace").toString());
                    regex_->setChecked(data.value("regex").toBool());
                    case_->setCurrentIndex(data.value("case").toInt());
                    start_->setValue(data.value("start", 1).toInt());
                    step_->setValue(data.value("step", 1).toInt());
                    findChild<QComboBox *>("CounterWidthBox")
                        ->setCurrentText(data.value("digits", 3).toString());
                    findChild<QCheckBox *>("RespectCaseBox")
                        ->setChecked(data.value("respectCase", true).toBool());
                    findChild<QCheckBox *>("AlsoExtensionBox")
                        ->setChecked(data.value("extensionReplace", false).toBool());
                });
            }
            menu->addSeparator();
            connect(menu->addAction("Save current settings…"), &QAction::triggered, this, [this] {
                bool ok = false;
                const auto name =
                    QInputDialog::getText(this, "Save rename preset", "Name:", QLineEdit::Normal, {}, &ok)
                        .trimmed();
                if (!ok || name.isEmpty())
                    return;
                QSettings settings;
                auto all = settings.value("renamePresets").toMap();
                all.insert(name,
                           QVariantMap{{"name", pattern_->text()},
                                       {"extension", extension_->text()},
                                       {"find", find_->text()},
                                       {"replace", replace_->text()},
                                       {"regex", regex_->isChecked()},
                                       {"case", case_->currentIndex()},
                                       {"start", start_->value()},
                                       {"step", step_->value()},
                                       {"digits", findChild<QComboBox *>("CounterWidthBox")->currentText()},
                                       {"respectCase", findChild<QCheckBox *>("RespectCaseBox")->isChecked()},
                                       {"extensionReplace",
                                        findChild<QCheckBox *>("AlsoExtensionBox")->isChecked()}});
                settings.setValue("renamePresets", all);
            });
            menu->popup(presets->mapToGlobal(QPoint(0, presets->height())));
        });
    if (auto *help = top->findChild<QPushButton *>("RegExHelp"))
        connect(help, &QPushButton::clicked, this, [this] {
            QMessageBox::information(
                this, "Multi-Rename Tool",
                "Supported mask tokens: [N] name, [E] extension, [C] counter.\nName/extension/path ranges, "
                "file modification date/time, parent name, size, native content fields [=field], presets and "
                "guarded undo are available. Windows DLL plugins are not supported.");
        });
    setupTable(table_, {"Old name", "Ext.", "New name", "Size", "Date", "Location"}, 2);
    table_->setObjectName("renamePreview");
    table_->verticalHeader()->hide();
    table_->setShowGrid(false);
    table_->horizontalHeader()->setFixedHeight(19);
    table_->verticalHeader()->setDefaultSectionSize(19);
    table_->setColumnWidth(0, 140);
    table_->setColumnWidth(1, 40);
    table_->setColumnWidth(3, 65);
    table_->setColumnWidth(4, 110);
    table_->setColumnWidth(5, 110);
    layout->addWidget(table_, 1);
    auto *bottom = new QHBoxLayout;
    bottom->setContentsMargins(4, 2, 4, 2);
    bottom->setSpacing(2);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    bottom->addWidget(status_, 1);
    auto *next = new QPushButton(">", this);
    next_ = next;
    next->setObjectName("renameNext");
    next->setFixedWidth(23);
    next->setEnabled(false);
    bottom->addWidget(next);
    apply_->setText("&Start!");
    apply_->setObjectName("applyRename");
    apply_->setFixedWidth(80);
    bottom->addWidget(apply_);
    auto *undo = new QPushButton("&Undo", this);
    undo_ = undo;
    undo->setObjectName("renameUndo");
    undo->setEnabled(false);
    undo->setFixedWidth(80);
    bottom->addWidget(undo);
    auto *result = new QPushButton("Result &list", this);
    result->setFixedWidth(80);
    bottom->addWidget(result);
    connect(result, &QPushButton::clicked, this, [this] {
        QStringList rows;
        for (const auto &item : plan_.items)
            rows << displayPath(item.source) + " → " + displayPath(item.target);
        QApplication::clipboard()->setText(rows.join('\n'));
    });
    auto *close = new QPushButton("Close", this);
    close->setFixedWidth(80);
    bottom->addWidget(close);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    layout->addLayout(bottom);
    apply_->setEnabled(false);
    auto *delay = new QTimer(this);
    delay->setSingleShot(true);
    delay->setInterval(180);
    const auto changed = [this, delay] {
        ++generation_;
        apply_->setEnabled(false);
        delay->start();
    };
    for (auto *edit : {pattern_, extension_, find_, replace_})
        connect(edit, &QLineEdit::textChanged, this, changed);
    connect(regex_, &QCheckBox::toggled, this, changed);
    connect(case_, &QComboBox::currentIndexChanged, this, changed);
    for (auto *spin : {start_, step_, width_})
        connect(spin, &QSpinBox::valueChanged, this, changed);
    connect(digits, &QComboBox::currentTextChanged, this, [changed](const QString &) { changed(); });
    for (const auto &name : {QString("RespectCaseBox"), QString("AlsoExtensionBox")}) {
        auto *check = findChild<QCheckBox *>(name);
        check->setEnabled(true);
        check->setToolTip({});
        connect(check, &QCheckBox::toggled, this, changed);
    }
    connect(delay, &QTimer::timeout, this, &RenameDialog::preview);
    connect(next_, &QPushButton::clicked, this, [this] {
        if (busy_)
            return;
        pattern_->setText("[N]");
        extension_->setText("[E]");
        find_->clear();
        replace_->clear();
        next_->setEnabled(false);
        preview();
    });
    connect(apply_, &QPushButton::clicked, this, [this] {
        if (busy_)
            return;
        busy_ = true;
        ++generation_;
        apply_->setEnabled(false);
        undo_->setEnabled(false);
        next_->setEnabled(false);
        const auto plan = plan_;
        const auto journal = journalDirectory_;
        auto inverse = std::make_shared<BatchRename::Plan>();
        QPointer<RenameDialog> self(this);
        queue_->enqueue(
            "Batch rename",
            [plan, journal, inverse](const auto &control) {
                return BatchRename::apply(plan, control, journal, inverse);
            },
            [self, inverse, plan](const auto &result) {
                if (!self)
                    return;
                self->busy_ = false;
                const bool ok =
                    !result.cancelled && std::none_of(result.items.begin(), result.items.end(),
                                                      [](const auto &item) { return !item.error.isEmpty(); });
                self->status_->setText(ok ? "Rename complete" : "Stopped; inspect background operations");
                if (ok) {
                    self->paths_.clear();
                    for (const auto &item : plan.items)
                        self->paths_.push_back(item.target);
                    self->inverse_ = inverse;
                    self->undo_->setEnabled(!inverse->items.empty());
                    self->next_->setEnabled(true);
                }
            });
        status_->setText("Queued");
    });
    connect(undo_, &QPushButton::clicked, this, [this] {
        if (busy_ || !inverse_ || inverse_->items.empty())
            return;
        busy_ = true;
        ++generation_;
        apply_->setEnabled(false);
        undo_->setEnabled(false);
        next_->setEnabled(false);
        const auto plan = *inverse_;
        const auto journal = journalDirectory_;
        QPointer<RenameDialog> self(this);
        queue_->enqueue(
            "Undo batch rename",
            [plan, journal](const auto &control) { return BatchRename::apply(plan, control, journal); },
            [self, plan](const auto &result) {
                if (!self)
                    return;
                self->busy_ = false;
                const bool ok =
                    !result.cancelled && std::none_of(result.items.begin(), result.items.end(),
                                                      [](const auto &item) { return !item.error.isEmpty(); });
                self->status_->setText(ok ? "Original names restored"
                                          : "Undo refused or failed; inspect background operations");
                if (ok) {
                    self->paths_.clear();
                    for (const auto &item : plan.items)
                        self->paths_.push_back(item.target);
                    self->pattern_->setText("[N]");
                    self->extension_->setText("[E]");
                    self->preview();
                }
            });
    });
    preview();
}

void RenameDialog::preview() {
    if (previewCancellation_)
        previewCancellation_->store(true);
    previewCancellation_ = std::make_shared<std::atomic_bool>(false);
    apply_->setEnabled(false);
    const auto generation = ++generation_;
    BatchRename::Rule rule{pattern_->text(),
                           find_->text(),
                           replace_->text(),
                           regex_->isChecked(),
                           static_cast<BatchRename::LetterCase>(case_->currentIndex()),
                           start_->value(),
                           step_->value(),
                           width_->value()};
    rule.commanderMasks = !pattern_->text().contains("{ext}");
    rule.extensionPattern = extension_->text();
    rule.cancellation = previewCancellation_;
    rule.autoWidth = findChild<QComboBox *>("CounterWidthBox")->currentText() == "a";
    rule.caseSensitiveFind = findChild<QCheckBox *>("RespectCaseBox")->isChecked();
    rule.replaceExtension = findChild<QCheckBox *>("AlsoExtensionBox")->isChecked();
    background<BatchRename::Plan>(
        this, [paths = paths_, rule] { return BatchRename::preview(paths, rule); },
        [this, generation](BatchRename::Plan plan) {
            if (generation != generation_)
                return;
            plan_ = std::move(plan);
            table_->setRowCount(static_cast<int>(std::min<size_t>(plan_.items.size(), 10000)));
            bool changed = false;
            for (int row = 0; row < table_->rowCount(); ++row) {
                const auto &item = plan_.items[row];
                changed = changed || item.source != item.target;
                const auto name = displayPath(item.source.filename());
                const auto dot = name.lastIndexOf('.');
                const QStringList values{
                    dot > 0 ? name.left(dot) : name,
                    dot > 0 ? name.mid(dot + 1) : QString(),
                    displayPath(item.target.filename()),
                    QString::number(item.before.st_size),
                    QDateTime::fromSecsSinceEpoch(item.before.st_mtime).toString("yyyy-MM-dd HH:mm"),
                    displayPath(item.source.parent_path())};
                for (int column = 0; column < values.size(); ++column)
                    table_->setItem(row, column, new QTableWidgetItem(values[column]));
            }
            status_->setText(plan_.errors.empty()
                                 ? QString("%1 entries · failed batches attempt to restore original names")
                                       .arg(plan_.items.size())
                                 : plan_.errors.join('\n'));
            status_->setToolTip(status_->text());
            apply_->setEnabled(!busy_ && plan_.errors.empty() && changed);
        });
}
CompareDialog::CompareDialog(const std::filesystem::path &left, const std::filesystem::path &right,
                             JobQueue *queue, const QString &journalDirectory, QWidget *parent)
    : QDialog(parent), left_(left), right_(right), queue_(queue), journalDirectory_(journalDirectory),
      contents_(new QCheckBox("Compare file contents", this)),
      equal_(new QCheckBox("Show equal entries", this)), table_(new QTableWidget(this)),
      status_(new QLabel(this)), scan_(new QPushButton("Compare", this)),
      apply_(new QPushButton("Copy checked left → right…", this)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Compare directories / one-way synchronize");
    resize(1050, 650);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Left: " + displayPath(left), this));
    layout->addWidget(new QLabel("Right: " + displayPath(right), this));
    auto *options = new QHBoxLayout;
    contents_->setChecked(true);
    options->addWidget(contents_);
    options->addWidget(equal_);
    options->addWidget(scan_);
    layout->addLayout(options);
    table_->setObjectName("directoryComparison");
    setupTable(table_, {"Copy", "Relative path", "State", "Left bytes", "Right bytes"}, 1);
    table_->setColumnWidth(0, 60);
    table_->setColumnWidth(2, 205);
    table_->setColumnWidth(3, 110);
    table_->setColumnWidth(4, 110);
    layout->addWidget(table_);
    layout->addWidget(status_);
    auto *buttons = new QHBoxLayout;
    auto *check = new QPushButton("Check copyable rows", this),
         *clear = new QPushButton("Clear checks", this), *stop = new QPushButton("Stop scan", this);
    for (auto *button : {check, clear, stop, apply_})
        buttons->addWidget(button);
    layout->addLayout(buttons);
    connect(scan_, &QPushButton::clicked, this, &CompareDialog::scan);
    connect(equal_, &QCheckBox::toggled, this, &CompareDialog::populate);
    connect(contents_, &QCheckBox::toggled, this, [this] {
        apply_->setEnabled(false);
        status_->setText("Comparison mode changed; compare again before applying");
    });
    connect(stop, &QPushButton::clicked, this, [this] {
        if (control_)
            control_->cancelled = true;
    });
    connect(check, &QPushButton::clicked, this, [this] {
        for (size_t i = 0; i < checked_.size(); ++i)
            checked_[i] = plan_.rows[i].actionable;
        populate();
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        std::fill(checked_.begin(), checked_.end(), false);
        populate();
    });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item->column() == 0 && static_cast<size_t>(item->row()) < rows_.size())
            checked_[rows_[item->row()]] = item->checkState() == Qt::Checked;
    });
    connect(apply_, &QPushButton::clicked, this, &CompareDialog::apply);
    scan_->setObjectName("compareScan");
    apply_->setObjectName("applySync");
    scan();
}
CompareDialog::~CompareDialog() {
    if (control_)
        control_->cancelled = true;
}
void CompareDialog::scan() {
    if (!scan_->isEnabled())
        return;
    scan_->setEnabled(false);
    contents_->setEnabled(false);
    apply_->setEnabled(false);
    status_->setText(
        "Comparing… permission-denied subdirectories may be omitted; errors prevent synchronization");
    control_ = std::make_shared<LocalFiles::Control>();
    const bool contents = contents_->isChecked();
    background<FileTools::DirectoryComparison>(
        this,
        [left = left_, right = right_, contents, control = control_] {
            return FileTools::compareDirectories(left, right, contents, control);
        },
        [this](FileTools::DirectoryComparison plan) {
            plan_ = std::move(plan);
            checked_.assign(plan_.rows.size(), false);
            for (size_t i = 0; i < checked_.size(); ++i)
                checked_[i] = plan_.rows[i].actionable;
            scan_->setEnabled(true);
            contents_->setEnabled(true);
            apply_->setEnabled(!plan_.cancelled && !plan_.truncated && plan_.errors.empty());
            status_->setText(QString("%1 entries · %2 errors%3%4 · right-only entries will be retained")
                                 .arg(plan_.rows.size())
                                 .arg(plan_.errors.size())
                                 .arg(plan_.cancelled ? " · cancelled" : "")
                                 .arg(plan_.truncated ? " · truncated" : ""));
            status_->setToolTip(plan_.errors.join('\n'));
            populate();
        });
}
void CompareDialog::populate() {
    const QSignalBlocker blocker(table_);
    rows_.clear();
    for (size_t index = 0; index < plan_.rows.size() && rows_.size() < 10000; ++index)
        if (equal_->isChecked() || plan_.rows[index].difference != FileTools::Difference::Equal)
            rows_.push_back(index);
    table_->setRowCount(rows_.size());
    for (size_t row = 0; row < rows_.size(); ++row) {
        const auto index = rows_[row];
        const auto &item = plan_.rows[index];
        auto *check = new QTableWidgetItem;
        check->setFlags(item.actionable ? Qt::ItemIsEnabled | Qt::ItemIsUserCheckable : Qt::NoItemFlags);
        check->setCheckState(checked_[index] ? Qt::Checked : Qt::Unchecked);
        if (!item.actionable)
            check->setToolTip("Only left-only directories and left-side regular files can be copied; links "
                              "and type conflicts require manual handling.");
        table_->setItem(row, 0, check);
        const QStringList values{displayPath(item.relative), FileTools::differenceText(item.difference),
                                 QString::number(item.left.st_size), QString::number(item.right.st_size)};
        for (int column = 0; column < values.size(); ++column)
            table_->setItem(row, column + 1, new QTableWidgetItem(values[column]));
    }
}
void CompareDialog::apply() {
    std::vector<size_t> selected;
    // Only rows actually presented in the preview can be approved.
    for (const auto index : rows_)
        if (checked_[index])
            selected.push_back(index);
    if (selected.empty()) {
        status_->setText("Check entries to copy first");
        return;
    }
    if (QMessageBox::question(
            this, "Apply one-way synchronization",
            QString(
                "Copy/create %1 checked entries from left to right?\nDifferent regular files will be "
                "replaced. Required parent directories will be created. Right-only entries will be retained.")
                .arg(selected.size()),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    const auto plan = plan_;
    const auto journal = journalDirectory_;
    QPointer<CompareDialog> self(this);
    apply_->setEnabled(false);
    queue_->enqueue(
        "One-way synchronization",
        [plan, selected, journal](const auto &control) {
            return FileTools::synchronize(plan, selected, control, journal);
        },
        [self](const auto &) {
            if (self)
                self->status_->setText(
                    "Job finished; inspect background operations, then compare again before another apply");
        });
}

RenameDialog::~RenameDialog() {
    if (previewCancellation_)
        previewCancellation_->store(true);
}

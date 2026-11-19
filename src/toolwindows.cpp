#include "toolwindows.h"
#include "backgroundpool.h"
#include "directorymodel.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHeaderView>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <array>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
struct ReadResult {
    QByteArray bytes;
    QString error;
};
ReadResult readDocument(const std::filesystem::path &path, int limit) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return {{}, QString::fromLocal8Bit(std::strerror(errno))};
    QFile file;
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        return {{}, file.errorString()};
    }
    struct stat st {};
    if (::fstat(fd, &st) || !S_ISREG(st.st_mode))
        return {{}, "Only regular files can be viewed or edited"};
    if (st.st_size > limit)
        return {{}, "Built-in viewer/editor size limit exceeded"};
    auto bytes = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError)
        return {{}, file.errorString()};
    if (bytes.size() > limit)
        return {{}, "File grew beyond the configured size limit"};
    return {bytes, {}};
}
} // namespace
FileWindow::FileWindow(const std::filesystem::path &path, bool editable, QWidget *parent)
    : QDialog(parent), path_(path), editable_(editable), text_(new QPlainTextEdit(this)),
      info_(new QLabel("Loading…", this)), image_(new QLabel(this)), mode_(new QComboBox(this)) {
    QImageReader::setAllocationLimit(64);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle((editable ? "Edit — " : "View — ") + displayPath(path));
    resize(900, 650);
    auto *layout = new QVBoxLayout(this);
    auto *tools = new QHBoxLayout;
    mode_->addItems({"UTF-8 text", "Hex", "Image"});
    mode_->setEnabled(!editable);
    tools->addWidget(mode_);
    auto *find = new QLineEdit(this);
    find->setPlaceholderText("Find text · Enter finds next");
    find->setAccessibleName("Find in file");
    tools->addWidget(find);
    auto *saveButton = new QPushButton("Save (Ctrl+S)", this);
    saveButton->setVisible(editable);
    tools->addWidget(saveButton);
    layout->addLayout(tools);
    text_->setReadOnly(true);
    text_->setLineWrapMode(QSettings().value("viewer/wrap").toBool() ? QPlainTextEdit::WidgetWidth
                                                                     : QPlainTextEdit::NoWrap);
    text_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text_->setAccessibleName(editable ? "File editor" : "File contents");
    layout->addWidget(text_);
    image_->setAlignment(Qt::AlignCenter);
    image_->hide();
    layout->addWidget(image_);
    layout->addWidget(info_);
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] {
        if (ready_)
            render();
    });
    connect(find, &QLineEdit::returnPressed, this, [this, find] {
        if (!text_->find(find->text())) {
            auto cursor = text_->textCursor();
            cursor.movePosition(QTextCursor::Start);
            text_->setTextCursor(cursor);
            text_->find(find->text());
        }
    });
    connect(saveButton, &QPushButton::clicked, this, &FileWindow::save);
    auto *saveKey = new QShortcut(QKeySequence::Save, this);
    connect(saveKey, &QShortcut::activated, this, &FileWindow::save);
    auto *watcher = new QFutureWatcher<ReadResult>(this);
    connect(watcher, &QFutureWatcher<ReadResult>::finished, this, [this, watcher, saveButton] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (!result.error.isEmpty()) {
            info_->setText(result.error);
            return;
        }
        original_ = result.bytes;
        ready_ = true;
        if (editable_ && (original_.contains('\0') || QString::fromUtf8(original_).toUtf8() != original_)) {
            editable_ = false;
            mode_->setEnabled(true);
            saveButton->hide();
            setWindowTitle("View — " + displayPath(path_));
            render();
            info_->setText("Editing requires lossless UTF-8 text without NUL bytes");
            return;
        }
        if (!editable_ && !QImage::fromData(original_).isNull())
            mode_->setCurrentIndex(2);
        render();
    });
    watcher->setFuture(QtConcurrent::run(
        ioPool(), readDocument, path,
        (editable ? 8 : qBound(1, QSettings().value("viewer/maxMiB", 8).toInt(), 64)) * 1024 * 1024));
}
void FileWindow::render() {
    text_->setVisible(mode_->currentIndex() != 2);
    image_->setVisible(mode_->currentIndex() == 2);
    if (mode_->currentIndex() == 2) {
        const auto decoded = QImage::fromData(original_);
        image_->setPixmap(
            QPixmap::fromImage(decoded).scaled(820, 510, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        info_->setText(decoded.isNull()
                           ? "Unsupported image"
                           : QString("%1 × %2 pixels").arg(decoded.width()).arg(decoded.height()));
    } else if (mode_->currentIndex() == 1) {
        // Rendering is capped independently from the readable file size.
        QString hex;
        const auto bytes = original_.left(256 * 1024);
        for (qsizetype offset = 0; offset < bytes.size(); offset += 16) {
            const auto row = bytes.mid(offset, 16);
            QString ascii;
            for (const auto byte : row)
                ascii += byte >= 32 && byte < 127 ? QChar(byte) : QChar('.');
            hex += QString("%1  %2  %3\n")
                       .arg(offset, 8, 16, QChar('0'))
                       .arg(QString::fromLatin1(row.toHex(' ')), -47)
                       .arg(ascii);
        }
        text_->setPlainText(hex);
        info_->setText(QString("%1 bytes · hex preview limited to 256 KiB").arg(original_.size()));
    } else {
        text_->setPlainText(QString::fromUtf8(original_));
        text_->setReadOnly(!editable_);
        text_->document()->setModified(false);
        info_->setText(QString("%1 bytes · UTF-8%2")
                           .arg(original_.size())
                           .arg(editable_ ? " · editable" : " · read only"));
    }
}
void FileWindow::save() {
    if (!editable_ || !ready_ || saving_ || !text_->document()->isModified())
        return;
    saving_ = true;
    auto bytes = text_->toPlainText().toUtf8();
    // Preserve a consistent CRLF document's newline convention.
    if (original_.contains("\r\n") && !QByteArray(original_).replace("\r\n", "").contains('\n'))
        bytes.replace("\n", "\r\n");
    const auto path = path_;
    auto *watcher = new QFutureWatcher<ReadResult>(this);
    text_->setReadOnly(true);
    const auto original = original_;
    connect(watcher, &QFutureWatcher<ReadResult>::finished, this, [this, watcher, bytes] {
        const auto result = watcher->result();
        watcher->deleteLater();
        saving_ = false;
        text_->setReadOnly(false);
        if (!result.error.isEmpty()) {
            QMessageBox::warning(this, "Save failed", result.error);
            return;
        }
        original_ = bytes;
        text_->document()->setModified(false);
        info_->setText("Saved atomically");
    });
    watcher->setFuture(QtConcurrent::run(ioPool(), [path, original, bytes]() -> ReadResult {
        return {{}, LocalFiles::saveFile(path, original, bytes)};
    }));
}
bool FileWindow::confirmClose() {
    if (saving_) {
        info_->setText("Wait for the save to finish before closing");
        return false;
    }
    return !editable_ || !text_->document()->isModified() ||
           QMessageBox::question(this, "Unsaved changes", "Discard unsaved changes?",
                                 QMessageBox::Discard | QMessageBox::Cancel,
                                 QMessageBox::Cancel) == QMessageBox::Discard;
}
void FileWindow::reject() {
    if (confirmClose())
        QDialog::reject();
}
void FileWindow::closeEvent(QCloseEvent *event) {
    if (confirmClose())
        event->accept();
    else
        event->ignore();
}

SearchDialog::SearchDialog(const std::filesystem::path &root, QWidget *parent)
    : QDialog(parent), root_(root), pattern_(new QLineEdit("*", this)), content_(new QLineEdit(this)),
      recursive_(new QCheckBox("Include subdirectories", this)),
      hidden_(new QCheckBox("Include hidden entries", this)), case_(new QCheckBox("Case sensitive", this)),
      results_(new QTableWidget(this)), status_(new QLabel("Search " + displayPath(root), this)),
      start_(new QPushButton("Search", this)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Find files");
    resize(760, 560);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto *top = new QHBoxLayout;
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName("searchTabs");
    auto *general = new QWidget(tabs), *advanced = new QWidget(tabs), *plugins = new QWidget(tabs),
         *profilesPage = new QWidget(tabs);
    tabs->addTab(general, "General");
    tabs->addTab(advanced, "Advanced");
    tabs->addTab(plugins, "Plugins");
    tabs->addTab(profilesPage, "Load/Save");
    auto *generalLayout = new QVBoxLayout(general);
    auto *advancedLayout = new QVBoxLayout(advanced);
    auto *profilesLayout = new QVBoxLayout(profilesPage);
    auto *pluginsLayout = new QVBoxLayout(plugins);
    auto *pluginOption = new QCheckBox("Search in plugins", plugins);
    pluginOption->setEnabled(true);
    pluginOption->setObjectName("searchNativeFields");
    pluginOption->setToolTip("Filter by a native content field using a bounded executable provider; Windows "
                             "DLL plugins are unsupported.");
    pluginsLayout->addWidget(pluginOption);
    auto *field = new QLineEdit("comment", plugins);
    field->setObjectName("searchFieldName");
    field->setPlaceholderText("Native field name: comment, parent, size, or configured provider");
    pluginsLayout->addWidget(field);
    auto *value = new QLineEdit(plugins);
    value->setObjectName("searchFieldValue");
    value->setPlaceholderText("Field contains");
    pluginsLayout->addWidget(value);
    pluginsLayout->addStretch();
    top->addWidget(tabs, 1);
    auto *side = new QVBoxLayout;
    start_->setText("&Start search");
    side->addWidget(start_);
    auto *cancel = new QPushButton("Cancel", this);
    side->addWidget(cancel);
    auto *help = new QPushButton("Help", this);
    side->addWidget(help);
    side->addStretch();
    top->addLayout(side);
    layout->addLayout(top);
    connect(help, &QPushButton::clicked, this, [this] {
        QMessageBox::information(
            this, "Find Files",
            "Search file names and UTF-8 contents. Advanced contains byte-size and "
            "modification-date limits. Native content-field filtering is available on the Plugins tab.");
    });
    auto *form = new QFormLayout;
    start_->setObjectName("searchStart");
    results_->setObjectName("searchResults");
    pattern_->setObjectName("searchPattern");
    content_->setObjectName("searchContent");
    form->addRow("Search &for:", pattern_);
    auto *searchRoot = new QLineEdit(displayPath(root), this);
    searchRoot->setReadOnly(true);
    searchRoot->setObjectName("searchRoot");
    form->addRow("Search &in:", searchRoot);
    form->addRow("Find &text (UTF-8):", content_);
    generalLayout->addLayout(form);
    recursive_->setChecked(true);
    auto *options = new QHBoxLayout;
    for (auto *box : {recursive_, hidden_, case_})
        options->addWidget(box);
    generalLayout->addLayout(options);
    nameRegex_ = new QCheckBox("Name is regular expression", this);
    contentRegex_ = new QCheckBox("Content is regular expression", this);
    auto *regexRow = new QHBoxLayout;
    regexRow->addWidget(nameRegex_);
    regexRow->addWidget(contentRegex_);
    generalLayout->addLayout(regexRow);
    generalLayout->addStretch();
    minBytes_ = new QLineEdit(this);
    maxBytes_ = new QLineEdit(this);
    minBytes_->setObjectName("searchMinBytes");
    maxBytes_->setObjectName("searchMaxBytes");
    for (auto *field : {minBytes_, maxBytes_}) {
        field->setPlaceholderText("Any size");
        field->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{0,18}"), field));
    }
    auto *sizes = new QHBoxLayout;
    sizes->addWidget(new QLabel("Min bytes:", this));
    sizes->addWidget(minBytes_);
    sizes->addWidget(new QLabel("Max bytes:", this));
    sizes->addWidget(maxBytes_);
    advancedLayout->addLayout(sizes);
    afterEnabled_ = new QCheckBox("Modified after", this);
    beforeEnabled_ = new QCheckBox("Modified before", this);
    after_ = new QDateTimeEdit(QDateTime::currentDateTime().addDays(-30), this);
    before_ = new QDateTimeEdit(QDateTime::currentDateTime(), this);
    for (auto *date : {after_, before_}) {
        date->setCalendarPopup(true);
        date->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    }
    auto *dates = new QHBoxLayout;
    dates->addWidget(afterEnabled_);
    dates->addWidget(after_);
    dates->addWidget(beforeEnabled_);
    dates->addWidget(before_);
    advancedLayout->addLayout(dates);
    advancedLayout->addStretch();
    auto *profiles = new QHBoxLayout;
    auto *saved = new QComboBox(this);
    saved->setEditable(true);
    saved->setObjectName("searchProfiles");
    saved->setPlaceholderText("Saved search name");
    saved->addItems(QSettings().value("searchProfiles").toMap().keys());
    profiles->addWidget(saved, 1);
    auto *saveProfile = new QPushButton("Save search", this),
         *removeProfile = new QPushButton("Remove", this);
    saveProfile->setObjectName("saveSearchProfile");
    profiles->addWidget(saveProfile);
    profiles->addWidget(removeProfile);
    profilesLayout->addLayout(profiles);
    profilesLayout->addStretch();
    connect(saveProfile, &QPushButton::clicked, this, [this, saved] {
        const auto name = saved->currentText().trimmed();
        if (name.isEmpty())
            return;
        QJsonObject data{
            {"pattern", pattern_->text()},
            {"content", content_->text()},
            {"recursive", recursive_->isChecked()},
            {"hidden", hidden_->isChecked()},
            {"case", case_->isChecked()},
            {"nameRegex", nameRegex_->isChecked()},
            {"contentRegex", contentRegex_->isChecked()},
            {"minBytes", minBytes_->text()},
            {"maxBytes", maxBytes_->text()},
            {"afterEnabled", afterEnabled_->isChecked()},
            {"beforeEnabled", beforeEnabled_->isChecked()},
            {"after", after_->dateTime().toString(Qt::ISODate)},
            {"before", before_->dateTime().toString(Qt::ISODate)},
            {"rootBase64", QString::fromLatin1(QByteArray::fromStdString(root_.native()).toBase64())}};
        QSettings settings;
        auto all = settings.value("searchProfiles").toMap();
        all.insert(name, QJsonDocument(data).toJson(QJsonDocument::Compact));
        settings.setValue("searchProfiles", all);
        if (saved->findText(name) < 0)
            saved->addItem(name);
        saved->setCurrentText(name);
        status_->setText("Search saved");
    });
    connect(removeProfile, &QPushButton::clicked, this, [saved] {
        QSettings settings;
        auto all = settings.value("searchProfiles").toMap();
        all.remove(saved->currentText());
        settings.setValue("searchProfiles", all);
        saved->clear();
        saved->addItems(all.keys());
        saved->setCurrentIndex(-1);
    });
    connect(saved, &QComboBox::textActivated, this, [this, searchRoot](const QString &name) {
        const auto data =
            QJsonDocument::fromJson(QSettings().value("searchProfiles").toMap().value(name).toByteArray())
                .object();
        if (data.isEmpty())
            return;
        pattern_->setText(data.value("pattern").toString());
        content_->setText(data.value("content").toString());
        recursive_->setChecked(data.value("recursive").toBool());
        hidden_->setChecked(data.value("hidden").toBool());
        case_->setChecked(data.value("case").toBool());
        nameRegex_->setChecked(data.value("nameRegex").toBool());
        contentRegex_->setChecked(data.value("contentRegex").toBool());
        minBytes_->setText(data.value("minBytes").toString());
        maxBytes_->setText(data.value("maxBytes").toString());
        afterEnabled_->setChecked(data.value("afterEnabled").toBool());
        beforeEnabled_->setChecked(data.value("beforeEnabled").toBool());
        after_->setDateTime(QDateTime::fromString(data.value("after").toString(), Qt::ISODate));
        before_->setDateTime(QDateTime::fromString(data.value("before").toString(), Qt::ISODate));
        root_ = std::filesystem::path(
            QByteArray::fromBase64(data.value("rootBase64").toString().toLatin1()).toStdString());
        status_->setText("Search " + displayPath(root_));
        searchRoot->setText(displayPath(root_));
    });
    results_->setColumnCount(1);
    results_->setHorizontalHeaderLabels({"Path · double-click to reveal"});
    results_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    results_->setSelectionBehavior(QAbstractItemView::SelectRows);
    results_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(results_);
    layout->addWidget(status_);
    auto *buttons = new QHBoxLayout;
    auto *view = new QPushButton("&View F3", this), *edit = new QPushButton("&Edit F4", this),
         *fresh = new QPushButton("&New search", this), *go = new QPushButton("&Go to file", this),
         *feed = new QPushButton("Feed to &listbox", this);
    view->setShortcut(QKeySequence("F3"));
    edit->setShortcut(QKeySequence("F4"));
    view->setObjectName("searchView");
    edit->setObjectName("searchEdit");
    feed->setObjectName("searchFeed");
    connect(view, &QPushButton::clicked, this, [this] {
        const auto row = results_->currentRow();
        if (row >= 0 && static_cast<size_t>(row) < paths_.size())
            (new FileWindow(paths_[row], false, this))->show();
    });
    connect(edit, &QPushButton::clicked, this, [this] {
        const auto row = results_->currentRow();
        if (row >= 0 && static_cast<size_t>(row) < paths_.size())
            (new FileWindow(paths_[row], true, this))->show();
    });
    connect(feed, &QPushButton::clicked, this, [this] {
        if (!paths_.empty())
            emit resultsRequested(paths_);
    });
    const auto updateResultButtons = [this, view, edit, feed] {
        const auto row = results_->currentRow();
        const bool current = row >= 0 && static_cast<size_t>(row) < paths_.size();
        view->setEnabled(current);
        edit->setEnabled(current);
        feed->setEnabled(!paths_.empty());
    };
    connect(results_, &QTableWidget::itemSelectionChanged, this, updateResultButtons);
    auto *resultTimer = new QTimer(this);
    resultTimer->setInterval(100);
    connect(resultTimer, &QTimer::timeout, this, updateResultButtons);
    resultTimer->start();
    updateResultButtons();
    for (auto *button : {view, edit, fresh, go, feed})
        buttons->addWidget(button);
    connect(fresh, &QPushButton::clicked, this, [this] {
        if (control_)
            control_->cancelled = true;
        pattern_->setFocus();
        pattern_->selectAll();
    });
    connect(go, &QPushButton::clicked, this, [this] {
        const auto row = results_->currentRow();
        if (row >= 0 && static_cast<size_t>(row) < paths_.size())
            emit revealRequested(paths_[row]);
    });
    layout->addLayout(buttons);
    connect(start_, &QPushButton::clicked, this, &SearchDialog::start);
    connect(cancel, &QPushButton::clicked, this, [this] {
        if (control_ && !start_->isEnabled())
            control_->cancelled = true;
        else
            close();
    });
    connect(results_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && static_cast<size_t>(row) < paths_.size())
            emit revealRequested(paths_[row]);
    });
    auto *timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this] {
        if (control_ && !start_->isEnabled())
            status_->setText(QString("Searching… %1 entries inspected").arg(control_->completed.load()));
    });
    timer->start();
}
SearchDialog::~SearchDialog() {
    if (control_)
        control_->cancelled = true;
}
void SearchDialog::start() {
    if (!start_->isEnabled())
        return;
    if (content_->text().toUtf8().size() > 4096) {
        status_->setText("Content query is limited to 4096 UTF-8 bytes");
        return;
    }
    SearchRequest request{root_,
                          pattern_->text().isEmpty() ? (nameRegex_->isChecked() ? ".*" : "*")
                                                     : pattern_->text(),
                          content_->text(),
                          case_->isChecked(),
                          hidden_->isChecked(),
                          recursive_->isChecked(),
                          5000};
    if (findChild<QCheckBox *>("searchNativeFields")->isChecked()) {
        request.field = findChild<QLineEdit *>("searchFieldName")->text().trimmed();
        request.fieldValue = findChild<QLineEdit *>("searchFieldValue")->text();
        if (request.field.isEmpty()) {
            status_->setText("Enter a native field name");
            return;
        }
    }
    request.nameRegex = nameRegex_->isChecked();
    request.contentRegex = contentRegex_->isChecked();
    request.minBytes = minBytes_->text().isEmpty() ? -1 : minBytes_->text().toLongLong();
    request.maxBytes = maxBytes_->text().isEmpty() ? -1 : maxBytes_->text().toLongLong();
    request.modifiedAfter = afterEnabled_->isChecked() ? after_->dateTime().toSecsSinceEpoch() : -1;
    request.modifiedBefore = beforeEnabled_->isChecked() ? before_->dateTime().toSecsSinceEpoch() : -1;
    control_ = std::make_shared<LocalFiles::Control>();
    start_->setEnabled(false);
    results_->setRowCount(0);
    paths_.clear();
    auto *watcher = new QFutureWatcher<SearchResult>(this);
    connect(watcher, &QFutureWatcher<SearchResult>::finished, this, [this, watcher] {
        auto result = watcher->result();
        watcher->deleteLater();
        paths_ = std::move(result.paths);
        results_->setRowCount(static_cast<int>(paths_.size()));
        for (int row = 0; row < results_->rowCount(); ++row)
            results_->setItem(row, 0, new QTableWidgetItem(displayPath(paths_[row])));
        status_->setText(
            QString("%1 matches · %2 inspected · %3 errors · %4 files skipped by content size limit%5%6")
                .arg(paths_.size())
                .arg(result.inspected)
                .arg(result.errors.size())
                .arg(result.contentSkipped)
                .arg(result.cancelled ? " · stopped" : "")
                .arg(result.truncated ? " · result limit reached" : ""));
        status_->setToolTip(result.errors.join('\n'));
        start_->setEnabled(true);
    });
    watcher->setFuture(QtConcurrent::run(ioPool(), searchFiles, request, control_));
}

#include "filesystem/pathencoding.h"
#include "ui/panels/directorymodel.h"
#include "ui/panels/filepanel.h"
#include "app/mainwindow.h"
#include "ui/dialogs/filecontentandsearchdialogs.h"
#include "ui/dialogs/fileoperationdialogs.h"
#include "operations/iothreadpool.h"
#include "ui/panels/filepanellayout.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHeaderView>
#include <QMenuBar>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>
#include <QTabWidget>
#include <QListWidget>
#include <QCheckBox>
#include <QFontDialog>
#include <QColorDialog>

#include <QAction>
#include <QAbstractItemModelTester>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPointer>
#include <QProgressDialog>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QThreadPool>
#include <QItemSelectionModel>
#include <QSettings>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>
#include <fstream>
#include <algorithm>

class CommanderTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDirectory_;

    static void writeFile(const QString &path, const QByteArray &content = "fixture")
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(content), content.size());
    }

private slots:
    void initTestCase()
    {
        QVERIFY(settingsDirectory_.isValid());
        QVERIFY(QDir().mkpath(settingsDirectory_.filePath("data")));
        qputenv("XDG_DATA_HOME", QFile::encodeName(settingsDirectory_.filePath("data")));
        QCoreApplication::setOrganizationName("TwinCommanderTests");
        QCoreApplication::setApplicationName("TwinCommanderTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory_.path());
    }

    void init() { QSettings().clear(); }

    void referenceMenusBarsAndLayout()
    {
        QTemporaryDir directory; writeFile(directory.filePath("sample.txt"));
        MainWindow window({directory.path(),directory.path()}); window.resize(1200,740); window.show();
        QTRY_COMPARE(window.activePanel()->model()->rowCount(),2);
        const QStringList expected{"&Files","&Mark","&Commands","&Net","Sho&w","C&onfiguration","&Start","&Help"};
        const auto menus=window.menuBar()->actions(); QCOMPARE(menus.size(),expected.size());
        const auto reference=commanderUiReference(); const auto definitions=reference.value("menus").toArray();
        std::function<void(QMenu *,QJsonArray)> verify=[&](QMenu *menu,QJsonArray items){
            if(items.isEmpty()) return; // Start is populated dynamically.
            QCOMPARE(menu->actions().size(),items.size());
            for(int i=0;i<items.size();++i){const auto item=items[i].toObject();auto *action=menu->actions()[i];
                if(item.value("caption")=="-"){QVERIFY(action->isSeparator());continue;}
                QCOMPARE(action->text(),item.value("command").toInt()==690?QString("&About Twin Commander..."):item.value("caption").toString());
                if(item.contains("items"))verify(action->menu(),item.value("items").toArray());
                else QCOMPARE(action->property("tcCommandId").toInt(),item.value("command").toInt());
            }
        };
        for(int i=0;i<menus.size();++i){QCOMPARE(menus[i]->text(),expected[i]);verify(menus[i]->menu(),definitions[i].toObject().value("items").toArray());}
        auto *bar=window.findChild<QToolBar *>("referenceButtonBar"); QVERIFY(bar); QCOMPARE(bar->actions().size(),26);
        const auto barSlots=reference.value("toolbar").toArray();
        for(int i=0;i<barSlots.size();++i){const auto item=barSlots[i].toObject();QCOMPARE(bar->actions()[i]->isSeparator(),item.value("separator").toBool());if(!bar->actions()[i]->isSeparator())QCOMPARE(bar->actions()[i]->property("referenceToolbarCommand").toString(),item.value("command").toString());}
        const QStringList keys{"F3 View","F4 Edit","F5 Copy","F6 Move","F7 NewFolder","F8 Delete","Alt+F4 Exit"};QStringList actual;
        for(auto *button:window.findChildren<QPushButton *>())if(button->objectName().startsWith("function_"))actual<<button->text();QCOMPARE(actual,keys);
        const QStringList columns{"Name","Ext","Size","Date","Attr"};for(int i=0;i<5;++i)QCOMPARE(window.activePanel()->model()->headerData(i,Qt::Horizontal,Qt::DisplayRole).toString(),columns[i]);
        QCOMPARE(window.findChild<QAction *>("cm_DirSize")->shortcut(),QKeySequence("Ctrl+L"));
        const auto screenshots=qEnvironmentVariable("TC_SCREENSHOT_DIR");if(!screenshots.isEmpty()){QDir().mkpath(screenshots);QVERIFY(window.grab().save(screenshots+"/reference-main.png"));}
        window.findChild<QAction *>("cm_Config")->trigger();auto *options=window.findChild<QDialog *>("referenceConfiguration");QVERIFY(options);
        QCOMPARE(options->findChild<QListWidget *>("configurationCategories")->count(),26);
        auto *showBar=options->findChild<QCheckBox *>("idShowButtonBar");QVERIFY(showBar);showBar->setChecked(false);
        QTest::mouseClick(options->findChild<QPushButton *>("configurationApply"),Qt::LeftButton);QVERIFY(!bar->isVisible());
        if(!screenshots.isEmpty())QVERIFY(options->grab().save(screenshots+"/reference-configuration.png"));
        options->close();
    }

    void parentSelectionAndBriefShareSafeTargets()
    {
        QTemporaryDir directory;writeFile(directory.filePath("a.txt"));writeFile(directory.filePath("b.txt"));
        FilePanel panel(nativePath(directory.path()));panel.show();QTRY_COMPARE(panel.model()->rowCount(),3);
        panel.selectMask("*");QCOMPARE(panel.selectedPaths().size(),size_t(2));
        for(const auto &path:panel.selectedPaths())QCOMPARE(path.parent_path(),nativePath(directory.path()));
        panel.invertSelection();QVERIFY(panel.selectedPaths().empty());panel.setBrief(true);panel.selectMask("*.txt");QCOMPARE(panel.selectedPaths().size(),size_t(2));
        panel.table()->selectionModel()->clearSelection();panel.table()->setCurrentIndex(panel.model()->index(0,0));QVERIFY(panel.selectedPaths().empty());
    }

    void implementedButtonsExceedThirtyPercentOfFrozenBaseline()
    {
        QFile baselineFile(QFINDTESTDATA("../docs/reference/disabled-buttons-0.4.json"));QVERIFY(baselineFile.open(QIODevice::ReadOnly));const auto baseline=QJsonDocument::fromJson(baselineFile.readAll()).array();
        QFile implementedFile(QFINDTESTDATA("../docs/reference/implemented-buttons-0.5.json"));QVERIFY(implementedFile.open(QIODevice::ReadOnly));const auto implemented=QJsonDocument::fromJson(implementedFile.readAll()).array();
        QCOMPARE(baseline.size(),155);QVERIFY(implemented.size()*100>=baseline.size()*30);
        QTemporaryDir directory;writeFile(directory.filePath("fixture.txt"));MainWindow window({directory.path(),directory.path()});window.show();QTRY_COMPARE(window.activePanel()->model()->rowCount(),2);
        window.findChild<QAction *>("cm_Config")->trigger();auto *configuration=window.findChild<QDialog *>("referenceConfiguration");
        window.findChild<QAction *>("cm_ButtonConfig")->trigger();auto *horizontal=window.findChild<QDialog *>("horizontalBarEditor");
        window.findChild<QAction *>("cm_ButtonConfig2")->trigger();auto *vertical=window.findChild<QDialog *>("verticalBarEditor");
        JobQueue queue;BatchRenameDialog rename({nativePath(directory.filePath("fixture.txt"))},&queue,{},&window);
        SearchDialog search(nativePath(directory.path()),&window);search.show();QTest::mouseClick(search.findChild<QPushButton *>("searchStart"),Qt::LeftButton);auto *results=search.findChild<QTableWidget *>("searchResults");QTRY_COMPARE(results->rowCount(),1);results->selectRow(0);QTest::qWait(150);
        const QMap<QString,QWidget *> contexts{{"configuration",configuration},{"cm_ButtonConfig",horizontal},{"cm_ButtonConfig2",vertical},{"rename",&rename},{"search",&search}};
        QSet<QString> seen;
        for(const auto &value:implemented){const auto item=value.toObject();const auto context=item.value("context").toString(),id=item.value("id").toString();const auto key=context+"/"+id;QVERIFY(!seen.contains(key));seen.insert(key);
            bool inBaseline=false;for(const auto &entry:baseline)if(entry.toObject()==item)inBaseline=true;QVERIFY(inBaseline);
            if(context=="commands"){QAction *found=nullptr;const int nativeId=id.mid(3).toInt();for(auto *action:window.findChildren<QAction *>())if(action->property("tcCommandId").toInt()==nativeId){found=action;break;}QVERIFY2(found&&found->isEnabled(),qPrintable(key));}
            else{auto *widget=contexts.value(context);QVERIFY(widget);QAbstractButton *found=nullptr;for(auto *button:widget->findChildren<QAbstractButton *>())if(button->objectName()==id||button->text()==id){found=button;break;}QVERIFY2(found&&found->isEnabled(),qPrintable(key));}
        }
        QCOMPARE(seen.size(),62);
    }

    void buttonBarEditsPersistAndCancelDiscards()
    {
        QTemporaryDir directory;MainWindow window({directory.path(),directory.path()});window.show();
        const auto edit=[&window]{window.findChild<QAction *>("cm_ButtonConfig")->trigger();return window.findChild<QDialog *>("horizontalBarEditor");};
        auto *dialog=edit();auto *list=dialog->findChild<QListWidget *>("id_btn_bar");QCOMPARE(list->count(),26);QTest::mouseClick(dialog->findChild<QPushButton *>("id_btn_delete"),Qt::LeftButton);QCOMPARE(list->count(),25);QTest::mouseClick(dialog->findChild<QPushButton *>("Cancel"),Qt::LeftButton);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QVERIFY(!QSettings().contains("customBars/horizontal"));dialog=edit();QCOMPARE(dialog->findChild<QListWidget *>("id_btn_bar")->count(),26);QTest::mouseClick(dialog->findChild<QPushButton *>("id_btn_delete"),Qt::LeftButton);QTest::mouseClick(dialog->findChild<QPushButton *>("Ok"),Qt::LeftButton);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QCOMPARE(window.findChild<QToolBar *>("referenceButtonBar")->actions().size(),25);QVERIFY(QSettings().contains("customBars/horizontal"));
        MainWindow restored({directory.path(),directory.path()});QCOMPARE(restored.findChild<QToolBar *>("referenceButtonBar")->actions().size(),25);
        window.findChild<QAction *>("cm_ButtonConfig2")->trigger();auto *vertical=window.findChild<QDialog *>("verticalBarEditor");QTest::mouseClick(vertical->findChild<QPushButton *>("Ok"),Qt::LeftButton);QVERIFY(QSettings().contains("customBars/vertical"));
    }

    void viewFiltersAndSearchFeedPreserveNativeTargets()
    {
        QTemporaryDir directory;writeFile(directory.filePath("a.txt"));writeFile(directory.filePath("b.bin"));QVERIFY(QDir(directory.path()).mkdir("child"));writeFile(directory.filePath("child/c.txt"));
        MainWindow window({directory.path(),directory.path()});window.show();auto *panel=window.activePanel();QTRY_COMPARE(panel->model()->rowCount(),4);
        panel->filterView("*.txt");QTRY_COMPARE(panel->model()->rowCount(),3);panel->selectMask("a.txt");panel->filterView({},false,true);QTRY_COMPARE(panel->model()->rowCount(),2);QCOMPARE(panel->model()->entry(1)->path,nativePath(directory.filePath("a.txt")));
        window.findChild<QAction *>("cm_AllFiles")->trigger();QTRY_COMPARE(panel->model()->rowCount(),4);
        QFile::setPermissions(directory.filePath("b.bin"),QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);window.findChild<QAction *>("cm_Programs")->trigger();QTRY_COMPARE(panel->model()->rowCount(),3);QCOMPARE(panel->model()->entry(2)->name,QString("b.bin"));
        const std::vector<std::filesystem::path> paths{nativePath(directory.filePath("a.txt")),nativePath(directory.filePath("child/c.txt"))};window.showSearchResults(paths);auto *results=window.activePanel();QVERIFY(results!=panel);QTRY_COMPARE(results->model()->rowCount(),3);results->selectMask("*");auto selected=results->selectedPaths();std::sort(selected.begin(),selected.end());auto expected=paths;std::sort(expected.begin(),expected.end());QVERIFY(selected==expected);
        results->navigate(nativePath(directory.filePath("child")));QTRY_COMPARE(results->model()->rowCount(),2);QCOMPARE(results->model()->entry(1)->name,QString("c.txt"));
    }

    void appearanceChangesRequireApplyAndViewModesPersist()
    {
        QTemporaryDir directory;MainWindow window({directory.path(),directory.path()});window.show();QTRY_VERIFY(!window.activePanel()->path().empty());
        window.findChild<QAction *>("cm_Config")->trigger();auto *dialog=window.findChild<QDialog *>("referenceConfiguration");
        QTimer::singleShot(0,dialog,[]{auto *picker=qobject_cast<QFontDialog *>(QApplication::activeModalWidget());QVERIFY(picker);QFont font=picker->currentFont();font.setPointSize(14);picker->setCurrentFont(font);picker->accept();});
        dialog->findChild<QPushButton *>("ChangeFont1")->click();QVERIFY(!QSettings().contains("appearance/fileFont"));dialog->findChild<QPushButton *>("configurationCancel")->click();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(!QSettings().contains("appearance/fileFont"));
        window.findChild<QAction *>("cm_Config")->trigger();dialog=window.findChild<QDialog *>("referenceConfiguration");
        QTimer::singleShot(0,dialog,[]{auto *picker=qobject_cast<QColorDialog *>(QApplication::activeModalWidget());QVERIFY(picker);picker->setCurrentColor(QColor("#123456"));picker->accept();});dialog->findChild<QPushButton *>("id_MarkChooseColor")->click();
        window.activePanel()->setBrief(true);QTimer::singleShot(0,dialog,[]{for(auto *widget:QApplication::topLevelWidgets())if(auto *input=qobject_cast<QInputDialog *>(widget)){input->setTextValue("Brief fixture");input->accept();}});dialog->findChild<QPushButton *>("id_addmenu")->click();
        QVERIFY(!QSettings().contains("viewModes"));dialog->findChild<QPushButton *>("configurationApply")->click();QCOMPARE(QSettings().value("appearance/markColor").toString(),QString("#123456"));QVERIFY(QSettings().value("viewModes").toMap().contains("Brief fixture"));
        window.activePanel()->setBrief(false);QTimer::singleShot(0,&window,[]{for(auto *widget:QApplication::topLevelWidgets())if(auto *input=qobject_cast<QInputDialog *>(widget))input->accept();});window.findChild<QAction *>("cm_ViewModes")->trigger();QVERIFY(window.activePanel()->isBrief());dialog->close();
    }

    void fedPanelIncludesExplicitHiddenPathsAndKeepsExtensionlessNames()
    {
        QTemporaryDir directory;QVERIFY(QDir(directory.path()).mkdir("child.dot"));writeFile(directory.filePath("child.dot/README"));writeFile(directory.filePath("child.dot/.hidden"));MainWindow window({directory.path(),directory.path()});window.show();QTRY_COMPARE(window.activePanel()->model()->rowCount(),2);
        window.showSearchResults({nativePath(directory.filePath("child.dot/README")),nativePath(directory.filePath("child.dot/.hidden"))});auto *model=window.activePanel()->model();QTRY_COMPARE(model->rowCount(),3);
        for(int row=1;row<3;++row){QVERIFY(model->index(row,0).data().toString().startsWith("child.dot/"));QCOMPARE(model->index(row,1).data().toString(),QString());}
    }

    void listingFiltersHiddenAndKeepsDirectoriesFirst()
    {
        QTemporaryDir directory;
        QVERIFY(QDir(directory.path()).mkdir("z-folder"));
        writeFile(directory.filePath("alpha"));
        writeFile(directory.filePath(".hidden"));
        DirectoryModel model;
        QAbstractItemModelTester modelTester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy loaded(&model, &DirectoryModel::loaded);
        model.load(nativePath(directory.path()));
        QTRY_COMPARE(loaded.count(), 1);
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(model.entry(0)->directory);
        QCOMPARE(model.entry(1)->name, QString("alpha"));
        model.setShowHidden(true);
        model.load(nativePath(directory.path()));
        QTRY_COMPARE(loaded.count(), 2);
        QCOMPARE(model.rowCount(), 3);
        model.sort(0, Qt::DescendingOrder);
        QVERIFY(model.entry(0)->directory);
    }

    void failedNavigationPreservesListing()
    {
        QTemporaryDir directory;
        writeFile(directory.filePath("keep"));
        DirectoryModel model;
        QSignalSpy loaded(&model, &DirectoryModel::loaded);
        QSignalSpy failed(&model, &DirectoryModel::loadFailed);
        model.load(nativePath(directory.path()));
        QTRY_COMPARE(loaded.count(), 1);
        model.load(nativePath(directory.filePath("missing")));
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(model.path() == nativePath(directory.path()));
        QCOMPARE(model.entry(0)->name, QString("keep"));
    }

    void latestNavigationWins()
    {
        QTemporaryDir first;
        QTemporaryDir second;
        writeFile(first.filePath("first"));
        writeFile(second.filePath("second"));
        DirectoryModel model;
        QSignalSpy loaded(&model, &DirectoryModel::loaded);
        model.load(nativePath(first.path()));
        model.load(nativePath(second.path()));
        QTRY_COMPARE(loaded.count(), 1);
        QVERIFY(model.path() == nativePath(second.path()));
        QCOMPARE(model.entry(0)->name, QString("second"));
    }

    void boundedBatchesKeepPreviousListingUntilCommit()
    {
        QTemporaryDir previous;
        QTemporaryDir directory;
        writeFile(previous.filePath("keep"));
        for (int i = 0; i < 1100; ++i)
            writeFile(directory.filePath(QString::number(i)));
        DirectoryModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy loaded(&model, &DirectoryModel::loaded);
        QSignalSpy batches(&model, &DirectoryModel::batchReceived);
        model.load(nativePath(previous.path()));
        QTRY_COMPARE(loaded.count(), 1);
        batches.clear();
        connect(&model, &DirectoryModel::batchReceived, this, [&](int count, int total) {
            if (total == count) model.sort(0, Qt::DescendingOrder);
            QVERIFY(count > 0 && count <= 256);
            QVERIFY(total <= 1100);
            QCOMPARE(model.entry(0)->name, QString("keep"));
            QVERIFY(model.path() == nativePath(previous.path()));
        });
        model.load(nativePath(directory.path()));
        QTRY_COMPARE(loaded.count(), 2);
        QCOMPARE(model.rowCount(), 1100);
        QCOMPARE(batches.count(), 5);
        QCOMPARE(batches.last().at(1).toInt(), 1100);
        QCOMPARE(model.entry(0)->name, QString("999"));
    }

    void destructionCancelsProducerWithQueuedBatches()
    {
        QTemporaryDir directory;
        for (int i = 0; i < 1500; ++i) writeFile(directory.filePath(QString::number(i)));
        auto model = std::make_unique<DirectoryModel>();
        model->load(nativePath(directory.path()));
        // Hold the UI thread so the producer can fill the bounded queue.
        QTest::qSleep(100);
        model.reset();
        QVERIFY(ioPool()->waitForDone(5000));
    }

    void navigationDuringBatchDeliveryRejectsStaleResults()
    {
        QTemporaryDir large;
        QTemporaryDir latest;
        for (int i = 0; i < 1500; ++i) writeFile(large.filePath(QString::number(i)));
        writeFile(latest.filePath("latest"));
        DirectoryModel model;
        QSignalSpy loaded(&model, &DirectoryModel::loaded);
        bool redirected = false;
        connect(&model, &DirectoryModel::batchReceived, this, [&](int, int) {
            if (!redirected) {
                redirected = true;
                model.load(nativePath(latest.path()));
            }
        });
        model.load(nativePath(large.path()));
        QTRY_COMPARE(loaded.count(), 1);
        QVERIFY(redirected);
        QVERIFY(model.path() == nativePath(latest.path()));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.entry(0)->name, QString("latest"));
    }

    void largeDirectoryMeasurement()
    {
        if (!qEnvironmentVariableIsSet("TC_LARGE_DIRECTORY_TEST"))
            QSKIP("Set TC_LARGE_DIRECTORY_TEST=1 to measure 100000 entries");
        QTemporaryDir directory;
        for (int i = 0; i < 100000; ++i) writeFile(directory.filePath(QString::number(i)));
        DirectoryModel model;
        QSignalSpy loaded(&model, &DirectoryModel::loaded);
        QElapsedTimer elapsed;
        elapsed.start();
        qint64 firstBatch = -1;
        qint64 lastTick = 0;
        qint64 maxLatency = 0;
        QTimer heartbeat;
        heartbeat.setInterval(1);
        connect(&heartbeat, &QTimer::timeout, this, [&] {
            const auto now = elapsed.elapsed();
            maxLatency = std::max(maxLatency, now - lastTick);
            lastTick = now;
        });
        connect(&model, &DirectoryModel::batchReceived, this, [&](int count, int) {
            QVERIFY(count <= 256);
            if (firstBatch < 0) firstBatch = elapsed.elapsed();
        });
        heartbeat.start();
        QEventLoop loop;
        connect(&model, &DirectoryModel::loaded, &loop, &QEventLoop::quit);
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(60000);
        model.load(nativePath(directory.path()));
        loop.exec();
        QCOMPARE(loaded.count(), 1);
        const auto total = elapsed.elapsed();
        maxLatency = std::max(maxLatency, total - lastTick);
        QCOMPARE(model.rowCount(), 100000);
        qInfo() << "100000 entries: first batch ms" << firstBatch
                << "total ms" << total << "max event-loop gap ms" << maxLatency;
    }

    void sortPreservesSelectedItemIdentity()
    {
        QTemporaryDir directory;
        writeFile(directory.filePath("alpha"), "a");
        writeFile(directory.filePath("beta"), "longer");
        DirectoryModel model;
        QSignalSpy loaded(&model, &DirectoryModel::loaded);
        model.load(nativePath(directory.path()));
        QTRY_COMPARE(loaded.count(), 1);
        QItemSelectionModel selection(&model);
        selection.select(model.index(0, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        model.sort(2, Qt::DescendingOrder);
        QCOMPARE(selection.selectedRows().size(), 1);
        QCOMPARE(selection.selectedRows().first().data().toString(), QString("alpha"));
    }

    void refreshRestoresCursorAndSelectionByIdentity()
    {
        QTemporaryDir directory;
        writeFile(directory.filePath("beta"));
        writeFile(directory.filePath("delta"));
        writeFile(directory.filePath(".hidden"));
        FilePanel panel(nativePath(directory.path()));
        QSignalSpy loaded(panel.model(), &DirectoryModel::loaded);
        QTRY_COMPARE(loaded.count(), 1);
        auto *selection = panel.table()->selectionModel();
        selection->select(panel.model()->index(1, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        selection->setCurrentIndex(panel.model()->index(2, 2), QItemSelectionModel::NoUpdate);
        writeFile(directory.filePath("alpha"));
        panel.refresh();
        QTRY_COMPARE(loaded.count(), 2);
        QCOMPARE(selection->selectedRows().size(), 1);
        QCOMPARE(selection->selectedRows().first().data().toString(), QString("beta"));
        QCOMPARE(panel.model()->entry(panel.table()->currentIndex().row())->name, QString("delta"));
        QCOMPARE(panel.table()->currentIndex().column(), 2);
        panel.toggleHidden();
        QTRY_COMPARE(loaded.count(), 3);
        QCOMPARE(selection->selectedRows().first().data().toString(), QString("beta"));
        QCOMPARE(panel.model()->entry(panel.table()->currentIndex().row())->name, QString("delta"));
        QVERIFY(QFile::remove(directory.filePath("delta")));
        QVERIFY(QFile::remove(directory.filePath("beta")));
        panel.refresh();
        QTRY_COMPARE(loaded.count(), 4);
        QVERIFY(selection->selectedRows().isEmpty());
        QCOMPARE(panel.model()->entry(panel.table()->currentIndex().row())->name, QString("alpha"));
        QVERIFY(QFile::remove(directory.filePath("alpha")));
        panel.toggleHidden();
        QTRY_COMPARE(loaded.count(), 5);
        QCOMPARE(panel.model()->rowCount(), 1);
        QVERIFY(panel.model()->entry(0)->parent);
        QVERIFY(panel.table()->currentIndex().isValid());
    }

    void navigationClearsSelectionEvenWithMatchingNames()
    {
        QTemporaryDir directory;
        QVERIFY(QDir(directory.path()).mkdir("child"));
        writeFile(directory.filePath("same"));
        writeFile(directory.filePath("child/same"));
        FilePanel panel(nativePath(directory.path()));
        QSignalSpy loaded(panel.model(), &DirectoryModel::loaded);
        QTRY_COMPARE(loaded.count(), 1);
        panel.table()->selectRow(2);
        panel.navigate(nativePath(directory.filePath("child")));
        QTRY_COMPARE(loaded.count(), 2);
        QVERIFY(panel.table()->selectionModel()->selectedRows().isEmpty());
        QCOMPARE(panel.table()->currentIndex().row(), 0);
    }

    void refreshKeepsUndecodableNamesDistinct()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir directory;
        const auto root = nativePath(directory.path());
        const auto first = root / std::string("raw-\xff");
        const auto second = root / std::string("raw-\xfe");
        std::ofstream(first) << "first";
        std::ofstream(second) << "second";
        FilePanel panel(root);
        QSignalSpy loaded(panel.model(), &DirectoryModel::loaded);
        QTRY_COMPARE(loaded.count(), 1);
        int selectedRow = panel.model()->entry(1)->path == first ? 1 : 2;
        panel.table()->selectRow(selectedRow);
        panel.refresh();
        QTRY_COMPARE(loaded.count(), 2);
        const auto selected = panel.table()->selectionModel()->selectedRows();
        QCOMPARE(selected.size(), 1);
        QVERIFY(panel.model()->entry(selected.first().row())->path == first);
        QVERIFY(panel.model()->entry(panel.table()->currentIndex().row())->path == first);
        std::filesystem::remove(first);
        std::filesystem::remove(second);
#else
        QSKIP("Linux native filename behavior");
#endif
    }

    void keyboardSelectionAndHistory()
    {
        QTemporaryDir directory;
        QVERIFY(QDir(directory.path()).mkpath("one/two"));
        writeFile(directory.filePath("file"));
        FilePanel panel(nativePath(directory.path()));
        panel.show();
        QTRY_COMPARE(panel.model()->rowCount(), 3);
        QCOMPARE(panel.table()->selectionModel()->selectedRows().size(), 0);
        panel.focusFiles();
        panel.table()->setCurrentIndex(panel.model()->index(1, 0));
        panel.table()->clearSelection();
        QTest::keyClick(panel.table(), Qt::Key_Insert);
        QCOMPARE(panel.table()->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(panel.table()->currentIndex().row(), 2);
        panel.navigate(nativePath(directory.filePath("one")));
        QTRY_VERIFY(panel.path() == nativePath(directory.filePath("one")));
        panel.navigate(nativePath(directory.filePath("one/two")));
        QTRY_VERIFY(panel.path() == nativePath(directory.filePath("one/two")));
        panel.back();
        QTRY_VERIFY(panel.path() == nativePath(directory.filePath("one")));
        panel.back();
        QTRY_VERIFY(panel.path() == nativePath(directory.path()));
        panel.table()->selectionModel()->setCurrentIndex(panel.model()->index(1, 0), QItemSelectionModel::NoUpdate);
        QTest::keyClick(panel.table(), Qt::Key_Return);
        QTRY_VERIFY(panel.path() == nativePath(directory.filePath("one")));
        QTest::keyClick(panel.table(), Qt::Key_Backspace);
        QTRY_VERIFY(panel.path() == nativePath(directory.path()));
    }

    void linuxNamesAndLinksRetainNativeIdentity()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir directory;
        const auto root = nativePath(directory.path());
        const auto rawPath = root / std::string("raw-\xff");
        std::ofstream(rawPath) << "raw";
        std::filesystem::create_symlink(root / "absent", root / "dangling");
        DirectoryModel model;
        QSignalSpy loaded(&model, &DirectoryModel::loaded);
        model.load(root);
        QTRY_COMPARE(loaded.count(), 1);
        QCOMPARE(model.rowCount(), 2);
        bool foundRaw = false;
        bool foundLink = false;
        for (int row = 0; row < model.rowCount(); ++row) {
            foundRaw = foundRaw || model.entry(row)->path == rawPath;
            foundLink = foundLink || model.entry(row)->symlink;
        }
        QVERIFY(foundRaw);
        QVERIFY(foundLink);
        std::filesystem::remove(rawPath);
#else
        QSKIP("Linux filename and symlink behavior");
#endif
    }

    void tabsSwitchPanelsAndPersistSession()
    {
        QTemporaryDir left;
        QTemporaryDir right;
        MainWindow window({left.path(), right.path()});
        window.show();
        auto *initial = window.activePanel();
        QTRY_VERIFY(initial->path() == nativePath(left.path()));
        initial->focusFiles();
        QTest::keyClick(initial->table(), Qt::Key_Tab);
        QVERIFY(window.activePanel() != initial);
        QTRY_VERIFY(window.activePanel()->path() == nativePath(right.path()));
        auto *newTab = window.findChild<QAction *>("cm_OpenNewTab");
        QVERIFY(newTab);
        QTest::keyClick(window.activePanel()->table(), Qt::Key_T, Qt::ControlModifier);
        QTRY_COMPARE(window.findChildren<FilePanel *>().size(), 3);
        QTRY_VERIFY(window.activePanel()->path() == nativePath(right.path()));
        QCOMPARE(window.findChildren<FilePanel *>().size(), 3);
        auto *closeTab = window.findChild<QAction *>("cm_CloseCurrentTab");
        QVERIFY(closeTab);
        QTest::keyClick(window.activePanel()->table(), Qt::Key_W, Qt::ControlModifier);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(window.findChildren<FilePanel *>().size(), 2);
        closeTab->trigger();
        QCOMPARE(window.findChildren<FilePanel *>().size(), 2);
        newTab->trigger();
        QTRY_VERIFY(window.activePanel()->path() == nativePath(right.path()));
        window.close();
        MainWindow restored;
        QCOMPARE(restored.findChildren<FilePanel *>().size(), 3);
        QTRY_VERIFY(restored.activePanel()->path() == nativePath(left.path()));
        const auto panels = restored.findChildren<FilePanel *>();
        for (auto *panel : panels) QTRY_VERIFY(!panel->path().empty());
    }

    void sessionRestoresNativeDirectoryBytes()
    {
        QTemporaryDir directory;
        const auto raw = nativePath(directory.path()) / std::string("dir-\xff");
        std::filesystem::create_directory(raw);
        {
            MainWindow first({directory.path(), directory.path()});
            QTRY_VERIFY(!first.activePanel()->path().empty());
            first.activePanel()->navigate(raw);
            QTRY_VERIFY(first.activePanel()->path() == raw);
            first.close();
        }
        {
            MainWindow restored;
            QTRY_VERIFY(restored.activePanel()->path() == raw);
            restored.close();
        }
        std::filesystem::remove(raw);
    }

    void panelMasksFilterAndReveal()
    {
        QTemporaryDir directory;
        writeFile(directory.filePath("a.txt"));
        writeFile(directory.filePath("b.txt"));
        writeFile(directory.filePath("c.bin"));
        FilePanel panel(nativePath(directory.path()));
        QTRY_COMPARE(panel.model()->rowCount(), 4);
        panel.selectMask("*.txt");
        QCOMPARE(panel.selectedPaths().size(), size_t(2));
        panel.invertSelection();
        QCOMPARE(panel.selectedPaths().size(), size_t(1));
        QCOMPARE(panel.selectedPaths().front().filename(), std::filesystem::path("c.bin"));
        auto *filter = panel.findChild<QLineEdit *>("quickFilter");
        QVERIFY(filter);
        filter->setText("b.tx");
        QTRY_COMPARE(panel.model()->rowCount(), 2);
        QCOMPARE(panel.model()->entry(1)->name, QString("b.txt"));
        panel.reveal(nativePath(directory.filePath("a.txt")));
        QTRY_COMPARE(panel.model()->rowCount(), 4);
        QTRY_COMPARE(panel.selectedPaths().front().filename(), std::filesystem::path("a.txt"));
    }

    void copyAndMkdirCommandsUseRealDialogs()
    {
        QTemporaryDir left;
        QTemporaryDir right;
        writeFile(left.filePath("copy-me"), "payload");
        MainWindow window({left.path(), right.path()});
        window.show();
        QTRY_COMPARE(window.activePanel()->model()->rowCount(), 2);
        QTest::qWait(20);
        window.activePanel()->table()->setCurrentIndex(window.activePanel()->model()->index(1, 0));
        QTimer::singleShot(0, &window, [&] {
            auto *destination = window.findChild<QLineEdit *>("operationDestination");
            QVERIFY(destination);
            QCOMPARE(destination->text(), right.path());
            auto *dialog = qobject_cast<QDialog *>(destination->parentWidget());
            QVERIFY(dialog);
            dialog->accept();
        });
        window.findChild<QAction *>("cm_Copy")->trigger();
        QTRY_VERIFY(QFile::exists(right.filePath("copy-me")));
        QTRY_VERIFY(!window.findChildren<QProgressDialog *>().size());
        QFile file(right.filePath("copy-me"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("payload"));
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QInputDialog *>();
            QVERIFY(dialog);
            dialog->setTextValue("created");
            dialog->accept();
        });
        window.findChild<QAction *>("cm_MkDir")->trigger();
        QTRY_VERIFY(QDir(left.filePath("created")).exists());
        QTRY_VERIFY(!window.findChildren<QProgressDialog *>().size());
        QTRY_COMPARE(window.activePanel()->model()->rowCount(), 3);
        const auto screenshotDirectory = qEnvironmentVariable("TC_SCREENSHOT_DIR");
        if (!screenshotDirectory.isEmpty()) {
            QDir().mkpath(screenshotDirectory);
            QVERIFY(window.grab().save(screenshotDirectory + "/main-window.png"));
        }
    }

    void cancellingCopyRestoresSourceFocusAndSelection()
    {
        QTemporaryDir directory;
        writeFile(directory.filePath("keep"));
        MainWindow window({directory.path(), directory.path()});
        window.show();
        auto *panel = window.activePanel();
        QTRY_COMPARE(panel->model()->rowCount(), 2);
        panel->focusFiles();
        panel->table()->selectRow(1);
        QTimer::singleShot(0, &window, [&] {
            auto *destination = window.findChild<QLineEdit *>("operationDestination");
            QVERIFY(destination);
            qobject_cast<QDialog *>(destination->parentWidget())->reject();
        });
        window.findChild<QAction *>("cm_Copy")->trigger();
        QTRY_VERIFY(panel->table()->hasFocus());
        QCOMPARE(panel->selectedPaths().size(), size_t(1));
        QCOMPARE(panel->selectedPaths().front().filename(), std::filesystem::path("keep"));
        QVERIFY(window.findChildren<QProgressDialog *>().empty());
    }

    void searchDialogFindsAndRevealsResult()
    {
        QTemporaryDir directory;
        writeFile(directory.filePath("match.txt"), "needle");
        writeFile(directory.filePath("other.bin"));
        QPointer<SearchDialog> dialog = new SearchDialog(nativePath(directory.path()));
        dialog->show();
        dialog->findChild<QLineEdit *>("searchPattern")->setText("*.txt");
        dialog->findChild<QLineEdit *>("searchContent")->setText("needle");
        auto *start = dialog->findChild<QPushButton *>("searchStart");
        QTest::mouseClick(start, Qt::LeftButton);
        auto *results = dialog->findChild<QTableWidget *>("searchResults");
        QTRY_COMPARE(results->rowCount(), 1);
        QCOMPARE(results->item(0, 0)->text(), directory.filePath("match.txt"));
        auto *tabs=dialog->findChild<QTabWidget *>("searchTabs");QVERIFY(tabs);QCOMPARE(tabs->count(),4);
        const QStringList names{"General","Advanced","Plugins","Load/Save"};for(int i=0;i<4;++i)QCOMPARE(tabs->tabText(i),names[i]);
        const auto screenshots=qEnvironmentVariable("TC_SCREENSHOT_DIR");if(!screenshots.isEmpty()){QDir().mkpath(screenshots);QVERIFY(dialog->grab().save(screenshots+"/reference-search.png"));}
        std::filesystem::path revealed;
        connect(dialog, &SearchDialog::revealRequested, this, [&](const auto &path) { revealed = path; });
        const auto point = results->visualItemRect(results->item(0, 0)).center();
        QTest::mouseClick(results->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseDClick(results->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTRY_VERIFY(revealed == nativePath(directory.filePath("match.txt")));
        dialog->close();
        QTRY_VERIFY(dialog.isNull());
    }

    void checksumCommandReportsKnownDigest()
    {
        QTemporaryDir directory;
        writeFile(directory.filePath("abc.txt"), "abc");
        MainWindow window({directory.path(), directory.path()});
        window.show();
        QTRY_COMPARE(window.activePanel()->model()->rowCount(), 2);
        window.activePanel()->table()->setCurrentIndex(window.activePanel()->model()->index(1, 0));
        window.findChild<QAction *>("cm_Checksum")->trigger();
        auto *text = window.findChild<QPlainTextEdit *>();
        QVERIFY(text);
        QTRY_VERIFY(text->toPlainText().contains("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    }

    void editorLoadsSavesAndProtectsUnsavedChanges()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("text.txt");
        writeFile(path, "original");
        QPointer<FileContentDialog> editor = new FileContentDialog(nativePath(path), true);
        editor->show();
        auto *text = editor->findChild<QPlainTextEdit *>();
        QTRY_VERIFY(!text->isReadOnly());
        QCOMPARE(text->toPlainText(), QString("original"));
        text->setFocus();
        text->selectAll();
        QTest::keyClicks(text, "changed");
        QVERIFY(text->document()->isModified());
        QTest::keyClick(text, Qt::Key_S, Qt::ControlModifier);
        QTRY_VERIFY(!text->document()->isModified());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("changed"));
        QTest::keyClicks(text, " unsaved");
        QTimer::singleShot(0, editor, [] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *message = qobject_cast<QMessageBox *>(widget)) message->button(QMessageBox::Cancel)->click();
        });
        QTest::keyClick(editor, Qt::Key_Escape);
        QVERIFY(editor && editor->isVisible());
        QTimer::singleShot(0, editor, [] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *message = qobject_cast<QMessageBox *>(widget)) message->button(QMessageBox::Discard)->click();
        });
        QTest::keyClick(editor, Qt::Key_Escape);
        QTRY_VERIFY(editor.isNull());
    }

    void editorPreservesConsistentCrlfNewlines()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("windows.txt");
        writeFile(path, "original\r\n");
        QPointer<FileContentDialog> editor = new FileContentDialog(nativePath(path), true);
        editor->show();
        auto *text = editor->findChild<QPlainTextEdit *>();
        QTRY_VERIFY(!text->isReadOnly());
        text->selectAll();
        text->insertPlainText("first\nsecond\n");
        text->setFocus();
        QTest::keyClick(text, Qt::Key_S, Qt::ControlModifier);
        QTRY_VERIFY(!text->document()->isModified());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("first\r\nsecond\r\n"));
        editor->close();
        QTRY_VERIFY(editor.isNull());
    }

    void archiveDialogListsAndExtractsThroughQueue()
    {
        QTemporaryDir directory;
        const auto root = nativePath(directory.path());
        writeFile(directory.filePath("source.txt"), "archive payload");
        const auto created = Archives::create({root / "source.txt"}, root / "source.zip", Archives::Format::Zip, std::make_shared<LocalFiles::Control>());
        QVERIFY(created.items.front().error.isEmpty());
        JobQueue queue;
        QPointer<ArchiveBrowserDialog> dialog = new ArchiveBrowserDialog(root / "source.zip", root, &queue);
        dialog->show();
        auto *entries = dialog->findChild<QTableWidget *>("archiveEntries");
        auto *extract = dialog->findChild<QPushButton *>("extractAll");
        QTRY_COMPARE(entries->rowCount(), 1); QTRY_VERIFY(extract->isEnabled());
        const auto screenshots = qEnvironmentVariable("TC_SCREENSHOT_DIR");
        if (!screenshots.isEmpty()) { QDir().mkpath(screenshots); QVERIFY(dialog->grab().save(screenshots + "/archive-browser.png")); }
        QTimer::singleShot(0, dialog, [dialog, directoryPath = directory.filePath("output")] {
            auto *input = dialog->findChild<QInputDialog *>(); QVERIFY(input); input->setTextValue(directoryPath); input->accept();
        });
        QTest::mouseClick(extract, Qt::LeftButton);
        QTRY_VERIFY(!queue.busy());
        QFile file(directory.filePath("output/source.txt")); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("archive payload"));
        dialog->close(); QTRY_VERIFY(dialog.isNull());
    }

    void multiRenamePreviewAppliesThroughQueue()
    {
        QTemporaryDir directory;
        writeFile(directory.filePath("a.txt"), "a"); writeFile(directory.filePath("b.txt"), "b");
        const auto root = nativePath(directory.path()); JobQueue queue;
        QPointer<BatchRenameDialog> dialog = new BatchRenameDialog({root / "a.txt", root / "b.txt"}, &queue, directory.filePath("jobs"));
        dialog->show();
        auto *pattern = dialog->findChild<QLineEdit *>("renamePattern"); auto *apply = dialog->findChild<QPushButton *>("applyRename");
        pattern->setText("item_[C]"); QTRY_VERIFY(apply->isEnabled());
        auto *preview = dialog->findChild<QTableWidget *>("renamePreview"); QCOMPARE(preview->rowCount(), 2); QCOMPARE(preview->item(0, 2)->text(), QString("item_001.txt"));
        const auto screenshots = qEnvironmentVariable("TC_SCREENSHOT_DIR");
        if (!screenshots.isEmpty()) { QDir().mkpath(screenshots); QVERIFY(dialog->grab().save(screenshots + "/multi-rename.png")); }
        QTest::mouseClick(apply, Qt::LeftButton); QTRY_VERIFY(!queue.busy());
        QVERIFY(QFile::exists(directory.filePath("item_001.txt"))); QVERIFY(QFile::exists(directory.filePath("item_002.txt"))); QVERIFY(!QFile::exists(directory.filePath("a.txt")));
        dialog->close(); QTRY_VERIFY(dialog.isNull());
    }

    void renameDefaultPreservesExtensionlessNames()
    {
        QTemporaryDir directory;writeFile(directory.filePath("README"));JobQueue queue;
        QPointer<BatchRenameDialog> dialog=new BatchRenameDialog({nativePath(directory.filePath("README"))},&queue,{});dialog->show();
        auto *table=dialog->findChild<QTableWidget *>("renamePreview");QTRY_COMPARE(table->rowCount(),1);
        QCOMPARE(table->item(0,2)->text(),QString("README"));QVERIFY(!dialog->findChild<QPushButton *>("applyRename")->isEnabled());
        dialog->findChild<QLineEdit *>("renamePattern")->setText("[UNSUPPORTED]");QTest::qWait(250);QVERIFY(!dialog->findChild<QPushButton *>("applyRename")->isEnabled());dialog->close();QTRY_VERIFY(dialog.isNull());
    }

    void directoryComparisonDialogApprovesCheckedUpdates()
    {
        QTemporaryDir directory;
        QVERIFY(QDir(directory.path()).mkdir("left")); QVERIFY(QDir(directory.path()).mkdir("right"));
        writeFile(directory.filePath("left/file.txt"), "new"); writeFile(directory.filePath("right/file.txt"), "old"); writeFile(directory.filePath("right/retain"), "keep");
        const auto root = nativePath(directory.path()); JobQueue queue;
        QPointer<DirectoryComparisonDialog> dialog = new DirectoryComparisonDialog(root / "left", root / "right", &queue, directory.filePath("jobs"));
        dialog->show(); auto *apply = dialog->findChild<QPushButton *>("applySync"); QTRY_VERIFY(apply->isEnabled());
        auto *table = dialog->findChild<QTableWidget *>("directoryComparison"); QCOMPARE(table->rowCount(), 2);
        const auto screenshots = qEnvironmentVariable("TC_SCREENSHOT_DIR");
        if (!screenshots.isEmpty()) { QDir().mkpath(screenshots); QVERIFY(dialog->grab().save(screenshots + "/directory-comparison.png")); }
        QTimer::singleShot(0, dialog, [] {
            for (auto *widget : QApplication::topLevelWidgets()) if (auto *message = qobject_cast<QMessageBox *>(widget)) message->button(QMessageBox::Yes)->click();
        });
        QTest::mouseClick(apply, Qt::LeftButton); QTRY_VERIFY(!queue.busy());
        QFile file(directory.filePath("right/file.txt")); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("new")); QVERIFY(QFile::exists(directory.filePath("right/retain")));
        dialog->close(); QTRY_VERIFY(dialog.isNull());
    }

    void savedSearchProfilesRestoreOptionsAndRoot()
    {
        QTemporaryDir directory;
        QPointer<SearchDialog> dialog = new SearchDialog(nativePath(directory.path())); dialog->show();
        auto *profiles = dialog->findChild<QComboBox *>("searchProfiles"); profiles->setEditText("fixture");
        auto *pattern = dialog->findChild<QLineEdit *>("searchPattern"); auto *minimum = dialog->findChild<QLineEdit *>("searchMinBytes");
        pattern->setText("*.txt"); minimum->setText("9");
        QTest::mouseClick(dialog->findChild<QPushButton *>("saveSearchProfile"), Qt::LeftButton);
        QVERIFY(QSettings().value("searchProfiles").toMap().contains("fixture"));
        pattern->setText("*.bin"); minimum->clear();
        QVERIFY(QMetaObject::invokeMethod(profiles, "textActivated", Qt::DirectConnection, Q_ARG(QString, QString("fixture"))));
        QCOMPARE(pattern->text(), QString("*.txt")); QCOMPARE(minimum->text(), QString("9"));
        dialog->close(); QTRY_VERIFY(dialog.isNull());
    }

    void queueWindowRemainsResponsiveWithBusyGlobalPool()
    {
        struct RestorePool {
            int previous = QThreadPool::globalInstance()->maxThreadCount();
            RestorePool() { QThreadPool::globalInstance()->setMaxThreadCount(1); }
            ~RestorePool() { QThreadPool::globalInstance()->setMaxThreadCount(previous); }
        } restorePool;
        JobQueue queue;
        const auto id = queue.enqueue("Long-running fixture", [](const auto &control) -> LocalFiles::Result {
            while (true) { LocalFiles::checkpoint(control); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        });
        queue.enqueue("Waiting fixture", [](const auto &) { return LocalFiles::Result{}; });
        QPointer<JobQueueDialog> dialog = new JobQueueDialog(&queue); dialog->show();
        auto *table = dialog->findChild<QTableWidget *>("jobTable"); QTRY_COMPARE(table->rowCount(), 2);
        QTRY_VERIFY(queue.jobs().front().state == JobQueue::State::Running); table->setCurrentCell(0, 0);
        QTest::mouseClick(dialog->findChild<QPushButton *>("queuePause"), Qt::LeftButton); QVERIFY(queue.jobs().front().state == JobQueue::State::Paused);
        const auto screenshots = qEnvironmentVariable("TC_SCREENSHOT_DIR");
        if (!screenshots.isEmpty()) { QDir().mkpath(screenshots); QVERIFY(dialog->grab().save(screenshots + "/background-operations.png")); }
        QTest::mouseClick(dialog->findChild<QPushButton *>("queueResume"), Qt::LeftButton); QVERIFY(queue.jobs().front().state == JobQueue::State::Running);
        QTest::mouseClick(dialog->findChild<QPushButton *>("queueCancel"), Qt::LeftButton); QTRY_VERIFY(!queue.busy()); QVERIFY(queue.jobs().front().state == JobQueue::State::Cancelled);
        QCOMPARE(queue.jobs().front().id, id); dialog->close(); QTRY_VERIFY(dialog.isNull());
    }

    void swapCurrentPanelDirectories()
    {
        QTemporaryDir left, right; MainWindow window({left.path(), right.path()});
        auto *a = window.activePanel();
        QTRY_VERIFY(a->path() == nativePath(left.path()));
        for (auto *panel : window.findChildren<FilePanel *>()) QTRY_VERIFY(!panel->path().empty());
        window.findChild<QAction *>("cm_Exchange")->trigger(); QTRY_VERIFY(a->path() == nativePath(right.path()));
        for (auto *panel : window.findChildren<FilePanel *>()) if (panel != a) QTRY_VERIFY(panel->path() == nativePath(left.path()));
    }

    void symlinkParentUsesFilesystemSemantics()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir directory;
        QVERIFY(QDir(directory.path()).mkpath("actual/child"));
        const auto root = nativePath(directory.path());
        std::filesystem::create_directory_symlink(root / "actual/child", root / "shortcut");
        FilePanel panel(root);
        QTRY_VERIFY(panel.path() == root);
        panel.navigate(root / "shortcut/..");
        QTRY_VERIFY(panel.path() == root / "actual");
#else
        QSKIP("Linux symlink navigation behavior");
#endif
    }
};

QTEST_MAIN(CommanderTests)
#include "commander_tests.moc"

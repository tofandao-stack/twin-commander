#include "mainwindow.h"
#include "advancedwindows.h"
#include "toolwindows.h"
#include "productivitytools.h"
#include "classicui.h"
#include "referenceforms.h"
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
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
#include <QPlainTextEdit>
#include <QPrintDialog>
#include <QPrinter>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTableView>
#include <QTabWidget>
#include <QTextDocument>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QMessageBox>

namespace {
void failure(QWidget *parent,const std::exception &error){QMessageBox::warning(parent,"Twin Commander",QString::fromLocal8Bit(error.what()));}
void printText(QWidget *parent,const QString &text){QPrinter printer;QPrintDialog dialog(&printer,parent);if(dialog.exec()==QDialog::Accepted){QTextDocument document;document.setPlainText(text);document.print(&printer);}}
}
void MainWindow::registerProductivityCommands()
{
    commands_.add("cm_SaveSelectionToFile","Save selection to file",{},[this]{const auto filename=QFileDialog::getSaveFileName(this,"Save selection to a new file",{},"Selection (*.tcselection)");if(filename.isEmpty())return;try{Productivity::exportNew(nativePath(filename),Productivity::selectionData(active_->selectedPaths()));}catch(const std::exception &e){failure(this,e);}});
    commands_.add("cm_LoadSelectionFromFile","Load selection from file",{},[this]{const auto filename=QFileDialog::getOpenFileName(this,"Load selection",{},"Selection (*.tcselection)");if(filename.isEmpty())return;try{active_->selectPaths(Productivity::selectionPaths(Productivity::readSmall(nativePath(filename)),active_->path()));}catch(const std::exception &e){failure(this,e);}});
    commands_.add("cm_HideSame","Mark newer and hide same",{},[this]{commands_.get("cm_CompareDirs")->trigger();active_->filterView({},false,true);});
    commands_.add("cm_DirectoryTree","CD Tree",{},[this]{const auto path=QFileDialog::getExistingDirectory(this,"CD Tree",displayPath(active_->path()),QFileDialog::ShowDirsOnly|QFileDialog::DontUseNativeDialog);if(!path.isEmpty())active_->navigate(nativePath(path));});
    commands_.add("cm_SearchSeparate","Search in separate process",{},[this]{if(!QProcess::startDetached(QCoreApplication::applicationFilePath(),{"--search",displayPath(active_->path())}))QMessageBox::warning(this,"Find Files","Cannot start a separate search process.");});
    commands_.add("cm_SystemInfo","System information",{},[this]{const auto text=QSysInfo::prettyProductName()+"\nKernel: "+QSysInfo::kernelType()+" "+QSysInfo::kernelVersion()+"\nArchitecture: "+QSysInfo::currentCpuArchitecture()+"\nQt: "+qVersion()+"\nTwin Commander: "+QCoreApplication::applicationVersion();(new ReportDialog("System information",[text](const auto &){return text;},this))->show();});
    commands_.add("cm_Terminal","Open command prompt",{},[this]{for(const auto &candidate:{QString("x-terminal-emulator"),QString("gnome-terminal"),QString("konsole"),QString("xterm")}){const auto executable=QStandardPaths::findExecutable(candidate);if(!executable.isEmpty()&&QProcess::startDetached(executable,{},displayPath(active_->path())))return;}QMessageBox::warning(this,"Terminal","No supported terminal could be started (x-terminal-emulator, gnome-terminal, konsole or xterm).");});
    commands_.add("cm_Desktop","Open Desktop Folder",{},[this]{const auto path=QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);if(QDir(path).exists())active_->navigate(nativePath(path));else QMessageBox::information(this,"Desktop","The configured desktop directory does not exist: "+path);});
    commands_.add("cm_Programs","Programs",{},[this]{active_->filterView({},true);});
    commands_.add("cm_AllFiles","*.*",{},[this]{active_->clearFilter();active_->filterView();});
    commands_.add("cm_CustomFilter","Custom filter",{},[this]{bool ok=false;const auto mask=QInputDialog::getText(this,"Custom file filter","Filename masks separated by semicolons:",QLineEdit::Normal,QSettings().value("lastCustomMask","*").toString(),&ok);if(ok){QSettings().setValue("lastCustomMask",mask);active_->filterView(mask);}});
    commands_.add("cm_OnlySelected","Only selected files",{},[this]{active_->filterView({},false,true);});
    commands_.add("cm_Unsorted","Unsorted",{},[this]{active_->table()->sortByColumn(-1,Qt::AscendingOrder);});
    commands_.add("cm_EditSettings","Change settings files directly",{},[this]{saveSession();QSettings settings;settings.sync();(new FileWindow(nativePath(settings.fileName()),true,this))->show();});
    for(const auto &pair:{std::pair<QString,bool>{"cm_PrintList",false},{"cm_PrintListRecursive",true}})commands_.add(pair.first,"File list",{},[this,recursive=pair.second]{const auto paths=active_->selectedPaths();if(paths.empty())return;auto *report=new ReportDialog(recursive?"File list with subdirectories":"File list",[paths,recursive](const auto &control){return Productivity::fileList(paths,recursive,control);},this);auto *row=new QHBoxLayout;auto *save=new QPushButton("Save list…",report),*print=new QPushButton("Print…",report);save->setEnabled(false);print->setEnabled(false);connect(report,&ReportDialog::ready,report,[save,print]{save->setEnabled(true);print->setEnabled(true);});row->addWidget(save);row->addWidget(print);qobject_cast<QVBoxLayout *>(report->layout())->addLayout(row);auto *text=report->findChild<QPlainTextEdit *>();connect(print,&QPushButton::clicked,report,[report,text]{printText(report,text->toPlainText());});connect(save,&QPushButton::clicked,report,[report,text]{const auto path=QFileDialog::getSaveFileName(report,"Save file list to a new file");if(path.isEmpty())return;try{Productivity::exportNew(nativePath(path),text->toPlainText().toUtf8());}catch(const std::exception &e){failure(report,e);}});report->show();});
    commands_.add("cm_PrintContents","Print file contents",{},[this]{const auto paths=active_->selectedPaths();if(paths.empty())return;try{const auto bytes=Productivity::readSmall(paths.front());if(bytes.contains('\0')||QString::fromUtf8(bytes).toUtf8()!=bytes)throw NativeFs::Error("Print contents requires lossless UTF-8 text within 8 MiB");printText(this,QString::fromUtf8(bytes));}catch(const std::exception &e){failure(this,e);}});
    commands_.add("cm_CreateChecksumFile","Create checksum files",{},[this]{const auto paths=active_->selectedPaths();if(paths.empty())return;bool ok=false;const auto choice=QInputDialog::getItem(this,"Create checksum file","Algorithm:",{"SHA256","SHA1","MD5"},0,false,&ok);if(!ok)return;const auto output=QFileDialog::getSaveFileName(this,"Create a new checksum file",displayPath(active_->path())+"/checksums."+choice.toLower());if(output.isEmpty())return;const auto algorithm=choice=="MD5"?QCryptographicHash::Md5:choice=="SHA1"?QCryptographicHash::Sha1:QCryptographicHash::Sha256;(new ReportDialog("Create checksums",[paths,output,algorithm](const auto &control){const auto data=Productivity::checksumManifest(paths,algorithm,control);LocalFiles::checkpoint(control);Productivity::exportNew(nativePath(output),data);return "Saved "+output+"\n"+QString::fromUtf8(data);},this))->show();});
    commands_.add("cm_VerifyChecksumFile","Verify checksum files",{},[this]{const auto paths=active_->selectedPaths();if(paths.empty())return;(new ReportDialog("Verify checksums",[paths](const auto &control){QStringList reports;for(const auto &path:paths)reports<<displayPath(path)+"\n"+Productivity::verifyManifest(path,control);return reports.join("\n\n");},this))->show();});
    commands_.add("cm_TestArchive","Test archives",{},[this]{const auto paths=active_->selectedPaths();if(paths.empty())return;(new ReportDialog("Test archives",[paths](const auto &control){QStringList reports;for(const auto &path:paths){const auto result=Archives::list(path,control);if(result.cancelled)break;QStringList unsafe;for(const auto &entry:result.entries)if(!entry.unsafe.isEmpty())unsafe<<QString::fromStdString(entry.name)+": "+entry.unsafe;reports<<displayPath(path)+": "+(result.error.isEmpty()?QString("OK — %1 entries read and validated").arg(result.entries.size()):result.error);if(!unsafe.isEmpty())reports<<"Extraction warnings:\n"+unsafe.join('\n');}return reports.join('\n');},this))->show();});
    commands_.add("cm_ViewModes","Custom view modes",{},[this]{const auto modes=QSettings().value("viewModes").toMap();if(modes.isEmpty()){commands_.get("cm_Config")->trigger();if(auto *dialog=findChild<QDialog *>("referenceConfiguration"))dialog->findChild<QListWidget *>("configurationCategories")->setCurrentRow(8);return;}bool ok=false;const auto name=QInputDialog::getItem(this,"View modes","Apply saved view mode:",modes.keys(),0,false,&ok);if(!ok)return;const auto mode=modes.value(name).toMap();applyViewMode(mode);});
}

#include "mainwindow.h"
#include "toolwindows.h"
#include <QDesktopServices>
#include <QUrl>

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("TwinCommander");
    QCoreApplication::setApplicationName("TwinCommander");
    QCoreApplication::setApplicationVersion("0.7.0");
    QCommandLineParser parser;
    parser.setApplicationDescription("Native Linux two-panel file manager");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"search", "Open an independent search window in directory", "directory"});
    parser.addPositionalArgument("paths", "Initial left and right directories", "[left] [right]");
    parser.process(application);
    if (parser.positionalArguments().size() > 2) parser.showHelp(1);
    if(parser.isSet("search")){
        auto *search=new SearchDialog(nativePath(parser.value("search")));
        QObject::connect(search,&SearchDialog::revealRequested,search,[](const auto &path){QDesktopServices::openUrl(QUrl::fromLocalFile(displayPath(path.parent_path())));});
        QObject::connect(search,&SearchDialog::resultsRequested,search,[root=parser.value("search")](const auto &paths){auto *window=new MainWindow({root,root});window->setAttribute(Qt::WA_DeleteOnClose);window->show();QObject::connect(window->activePanel()->model(),&DirectoryModel::loaded,window,[window,paths]{if(!window->property("fed").toBool()){window->setProperty("fed",true);window->showSearchResults(paths);}});});
        search->show();return application.exec();
    }
    MainWindow window(parser.positionalArguments());
    window.show();
    return application.exec();
}

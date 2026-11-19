#include "ui/icons/commandicons.h"
#include "filesystem/pathencoding.h"
#include "settings/applicationpreferences.h"
#include "app/mainwindow.h"
#include "ui/dialogs/filecontentandsearchdialogs.h"
#include <QDesktopServices>
#include <QMessageBox>
#include <QUrl>

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("TwinCommander");
    QCoreApplication::setApplicationName("TwinCommander");
    QCoreApplication::setApplicationVersion("0.9.0");
    QCommandLineParser parser;
    parser.setApplicationDescription("Native Linux two-panel file manager");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"search", "Open an independent search window in directory", "directory"});
    parser.addPositionalArgument("paths", "Initial left and right directories", "[left] [right]");
    parser.process(application);
    CommandIcons::install();
    application.setDesktopFileName("twin-commander");
    if (parser.positionalArguments().size() > 2)
        parser.showHelp(1);
    if (parser.isSet("search")) {
        auto *search = new SearchDialog(nativePath(parser.value("search")));
        QObject::connect(search, &SearchDialog::revealRequested, search, [](const auto &path) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(displayPath(path.parent_path())));
        });
        QObject::connect(search, &SearchDialog::resultsRequested, search,
                         [root = parser.value("search")](const auto &paths) {
                             auto *window = new MainWindow({root, root});
                             window->setAttribute(Qt::WA_DeleteOnClose);
                             window->show();
                             QObject::connect(window->activePanel()->model(), &DirectoryModel::loaded, window,
                                              [window, paths] {
                                                  if (!window->property("fed").toBool()) {
                                                      window->setProperty("fed", true);
                                                      window->showSearchResults(paths);
                                                  }
                                              });
                         });
        search->show();
        return application.exec();
    }
    if (!ApplicationPreferences::instanceLock()) {
        QMessageBox::information(nullptr, "Twin Commander", "This profile is already open.");
        return 1;
    }
    MainWindow window(parser.positionalArguments());
    window.show();
    return application.exec();
}

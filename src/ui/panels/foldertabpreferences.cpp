#include "ui/panels/foldertabpreferences.h"
#include "filesystem/pathencoding.h"
#include "settings/applicationpreferences.h"
#include "ui/panels/filepanel.h"
#include <QMenu>
#include <QTabWidget>
using ApplicationPreferences::flag;

void FolderTabPreferences::showContextMenu(QTabWidget *tabs, int index, const QPoint &point) {
    if (index < 0 || index >= tabs->count())
        return;
    auto *panel = tabs->widget(index);
    auto *menu = new QMenu(tabs);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto *lock = menu->addAction(
        panel->property("lockedRoot").toByteArray().isEmpty() ? "Lock this tab's root" : "Unlock this tab");
    QObject::connect(lock, &QAction::triggered, panel, [panel, tabs] {
        auto *p = qobject_cast<FilePanel *>(panel);
        panel->setProperty("lockedRoot", panel->property("lockedRoot").toByteArray().isEmpty()
                                             ? QByteArray::fromStdString(p->path().native())
                                             : QByteArray());
        const auto title = displayPath(p->path().filename());
        tabs->setTabText(
            tabs->indexOf(panel),
            title + (flag("ChkTabsLockedAsterisk") && !panel->property("lockedRoot").toByteArray().isEmpty()
                         ? "*"
                         : ""));
    });
    menu->popup(point);
}

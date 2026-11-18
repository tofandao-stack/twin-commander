#pragma once
#include <QIcon>
#include <QJsonObject>
#include <QWidget>
#include <functional>

class QComboBox;
class QLabel;
class QToolButton;
class QTabWidget;
class FilePanel;
QJsonObject commanderUiReference();
QJsonObject commanderCommandIds();
QIcon commanderIcon(const QString &name, int size = 24);
void applyCommanderAppearance();
void applySavedAppearance();

class PanelHost : public QWidget {
    Q_OBJECT
  public:
    explicit PanelHost(QTabWidget *tabs, QWidget *parent = nullptr);
    void refresh();
    void showDriveButtons(bool visible);

  private:
    QTabWidget *tabs_;
    QWidget *drives_;
    QComboBox *drive_;
    QLabel *space_;
    bool updating_ = false;
};

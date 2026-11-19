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

class FilePanelHost : public QWidget {
    Q_OBJECT
  public:
    explicit FilePanelHost(QTabWidget *tabs, QWidget *parent = nullptr);
    void refresh();
    void showDriveButtons(bool visible);
    void updateTabLayout(bool visible = true);

  protected:
    void resizeEvent(QResizeEvent *) override;

  private:
  private:
    QTabWidget *tabs_;
    QWidget *drives_;
    QWidget *wrappedTabs_ = nullptr;
    QWidget *wrappedScroller_ = nullptr;
    bool tabLayoutVisible_ = true;
    QComboBox *drive_;
    QLabel *space_;
    bool updating_ = false;
};

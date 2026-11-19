#include "ui/settings/referenceformrenderer.h"
#include "ui/panels/filepanellayout.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>

QJsonObject referenceDialogMetadata(const QString &name)
{
    commanderUiReference();
    QFile file(":/reference/tc-11.58.1-dialogs.json");
    if (!file.open(QIODevice::ReadOnly)) qFatal("Missing reference dialog catalog");
    return QJsonDocument::fromJson(file.readAll()).object().value(name).toObject();
}
QWidget *renderReferenceControls(const QJsonObject &node, QWidget *parent, const QSet<QString> &enabled,
                                const QMap<QString, QVariant> &values, int topOffset)
{
    const auto type = node.value("type").toString(), name = node.value("name").toString();
    const auto properties = node.value("properties").toObject();
    const auto caption = properties.value("Caption").toString();
    QWidget *widget = nullptr; bool input = false, group = false;
    if (type.contains("GroupBox")) { widget = new QGroupBox(caption, parent); group = true; }
    else if (type.contains("CheckBox")) {
        auto *check = new QCheckBox(caption, parent); check->setTristate(properties.value("State").toString() == "cbGrayed");
        check->setCheckState(properties.value("State").toString() == "cbGrayed" ? Qt::PartiallyChecked : properties.value("Checked").toBool() ? Qt::Checked : Qt::Unchecked);
        if (values.contains(name)) check->setChecked(values.value(name).toBool());
        widget = check; input = true;
    } else if (type.contains("RadioButton")) { auto *radio = new QRadioButton(caption, parent); radio->setChecked(properties.value("Checked").toBool()); widget = radio; input = true; }
    else if (type.contains("SpeedButton") || type == "TBitBtn") { auto *button = new QToolButton(parent); button->setText(caption.isEmpty() ? "..." : caption); widget = button; input = true; }
    else if (type == "TButton") { widget = new QPushButton(caption, parent); input = true; }
    else if (type.contains("ComboBox") || type.contains("ColorBox")) {
        auto *combo = new QComboBox(parent); combo->setEditable(properties.value("Style").toString() != "csDropDownList");
        for (const auto &item : properties.value("Items.Strings").toArray()) combo->addItem(item.toString());
        if (properties.contains("ItemIndex")) combo->setCurrentIndex(properties.value("ItemIndex").toInt());
        if (properties.contains("Text")) combo->setCurrentText(properties.value("Text").toString());
        widget = combo; input = true;
    } else if (type.contains("ListBox")) {
        auto *list = new QListWidget(parent); for (const auto &item : properties.value("Items.Strings").toArray()) list->addItem(item.toString()); widget = list; input = true;
    } else if (type.contains("Memo")) { auto *text = new QPlainTextEdit(parent); QStringList lines; for (const auto &line : properties.value("Lines.Strings").toArray()) lines << line.toString(); text->setPlainText(lines.join('\n')); widget = text; input = true; }
    else if (type.contains("Spin") || type.contains("UpDown")) { auto *spin = new QSpinBox(parent); spin->setRange(properties.value("MinValue").toInt(), properties.value("MaxValue").toInt(10000)); widget = spin; input = true; }
    else if (type.contains("Edit")) { auto *edit = new QLineEdit(properties.value("Text").toString(), parent); widget = edit; input = true; }
    else if (type.contains("TrackBar")) { widget = new QSlider(Qt::Horizontal, parent); input = true; }
    else if (type.contains("Label") || type == "TImage") { auto *label = new QLabel(caption, parent); label->setWordWrap(properties.value("WordWrap").toBool()); widget = label; }
    else if (type.contains("Bevel")) { auto *frame = new QFrame(parent); frame->setFrameShape(QFrame::HLine); widget = frame; }
    else widget = new QWidget(parent);
    widget->setObjectName(name);
    widget->setGeometry(properties.value("Left").toInt(), properties.value("Top").toInt() + topOffset,
                        properties.value("Width").toInt(parent ? parent->width() : 80), properties.value("Height").toInt(parent ? parent->height() : 20));
    if (input && !enabled.contains(name)) { widget->setEnabled(false); widget->setToolTip("This reference control is not implemented in Twin Commander yet."); }
    for (const auto &child : node.value("children").toArray()) renderReferenceControls(child.toObject(), widget, enabled, values, group ? 16 : 0);
    if (properties.contains("Visible") && !properties.value("Visible").toBool()) widget->hide();
    else widget->show();
    return widget;
}

#pragma once
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QVariant>
#include <QWidget>

QJsonObject referenceDialogMetadata(const QString &name);
QWidget *renderReferenceControls(const QJsonObject &node, QWidget *parent, const QSet<QString> &enabled,
                                const QMap<QString, QVariant> &values = {}, int topOffset = 0);

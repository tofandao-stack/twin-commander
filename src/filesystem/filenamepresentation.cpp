#include "filesystem/filenamepresentation.h"
#include <QRegularExpression>

QString FilenamePresentation::dosAlias(const QString &name, int serial) {
    const auto dot = name.lastIndexOf('.');
    auto stem = dot > 0 ? name.left(dot) : name, extension = dot > 0 ? name.mid(dot + 1) : QString();
    const auto clean = [](QString s) {
        s = s.toUpper();
        s.remove(QRegularExpression("[^A-Z0-9_$~!#%&{}()@'`-]"));
        return s;
    };
    stem = clean(stem);
    extension = clean(extension).left(3);
    if (stem.isEmpty())
        stem = "FILE";
    if (serial > 1 || stem.size() > 8 || name.contains(' ') || name.toUtf8().size() != name.size()) {
        const auto suffix = "~" + QString::number(serial);
        stem = stem.left(qMax(1, 8 - suffix.size())) + suffix;
    }
    return stem.left(8) + (extension.isEmpty() ? QString() : "." + extension);
}

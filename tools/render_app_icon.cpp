// Regenerate the native PNG launcher assets using Qt Gui, with no SVG plugin dependency.
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    if (argc != 2)
        return 1;
    for (int size : {16, 32, 48, 64, 128, 256, 512}) {
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 64., size / 64.);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#183d68"));
        p.drawRoundedRect(QRectF(2, 2, 60, 60), 12, 12);
        p.setBrush(QColor("#e8f2fa"));
        p.drawRoundedRect(QRectF(9, 12, 21, 40), 3, 3);
        p.drawRoundedRect(QRectF(34, 12, 21, 40), 3, 3);
        p.setBrush(QColor("#62abe0"));
        p.drawRect(QRectF(11, 15, 17, 7));
        p.drawRect(QRectF(36, 15, 17, 7));
        p.setPen(QPen(QColor("#7693af"), 2));
        for (int y : {28, 35, 42}) {
            p.drawLine(14, y, 25, y);
            p.drawLine(39, y, 50, y);
        }
        p.setPen(QPen(QColor("#e9ac38"), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(23, 48, 41, 48);
        p.drawPolyline(QPolygonF{{36, 43}, {41, 48}, {36, 53}});
        p.end();
        if (!image.save(QDir(argv[1]).filePath(QString("twin-commander-%1.png").arg(size))))
            return 2;
    }
}

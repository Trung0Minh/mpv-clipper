#include "Theme.h"
#include <QApplication>
#include <QFile>
#include <QPalette>

void applyTheme()
{
    Q_INIT_RESOURCE(theme);
    qApp->setStyle("Fusion");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#110e14"));
    palette.setColor(QPalette::WindowText, QColor("#f5f3ff"));
    palette.setColor(QPalette::Base, QColor("#0f0c13"));
    palette.setColor(QPalette::AlternateBase, QColor("#17121c"));
    palette.setColor(QPalette::Text, QColor("#ede9fe"));
    palette.setColor(QPalette::Button, QColor("#201727"));
    palette.setColor(QPalette::ButtonText, QColor("#ede9fe"));
    palette.setColor(QPalette::Highlight, QColor("#5a2963"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, QColor("#1e1625"));
    palette.setColor(QPalette::ToolTipText, Qt::white);
    qApp->setPalette(palette);
    QFile file(":/theme.qss");
    if (file.open(QIODevice::ReadOnly)) qApp->setStyleSheet(QString::fromUtf8(file.readAll()));
}

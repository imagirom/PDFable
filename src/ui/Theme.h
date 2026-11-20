// Look and feel: Fusion-based light/dark theme and the line icons used in the toolbar.
#pragma once
#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;

namespace Theme {
void apply(QApplication &app);
bool isDark();
QColor accent();
QIcon icon(const QString &name);
}

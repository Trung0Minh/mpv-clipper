#pragma once
#include <QComboBox>
#include <QAbstractItemView>
#include <QScreen>
#include <algorithm>

class SettingsComboBox : public QComboBox {
public:
    using QComboBox::QComboBox;
    void showPopup() override
    {
        QComboBox::showPopup();
        auto *popup = view()->window();
        const QPoint anchor = mapToGlobal(QPoint(0, height()));
        const QRect available = screen()->availableGeometry();
        // Keep the list below its control; scroll rather than cover the selection.
        popup->resize(width(), std::min(popup->height(), std::max(1, available.bottom() - anchor.y() + 1)));
        popup->move(anchor);
    }
};

#pragma once
#include <QWidget>
#include <QElapsedTimer>
class RangeTimeline : public QWidget {
    Q_OBJECT
public:
    explicit RangeTimeline(QWidget *parent = nullptr);
    void setRange(double duration, double start, double end, double position);
    void setClips(const QList<QPair<double, double>> &ranges, int active);
    void zoom(double factor);
    void zoomToClip();
    void fitVideo();
    double visibleStart() const { return viewStart; }
    double visibleEnd() const { return viewStart + viewSpan; }
    bool dragging() const { return drag >= 1 && drag <= 3; }
    int selectedBoundary() const { return selected; }
signals:
    void clipSelected(int index);
    void edited(int boundary, double value, bool final);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
private:
    void selectBoundary(int boundary) { selected = boundary; update(); }
    double at(double x) const;
    double x(double time) const;
    void move(QMouseEvent *event, bool final);
    void setView(double first, double span);
    void zoomAt(double factor, double fraction);
    int markerAt(double px) const;
    double duration = 0, start = 0, end = 0, position = 0;
    double viewStart = 0, viewSpan = 0;
    double overviewAnchor = 0, overviewStart = 0;
    QList<QPair<double, double>> clips;
    int activeClip = -1;
    int drag = 0;
    int selected = 0;
    double dragOffset = 0;
    QElapsedTimer throttle;
};

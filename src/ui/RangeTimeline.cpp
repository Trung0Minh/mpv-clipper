#include "RangeTimeline.h"
#include "media/Timecode.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>
#include <limits>
#include <algorithm>
RangeTimeline::RangeTimeline(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(78); setMouseTracking(true);
    setAccessibleName("Clip range timeline");
    setToolTip("Wheel to zoom at the pointer. Drag markers to trim; click to seek. Drag the lower overview to pan.");
    throttle.start();
}
void RangeTimeline::setRange(double d, double a, double b, double p)
{
    bool sourceChanged = d != duration;
    duration = d; start = a; end = b; position = p;
    if (sourceChanged) { drag = 0; selected = 0; fitVideo(); }
    update();
}
void RangeTimeline::setClips(const QList<QPair<double, double>> &ranges, int active)
{ if (activeClip != active) selected = 0; clips = ranges; activeClip = active; update(); }
void RangeTimeline::setView(double first, double span)
{
    if (duration <= 0) { viewStart = viewSpan = 0; update(); return; }
    viewSpan = std::clamp(span, std::min(0.1, duration), duration);
    viewStart = std::clamp(first, 0.0, duration - viewSpan);
    update();
}
void RangeTimeline::fitVideo() { setView(0, duration); }
void RangeTimeline::zoomToClip()
{
    double padding = std::max(0.25, (end - start) * 0.15);
    setView(start - padding, end - start + padding * 2);
}
void RangeTimeline::zoom(double factor) { zoomAt(factor, 0.5); }
void RangeTimeline::zoomAt(double factor, double fraction)
{
    if (duration <= 0 || drag || !std::isfinite(factor) || factor <= 0) return;
    double span = std::clamp(viewSpan / factor, std::min(0.1, duration), duration);
    setView(viewStart + fraction * (viewSpan - span), span);
}
void RangeTimeline::wheelEvent(QWheelEvent *event)
{
    double steps = event->angleDelta().y() / 120.0;
    if (!event->pixelDelta().isNull()) steps = event->pixelDelta().y() / 60.0;
    zoomAt(std::pow(1.1, std::clamp(steps, -8.0, 8.0)),
           std::clamp(event->position().x() / std::max(1, width()), 0.0, 1.0));
    event->accept();
}
double RangeTimeline::x(double t) const { return viewSpan > 0 ? (t - viewStart) / viewSpan * width() : 0; }
double RangeTimeline::at(double px) const { return viewStart + std::clamp(px / std::max(1, width()), 0.0, 1.0) * viewSpan; }
int RangeTimeline::markerAt(double px) const
{
    auto distance = [&](double t) {
        return t >= viewStart && t <= visibleEnd() ? std::abs(px - x(t)) : std::numeric_limits<double>::infinity();
    };
    double a = distance(start), b = distance(end);
    return std::min(a, b) < 12 ? (a <= b ? 1 : 2) : 0;
}
void RangeTimeline::paintEvent(QPaintEvent *)
{
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen); p.setBrush(palette().alternateBase());
    p.drawRoundedRect(QRectF(0, 8, width(), 22), 4, 4);
    auto segment = [&](double a, double b) {
        a = std::clamp(a, viewStart, visibleEnd()); b = std::clamp(b, viewStart, visibleEnd());
        return QRectF(x(a), 8, std::max(0.0, x(b) - x(a)), 22);
    };
    QColor inactive = palette().highlight().color(); inactive.setAlpha(80);
    for (int i = 0; i < clips.size(); ++i) if (i != activeClip)
        p.fillRect(segment(clips[i].first, clips[i].second), inactive);
    QLinearGradient selection(0, 0, width(), 0);
    selection.setColorAt(0, QColor("#461b4d")); selection.setColorAt(1, QColor("#6b3c74"));
    p.setBrush(selection); p.drawRoundedRect(segment(start, end), 4, 4);
    p.setPen(palette().text().color());
    p.drawText(QRect(0, 36, width() / 2, 20), Qt::AlignLeft, timecode(viewStart));
    p.drawText(QRect(width() / 2, 36, width() - width() / 2, 20), Qt::AlignRight, timecode(visibleEnd()));
    if (duration <= 0) return;
    for (double boundary : {start, end}) if (boundary >= viewStart && boundary <= visibleEnd())
        p.fillRect(QRectF(std::clamp(x(boundary) - 1.5, 0.0, double(width() - 3)), 4, 3, 30), QColor("#c084fc"));
    double selectedTime = selected == 1 ? start : end;
    if (selected && selectedTime >= viewStart && selectedTime <= visibleEnd()) {
        p.setPen(QPen(QColor("#ffffff"), 2));
        p.drawRect(QRectF(std::clamp(x(selectedTime) - 4, 1.0, double(width() - 9)), 3, 8, 32));
    }
    p.setPen(QPen(QColor("#ffffff"), 2));
    if (position >= viewStart && position <= visibleEnd())
        p.drawLine(QPointF(x(position), 0), QPointF(x(position), 34));
    // Full-source overview stays fixed while the detailed timeline zooms.
    auto overviewX = [&](double t) { return t / duration * width(); };
    p.setPen(Qt::NoPen); p.setBrush(QColor("#201727"));
    p.drawRoundedRect(QRectF(0, 62, width(), 12), 3, 3);
    p.fillRect(QRectF(overviewX(start), 64, std::max(2.0, overviewX(end - start)), 8), QColor("#6b3c74"));
    double windowWidth = std::min(double(width()), std::max(12.0, overviewX(viewSpan)));
    double left = std::clamp(overviewX(viewStart), 0.0, width() - windowWidth);
    p.setBrush(QColor(192, 132, 252, 35)); p.setPen(QColor("#c084fc"));
    p.drawRoundedRect(QRectF(left, 61, windowWidth, 14).adjusted(0.5, 0, -0.5, 0), 3, 3);
}
void RangeTimeline::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || duration <= 0) return;
    double px = event->position().x();
    if (event->position().y() >= 60) {
        double overviewTime = std::clamp(px / std::max(1, width()), 0.0, 1.0) * duration;
        if (overviewTime < viewStart || overviewTime > visibleEnd()) setView(overviewTime - viewSpan / 2, viewSpan);
        overviewAnchor = px; overviewStart = viewStart; drag = 4; setCursor(Qt::ClosedHandCursor); return;
    }
    if (event->position().y() > 34) return;
    int marker = markerAt(px);
    if (!marker && (at(px) < start || at(px) > end)) {
        for (int i = 0; i < clips.size(); ++i) if (i != activeClip && at(px) >= clips[i].first && at(px) <= clips[i].second) {
            emit clipSelected(i); return;
        }
    }
    drag = marker ? marker : 3;
    selectBoundary(marker);
    dragOffset = marker ? (marker == 1 ? start : end) - at(px) : 0;
    emit edited(drag, at(px) + dragOffset, false); throttle.restart();
}
void RangeTimeline::mouseMoveEvent(QMouseEvent *event)
{
    if (drag) move(event, false);
    if (drag == 4) { setCursor(Qt::ClosedHandCursor); return; }
    bool marker = duration > 0 && event->position().y() <= 34 && markerAt(event->position().x());
    setCursor(marker || drag == 1 || drag == 2 ? Qt::SizeHorCursor :
              event->position().y() >= 60 && duration > 0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
}
void RangeTimeline::mouseReleaseEvent(QMouseEvent *event)
{ if (drag && event->button() == Qt::LeftButton) { move(event, true); drag = 0; } }
void RangeTimeline::move(QMouseEvent *event, bool final)
{
    if (drag == 4) {
        setView(overviewStart + (event->position().x() - overviewAnchor) / std::max(1, width()) * duration, viewSpan);
        return;
    }
    if (final || throttle.elapsed() >= 16) { emit edited(drag, at(event->position().x()) + dragOffset, final); throttle.restart(); }
}

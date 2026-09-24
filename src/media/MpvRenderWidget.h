#pragma once
#include <QOpenGLWidget>
#include <mpv/render_gl.h>
class MpvPlayer;
class MpvRenderWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit MpvRenderWidget(MpvPlayer *player, QWidget *parent = nullptr);
    ~MpvRenderWidget();
signals:
    void ready();
    void failed(QString message);
protected:
    void initializeGL() override;
    void paintGL() override;
private:
    void cleanup();
    MpvPlayer *player;
    mpv_render_context *render = nullptr;
};

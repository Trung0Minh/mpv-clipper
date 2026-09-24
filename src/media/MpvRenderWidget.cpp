#include "MpvRenderWidget.h"
#include "MpvPlayer.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions>

MpvRenderWidget::MpvRenderWidget(MpvPlayer *player, QWidget *parent)
    : QOpenGLWidget(parent), player(player)
{
    setMinimumSize(320, 200);
    setAccessibleName("Video preview");
}
MpvRenderWidget::~MpvRenderWidget()
{
    // The base destructor destroys the context after our derived slots are gone.
    if (context()) disconnect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &MpvRenderWidget::cleanup);
    cleanup();
}
void MpvRenderWidget::cleanup()
{
    if (!render) return;
    makeCurrent();
    mpv_render_context_set_update_callback(render, nullptr, nullptr);
    mpv_render_context_free(render); render = nullptr;
    doneCurrent();
}
void MpvRenderWidget::initializeGL()
{
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &MpvRenderWidget::cleanup, Qt::DirectConnection);
    mpv_opengl_init_params gl{[](void *, const char *name) -> void * {
        return reinterpret_cast<void *>(QOpenGLContext::currentContext()->getProcAddress(name));
    }, nullptr};
    mpv_render_param params[] = {{MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl}, {MPV_RENDER_PARAM_INVALID, nullptr}};
    int error = mpv_render_context_create(&render, player->handle(), params);
    if (error < 0) { emit failed("Could not initialize the OpenGL video preview."); return; }
    mpv_render_context_set_update_callback(render, [](void *ctx) {
        auto *widget = static_cast<MpvRenderWidget *>(ctx);
        QMetaObject::invokeMethod(widget, [widget] { widget->update(); }, Qt::QueuedConnection);
    }, this);
    emit ready();
}
void MpvRenderWidget::paintGL()
{
    if (!render) return;
    mpv_render_context_update(render);
    mpv_opengl_fbo fbo{int(defaultFramebufferObject()), qRound(width() * devicePixelRatioF()),
                       qRound(height() * devicePixelRatioF()), 0};
    int flip = 1;
    mpv_render_param params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip}, {MPV_RENDER_PARAM_INVALID, nullptr}};
    mpv_render_context_render(render, params);
}

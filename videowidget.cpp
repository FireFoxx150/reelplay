#include "videowidget.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions>

static void *getProcAddress(void *, const char *name)
{
    QOpenGLContext *gl = QOpenGLContext::currentContext();
    return gl ? reinterpret_cast<void *>(gl->getProcAddress(name)) : nullptr;
}

VideoWidget::VideoWidget(mpv_handle *mpv) : mpv(mpv) {}

VideoWidget::~VideoWidget()
{
    release();
}

void VideoWidget::release()
{
    if (!ctx)
        return;
    makeCurrent();
    mpv_render_context_set_update_callback(ctx, nullptr, nullptr);
    mpv_render_context_free(ctx);
    ctx = nullptr;
    doneCurrent();
}

void VideoWidget::initializeGL()
{
    // No MPV_RENDER_PARAM_ADVANCED_CONTROL: it forbids synchronous mpv calls on the
    // render thread (here the GUI thread, which uses them), and deadlocks if broken.
    mpv_opengl_init_params gl{getProcAddress, nullptr};
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
        {MPV_RENDER_PARAM_INVALID, nullptr}};
    if (mpv_render_context_create(&ctx, mpv, params) < 0)
        qFatal("mpv_render_context_create failed");
    // Called from mpv's thread: hop to the GUI thread before touching the widget.
    mpv_render_context_set_update_callback(ctx, [](void *self) {
        auto *w = static_cast<VideoWidget *>(self);
        QMetaObject::invokeMethod(w, [w] {
            if (w->ctx && (mpv_render_context_update(w->ctx) & MPV_RENDER_UPDATE_FRAME))
                w->update();
        }, Qt::QueuedConnection);
    }, this);
}

void VideoWidget::paintGL()
{
    if (!ctx) {
        context()->functions()->glClearColor(0, 0, 0, 1);
        context()->functions()->glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    qreal dpr = devicePixelRatioF();
    mpv_opengl_fbo fbo{int(defaultFramebufferObject()), int(width() * dpr), int(height() * dpr), 0};
    int flip = 1;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip},
        {MPV_RENDER_PARAM_INVALID, nullptr}};
    mpv_render_context_render(ctx, params);
}

#pragma once
#include <QOpenGLWidget>
#include <mpv/client.h>
#include <mpv/render_gl.h>

// Draws mpv's video with the libmpv render API, so it works on X11 and Wayland alike
// (no native window embedding).
class VideoWidget : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit VideoWidget(mpv_handle *mpv);
    ~VideoWidget() override;
    void release(); // free the render context; must run before mpv_terminate_destroy()

protected:
    void initializeGL() override;
    void paintGL() override;

private:
    mpv_handle *mpv;
    mpv_render_context *ctx = nullptr;
};

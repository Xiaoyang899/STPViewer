#pragma once

#include <QToolButton>
#include <QPointer>

class QAction;
class QVariantAnimation;

namespace Stp {

// A viewport overlay: the parent remains the existing OpenGL widget.
class ViewportFullscreenButton final : public QToolButton {
    Q_OBJECT
public:
    explicit ViewportFullscreenButton(QWidget* viewport);
    ~ViewportFullscreenButton() override;
    void setGrabAction(QAction* action);
    void setFullscreen(bool active);
    void setDarkBackground(bool dark);
    bool isFullscreen() const { return m_fullscreen; }
signals:
    void toggleRequested();
protected:
    void paintEvent(QPaintEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
    void changeEvent(QEvent*) override;
private:
    void anchorToViewport();
    void cancelAnimation();
    QVariantAnimation* m_animation = nullptr;
    QPointer<QToolButton> m_grabButton;
    qreal m_cornerOffset = 0;
    bool m_fullscreen = false;
    bool m_darkBackground = false;
};

} // namespace Stp

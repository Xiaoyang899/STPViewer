#include "fullscreen_button.h"

#include <QAction>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVariantAnimation>
#include <algorithm>

namespace Stp {

ViewportFullscreenButton::ViewportFullscreenButton(QWidget* viewport) : QToolButton(viewport)
{
    setObjectName(QStringLiteral("viewportFullscreenButton"));
    setFixedSize(36, 36);
    setAutoRaise(true);
    setFocusPolicy(Qt::TabFocus);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet(QStringLiteral("background:transparent;border:0;padding:0;"));
    setFullscreen(false);
    m_animation = new QVariantAnimation(this);
    m_animation->setDuration(180);
    m_animation->setEasingCurve(QEasingCurve::InOutQuad);
    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_cornerOffset = value.toReal();
        update();
    });
    connect(m_animation, &QVariantAnimation::finished, this, [this] {
        m_cornerOffset = 0;
        update();
        if (isEnabled() && isVisible()) emit toggleRequested();
    });
    connect(this, &QToolButton::clicked, this, [this] {
        if (m_animation->state() == QAbstractAnimation::Running) return;
        m_animation->setStartValue(0.0);
        m_animation->setEndValue(m_fullscreen ? -3.0 : 3.0);
        m_animation->start();
    });
    if (viewport) viewport->installEventFilter(this);
    anchorToViewport();
    show();
    QTimer::singleShot(0, this, &ViewportFullscreenButton::anchorToViewport);
}

ViewportFullscreenButton::~ViewportFullscreenButton()
{
    // Both controls are viewport children; deleting this control also removes
    // its companion if the viewport has not already destroyed it.
    delete m_grabButton.data();
}

void ViewportFullscreenButton::setGrabAction(QAction* action)
{
    if(!action){delete m_grabButton.data();return;}
    if(!m_grabButton){
        m_grabButton=new QToolButton(parentWidget());
        m_grabButton->setObjectName(QStringLiteral("viewportGrabButton"));
        m_grabButton->setFixedSize(36,36);
        m_grabButton->setIconSize(QSize(28,28));
        m_grabButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
        m_grabButton->setFocusPolicy(Qt::TabFocus);
        m_grabButton->setCursor(Qt::PointingHandCursor);
        m_grabButton->setAccessibleName(QStringLiteral("抓手"));
        m_grabButton->setStyleSheet(QStringLiteral(
            "QToolButton#viewportGrabButton{padding:3px;border:1px solid #c5d2e3;"
            "border-radius:4px;background:#f0f4f9;color:#233a56;}"
            "QToolButton#viewportGrabButton:checked{background:#2f6cb0;border-color:#2f6cb0;color:#fff;}"
            "QToolButton#viewportGrabButton:hover{border-color:#2f6cb0;}"
            "QToolButton#viewportGrabButton:focus{border-color:#2f6cb0;}"
            "QToolButton#viewportGrabButton:disabled{color:#929dab;border-color:#dae1ec;}"));
    }
    // This is the same action as the normal control. Checked/enabled/icon and
    // tooltip updates are supplied by QAction, with no second interaction mode.
    m_grabButton->setDefaultAction(action);
    m_grabButton->setVisible(m_fullscreen);
    anchorToViewport();
}

void ViewportFullscreenButton::setFullscreen(bool active)
{
    cancelAnimation();
    m_fullscreen = active;
    const auto text = active ? QStringLiteral("退出模型全屏") : QStringLiteral("全屏查看模型");
    setToolTip(text);
    setAccessibleName(text);
    if(m_grabButton)m_grabButton->setVisible(active);
    anchorToViewport();
    update();
}

void ViewportFullscreenButton::cancelAnimation()
{
    if (m_animation) m_animation->stop();
    m_cornerOffset = 0;
    update();
}

void ViewportFullscreenButton::setDarkBackground(bool dark)
{
    m_darkBackground = dark;
    update();
}

void ViewportFullscreenButton::anchorToViewport()
{
    if (const auto viewport = parentWidget()) {
        move(std::max(0, viewport->width() - width() - 12),
             std::max(0, viewport->height() - height() - 12));
        if(m_grabButton){
            m_grabButton->move(std::max(0,x()-m_grabButton->width()-8),y());
            m_grabButton->raise();
        }
        raise();
    }
}

bool ViewportFullscreenButton::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget()) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show)
            anchorToViewport();
        else if (event->type() == QEvent::Hide)
            cancelAnimation();
    }
    return QToolButton::eventFilter(watched, event);
}

void ViewportFullscreenButton::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::EnabledChange && !isEnabled()) cancelAnimation();
    QToolButton::changeEvent(event);
}

void ViewportFullscreenButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor color = !isEnabled() ? QColor("#96a1ad")
        : underMouse() || hasFocus() ? QColor(m_darkBackground ? "#a7cbf2" : "#2f6cb0")
        : QColor(m_darkBackground ? "#d7e1ed" : "#536579");
    painter.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    // Four independent L corners form a 24-logical-pixel icon at rest.
    for (int sx : {-1, 1}) for (int sy : {-1, 1}) {
        const QPointF corner(18 + sx * (12 + m_cornerOffset),
                             18 + sy * (12 + m_cornerOffset));
        QPainterPath path;
        path.moveTo(corner + QPointF(-sx * 6, 0));
        path.lineTo(corner);
        path.lineTo(corner + QPointF(0, -sy * 6));
        painter.drawPath(path);
    }
    if (hasFocus()) {
        painter.setPen(QPen(color, 1, Qt::DotLine));
        painter.drawRect(QRectF(1.5, 1.5, 33, 33));
    }
}

} // namespace Stp

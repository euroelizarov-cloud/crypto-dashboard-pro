#include "TransitionOverlay.h"
#include <QPainter>
#include <QLinearGradient>
#include <QEasingCurve>
#include <QEvent>
#include <QDebug>

TransitionOverlay::TransitionOverlay(QWidget* target, const QPixmap& from, const QPixmap& to, Type type, int msec)
    : QWidget(target), m_target(target), m_from(from), m_to(to), m_type(type), m_anim(this, "progress") {
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    if (m_target) { setGeometry(m_target->rect()); m_target->installEventFilter(this); }
    m_anim.setDuration(msec);
    m_anim.setStartValue(0.0);
    m_anim.setEndValue(1.0);
    m_anim.setEasingCurve(QEasingCurve::InOutCubic);
}

void TransitionOverlay::start() {
    if (!m_target) { deleteLater(); return; }
    raise();
    show();
    // Do NOT pass DeleteWhenStopped for a member animation!
    // The animation is a member (not heap-allocated separately), so deleting it
    // leads to use-after-free or double-delete. Start normally and delete the overlay on finish.
    m_anim.start();
    connect(&m_anim, &QAbstractAnimation::finished, this, [this]{
        if (m_target) m_target->removeEventFilter(this);
        deleteLater();
    });
}

bool TransitionOverlay::eventFilter(QObject*, QEvent* e) {
    switch (e->type()) {
        case QEvent::Resize:
        case QEvent::Move:
            if (m_target) setGeometry(m_target->rect());
            return false;
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseMove:
        case QEvent::Wheel:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
            return true;
        default: return false;
    }
}

void TransitionOverlay::paintEvent(QPaintEvent*) {
    if (m_type == None) return; // nothing to draw
    if (m_from.isNull() || m_to.isNull()) return;
    static bool logEnabled = qEnvironmentVariableIsSet("DASH_TRANSITION_LOG");
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    const QRect r = rect();
    if (r.isEmpty()) { if (logEnabled) qDebug() << "[TRANSITION] empty rect skip"; return; }
    if (logEnabled && (m_progress==0.0 || m_progress==1.0)) qDebug() << "[TRANSITION] paint boundary progress=" << m_progress << "type=" << m_type;

    if (m_type == Crossfade) {
        p.setOpacity(std::clamp(1.0 - m_progress, 0.0, 1.0));
        p.drawPixmap(r, m_from);
        p.setOpacity(std::clamp(m_progress, 0.0, 1.0));
        p.drawPixmap(r, m_to);
        return;
    }

    if (m_type == ZoomBlur) {
        // Brief zoom-out + subtle blur of old, fade-in new
        // Approximate blur by drawing multiple slightly scaled, low-opacity layers
        double zoom = 1.0 - 0.06 * m_progress; // up to 6% zoom-out
        int layers = 6;
        for (int i=0; i<layers; ++i) {
            double a = (1.0 - m_progress) * (0.25 / layers);
            p.setOpacity(a);
            double s = zoom * (1.0 - 0.015*i);
            int w = int(r.width()*s), h = int(r.height()*s);
            QRect dst(r.center().x()-w/2, r.center().y()-h/2, w, h);
            p.drawPixmap(dst, m_from);
        }
        p.setOpacity(m_progress);
        p.drawPixmap(r, m_to);
        return;
    }

    if (m_type == Slide) {
        int w = r.width();
        int dx = int(m_progress * w);
        p.drawPixmap(QRect(-dx, 0, w, r.height()), m_from);
        p.drawPixmap(QRect(w - dx, 0, w, r.height()), m_to);
        return;
    }

    // Flip
    double phase = std::clamp(m_progress, 0.0, 1.0);
    bool secondHalf = phase >= 0.5;
    double t = secondHalf ? (phase - 0.5) * 2.0 : phase * 2.0; // 0..1
    double sx = qMax(0.02, 1.0 - t); // avoid near-zero scales

    if (!secondHalf) {
        p.save();
        p.translate(r.center());
        p.scale(sx, 1.0);
        p.translate(-r.center());
        p.drawPixmap(r, m_from);
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        g.setColorAt(0.0, QColor(0,0,0, 60));
        g.setColorAt(0.5, QColor(0,0,0, 0));
        g.setColorAt(1.0, QColor(0,0,0, 60));
        p.fillRect(r, g);
        p.restore();
    } else {
    double sx2 = qMax(0.02, t);
        p.save();
        p.translate(r.center());
        p.scale(sx2, 1.0);
        p.translate(-r.center());
        p.drawPixmap(r, m_to);
        p.restore();
        QLinearGradient g(r.topLeft(), r.topRight());
        g.setColorAt(0.0, QColor(0,0,0, 40));
        g.setColorAt(0.5, QColor(0,0,0, 0));
        g.setColorAt(1.0, QColor(0,0,0, 40));
        p.fillRect(r, g);
    }
}

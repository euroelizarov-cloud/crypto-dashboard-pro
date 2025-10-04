#pragma once
#include <QWidget>
#include <QPixmap>
#include <QPropertyAnimation>

class TransitionOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress WRITE setProgress)
public:
    enum Type { None, Flip, Slide, Crossfade, ZoomBlur };
    explicit TransitionOverlay(QWidget* target, const QPixmap& from, const QPixmap& to, Type type, int msec = 380);
    double progress() const { return m_progress; }
    void setProgress(double p) { m_progress = p; update(); }
    void start();
protected:
    void paintEvent(QPaintEvent*) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;
private:
    QWidget* m_target;
    QPixmap m_from, m_to;
    Type m_type;
    double m_progress = 0.0;
    QPropertyAnimation m_anim;
};

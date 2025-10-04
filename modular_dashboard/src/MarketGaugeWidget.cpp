#include "MarketGaugeWidget.h"
#include <QPainter>
#include <QFontMetrics>
#include <QtMath>

MarketGaugeWidget::MarketGaugeWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 220);
}

void MarketGaugeWidget::setSnapshot(const MarketSnapshot& s) {
    snap = s; update();
}

void MarketGaugeWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = rect().adjusted(12, 12, -12, -12);
    // Draw background
    p.fillRect(rect(), QColor(30,32,38));
    // Dial area (top 2/3)
    QRectF dial = QRectF(r.left(), r.top(), r.width(), r.height()*0.66);
    QPointF c = dial.center(); double radius = qMin(dial.width(), dial.height())/2.0 - 8.0;
    // Arc from -140° to +140°
    double startDeg = 220; double spanDeg = 280;
    // Color gradient: red->yellow->green across span
    QConicalGradient grad(c, startDeg+spanDeg);
    grad.setColorAt(0.0, QColor(210,60,60));
    grad.setColorAt(0.5, QColor(220,200,70));
    grad.setColorAt(1.0, QColor(70,200,90));
    p.setPen(QPen(QBrush(grad), 10.0, Qt::SolidLine, Qt::FlatCap));
    p.drawArc(QRectF(c.x()-radius, c.y()-radius, radius*2, radius*2), int(startDeg*16), int(spanDeg*16));
    // Needle position map: snap.index [-1,1] -> [start..start+span]
    double idx = std::clamp(snap.index, -1.0, 1.0);
    double ang = (startDeg + (idx*0.5 + 0.5)*spanDeg) * M_PI / 180.0;
    QPointF tip(c.x() + std::cos(ang)* (radius-8), c.y() - std::sin(ang)* (radius-8));
    // Needle color from index
    QColor ncol = (idx>0.2? QColor(90,220,120) : idx<-0.2? QColor(230,80,80) : QColor(220,200,70));
    p.setPen(QPen(Qt::white, 2)); p.setBrush(ncol);
    QPolygonF needle; needle << c << QPointF(c.x()-6, c.y()+8) << tip << QPointF(c.x()+6, c.y()+8);
    p.drawPolygon(needle);
    p.setBrush(Qt::white); p.drawEllipse(c, 4, 4);
    // Strength and confidence bars (bottom left/right)
    QRectF bars = QRectF(r.left(), r.top()+r.height()*0.70, r.width(), r.height()*0.10);
    double bw = (bars.width()-20)/2.0; double bh = bars.height();
    auto drawBar = [&](QRectF br, double v, const QString& label, const QColor& col){
        p.setPen(Qt::NoPen); p.setBrush(QColor(55,60,68)); p.drawRoundedRect(br, 6, 6);
        QRectF fill = br.adjusted(2,2,-2,-2); fill.setWidth(fill.width()*std::clamp(v,0.0,1.0));
        p.setBrush(col); p.drawRoundedRect(fill, 5, 5);
        p.setPen(Qt::white); p.setBrush(Qt::NoBrush);
        p.drawText(br.adjusted(6,0,-6,0), Qt::AlignVCenter|Qt::AlignLeft, label);
        p.drawText(br.adjusted(6,0,-6,0), Qt::AlignVCenter|Qt::AlignRight, QString::number(int(std::round(v*100)))+"%" );
    };
    drawBar(QRectF(bars.left(), bars.top(), bw, bh), snap.strength, tr("Сила"), QColor(90,180,240));
    drawBar(QRectF(bars.left()+bw+20, bars.top(), bw, bh), snap.confidence, tr("Уверенность"), QColor(200,140,255));
    // Comment text
    QString text = snap.label.isEmpty()? tr("анализ...") : snap.label;
    p.setPen(Qt::white);
    p.drawText(QRectF(r.left(), r.bottom()-r.height()*0.15, r.width(), r.height()*0.14), Qt::AlignCenter, text);
}

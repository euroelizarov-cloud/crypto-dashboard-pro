#pragma once
#include <QString>
#include <QMap>
#include <QColor>
#include <QStringList>
#include <QObject>
#include "DynamicSpeedometerCharts.h"

struct Theme {
    QString name;
    QString appStyleSheet; // optional global stylesheet
    QColor background;
    QColor foreground;
    QColor accent;
    QColor grid;
    SpeedometerColors speedometer; // colors for speedometer widgets
};

class ThemeManager : public QObject {
    Q_OBJECT
public:
    enum class ColorTheme {
        Dark = 0,
        Light = 1,
        Blue = 2,
        Green = 3,
        Purple = 4,
        Orange = 5,
        Red = 6,
        Cyber = 7,
    Ocean = 8,
    Forest = 9,
    TokyoNight = 10,
    Cyberpunk = 11,
    Retro = 12
    };

    ThemeManager(QObject* parent = nullptr);
    const Theme& current() const { return themeByName(currentName); }
    const Theme& themeByName(const QString& name) const;
    QStringList themeNames() const;
    void setCurrent(const QString& name);
    void setTheme(ColorTheme theme);
    SpeedometerColors getSpeedometerColors(ColorTheme theme) const;
private:
    void add(const Theme& t);
    QMap<QString,Theme> themes;
    QString currentName;
};

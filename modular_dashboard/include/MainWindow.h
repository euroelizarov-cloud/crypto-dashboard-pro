#pragma once
#include <QMainWindow>
#include <QMap>
#include <QStringList>
#include "DataWorker.h"
#include "DynamicSpeedometerCharts.h"
#include "ThemeManager.h"

class QThread;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow();
private slots:
    void switchMode(StreamMode m);
    void openPerformanceDialog();
    void openThemeDialog();
    void handleData(const QString& currency, double price, double timestamp);
    void onRequestRename(const QString& currentTicker);
private:
    struct PerfSettings { int animMs; int renderMs; int cacheMs; int volWindow; int maxPts; int rawCache; };
    PerfSettings readPerfSettings();
    void writePerfSettings(const PerfSettings& s);
    void loadSettingsAndApply();
    QStringList readCurrenciesSettings();
    void saveCurrenciesSettings(const QStringList& list);
    void applyTheme(const QString& name);
    void applyThresholdsToAll(bool enabled, double warnValue, double dangerValue);
    void onThemeChanged(const ThemeManager::ColorTheme& theme);
private:
    QMap<QString, DynamicSpeedometerCharts*> widgets;
    DataWorker* dataWorker=nullptr; QThread* workerThread=nullptr; double btcPrice=0.0; StreamMode streamMode=StreamMode::Trade; QStringList currentCurrencies; ThemeManager* themeManager;
};

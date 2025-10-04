#pragma once
#include <QMainWindow>
#include <QMap>
#include <QSet>
#include <QHash>
#include <QGridLayout>
#include <QStringList>
#include "DataWorker.h"
#include "DynamicSpeedometerCharts.h"
#include "ThemeManager.h"
#include "MarketAnalyzer.h"
class MarketOverviewWindow;
class MultiCompareWindow;

class QThread;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow();
protected:
    void closeEvent(QCloseEvent* e) override;
private slots:
    void switchMode(StreamMode m);
    void openPerformanceDialog();
    void openThemeDialog();
    void handleData(const QString& currency, double price, double timestamp);
    void onRequestRename(const QString& currentTicker);
    void showAbout();
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
    void reflowGrid();
    // Pseudo tickers support
    bool isPseudo(const QString& name) const { return name.startsWith("@"); }
    enum class PseudoKind { None, Avg, AltAvg, Median, Spread, Diff, Top10Avg, VolAvg, BtcDom, ZScore };
    struct DiffSpec { QString symbol; BybitMarket market = BybitMarket::Linear; bool valid=false; };
    PseudoKind classifyPseudo(const QString& name, DiffSpec* outDiff = nullptr) const;
    void connectRealWidgetSignals(const QString& symbol, DynamicSpeedometerCharts* w);
    void recomputePseudoTickers();
    void refreshCompareSubscriptions();
    QStringList realSymbolsFrom(const QStringList& list) const;
private:
    QMap<QString, DynamicSpeedometerCharts*> widgets;
    QGridLayout* gridLayout = nullptr;
    int gridCols = 4;
    int gridRows = 3; // informational, placement uses gridCols
    DataWorker* dataWorker=nullptr; QThread* workerThread=nullptr; double btcPrice=0.0; StreamMode streamMode=StreamMode::Trade; QStringList currentCurrencies; ThemeManager* themeManager;
    // Aggregation maps
    QHash<QString,double> normalizedBySymbol; // 0..100 per real symbol
    QHash<QString,double> volBySymbol; // volatility % per real symbol
    // Compare workers (Binance + Bybit Linear/Spot)
    DataWorker* cmpBinance=nullptr; QThread* cmpBinanceThread=nullptr;
    DataWorker* cmpBybitLinear=nullptr; QThread* cmpBybitLinearThread=nullptr;
    DataWorker* cmpBybitSpot=nullptr; QThread* cmpBybitSpotThread=nullptr;
    QSet<QString> cmpSymsBinance, cmpSymsBybitLinear, cmpSymsBybitSpot;
    QHash<QString,double> binancePrice, bybitLinearPrice, bybitSpotPrice;
    // Market overview analyzer and window
    MarketAnalyzer* marketAnalyzer = nullptr;
    MarketOverviewWindow* marketWindow = nullptr;
    MultiCompareWindow* compareWindow = nullptr;
};

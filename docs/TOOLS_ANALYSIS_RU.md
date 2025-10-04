# Инструменты анализа: глубокий разбор реализации

Документ описывает реализацию двух модулей:
- Общий обзор рынка (Market Overview): агрегированный индикатор состояния рынка.
- Сравнение графиков (норм.) (Multi-Compare): совместный график нормализованных рядов.

Цель — показать точные структуры данных, алгоритмы, сигналы/слоты, рендеринг, жизненный цикл, настройки, обработку ошибок и производительность.

---

## 1) Общий обзор рынка

Компоненты:
- MarketAnalyzer — ядро анализа (агрегатор).
- MarketGaugeWidget — визуализация результата (шкала + стрелка + бары).
- MarketOverviewWindow — окно с настройками и виджетом.

Связи (сигналы/слоты):
- MainWindow → MarketAnalyzer: onQuote(sym, price, ts, vol) → analyzer.updateSymbol(sym, ts, price, vol)
- MarketAnalyzer → MarketOverviewWindow: snapshotReady(const Snapshot&) → gauge->setSnapshot(...)

### 1.1 MarketAnalyzer

Назначение: из историй всех включённых активов вычислить агрегированный индекс направления, силу и уверенность с учётом выбранных весов и интервала; поддерживать режимы “с BTC” и “без BTC”.

Состояние (основное):
```
struct SymbolHist {
  // компактное окно истории
  QVector<double> t;  // ts в секундах или мс (double для лин. регрессии)
  QVector<double> x;  // нормализованное значение или цена (используется унифицированно)
  double volEwma = 0; // оценка волатильности (EWMA абсолютных доходностей)
};

class MarketAnalyzer : public QObject {
  Q_OBJECT
public:
  struct Config {
    int windowSec = 900; // 15m
    enum Weight { Equal, InverseVolatility } weight = Equal;
    bool includeBTC = true;
    QSet<QString> excluded; // исключённые активы
  };
  struct Snapshot {
    double consensusIndex;   // [-1..1]
    double strength;         // [0..1]
    double confidence;       // [0..1]
    QString label;           // текстовое описание
    int symbolsUsed;         // сколько активов вошло в расчёт
  };

  void setConfig(const Config&);
  void updateSymbol(const QString& sym, double ts, double priceOrNorm, double volDelta);
signals:
  void snapshotReady(const Snapshot& s);

private:
  QHash<QString, SymbolHist> m;   // истории по символам (обрезаются по windowSec)
  Config cfg;
  QTimer tick; // периодическая переоценка (например, раз в 500–1000 мс)
};
```

Обрезка истории:
- При каждом updateSymbol для символа сдвигаем левую границу: удаляем точки, чей t < tMax - windowSec.
- Это гарантирует O(1) память на символ и стабильное время расчёта.

Оценка волатильности:
- volEwma = alpha*|dx| + (1-alpha)*volEwma, где dx = x[t] - x[t-1]; alpha ~ 0.1.
- Используется для весовой схемы InverseVolatility (w = 1 / (eps + volEwma)).

Расчёт тренда (регрессия):
- Для каждого символа считаем slope линейной регрессии x ~ a + b t на текущем окне.
- Формулы (обычный OLS по накопленным суммам):
  - b = (n*Σ(tx) - Σt Σx) / (n*Σ(t^2) - (Σt)^2)
- Для стабильности t центрируем: t' = t - t_mean (уменьшает числ. ошибки).
- Если точек < 3 — символ пропускается.

Агрегирование:
- Для каждой b (slope) вычисляем вес w:
  - Equal: w = 1
  - InverseVolatility: w = 1 / (eps + volEwma)
- Консенсус: B = Σ(w*b) / Σ(w)
- Нормируем в индекс [-1..1] через сигмоиду или линейную шкалу с клиппингом:
  - idx = clamp(B / K, -1, +1), где K — масштаб (напр., 0.5 ед/мин).
- Сила: mean(|b|) нормированная тем же K и клипнутая [0..1].
- Уверенность:
  - cDir = доля активов, у которых sign(b) == sign(B) (консенсус направления).
  - cDisp = 1 - normVar(b) (низкая дисперсия — высокая уверенность).
  - confidence = 0.5*cDir + 0.5*cDisp.
- Режим includeBTC=false: просто исключаем “BTC...” из множества при расчёте.

Метки:
```
if confidence < 0.35: "рынок нестабилен"
else if idx > +0.66: "сильный рост"
else if idx > +0.2:  "рост"
else if idx < -0.66: "сильный спад"
else if idx < -0.2:  "спад"
else:                "боковое движение"
```

Псевдокод переоценки (tick):
```
onTick():
  acc = []
  for sym in m.keys():
    if !cfg.includeBTC && isBTC(sym): continue
    if sym in cfg.excluded: continue
    slope = regressionSlope(m[sym].t, m[sym].x)
    if !isFinite(slope): continue
    w = (cfg.weight==Equal) ? 1.0 : 1.0 / (eps + m[sym].volEwma)
    acc.push({slope, w})

  if acc.empty(): emit snapshotReady({0,0,0,"недостаточно данных",0}); return

  B = Σ(w*slope)/Σ(w)
  strength = clamp( mean(|slope|)/K, 0, 1 )
  consensusDir = fractionSameSign(acc.slopes, B)
  dispersion = normalizedVariance(acc.slopes)
  confidence = 0.5*consensusDir + 0.5*(1 - dispersion)
  idx = clamp(B / K, -1, +1)
  label = labelFor(idx, confidence)
  emit snapshotReady({idx, strength, confidence, label, acc.size()})
```

Производительность:
- Обрезка окна и регрессия по окну дают стабильное O(n) от числа точек в окне на символ.
- Таймер tick ~1 Гц достаточен; можно чаще.

Ошибки/устойчивость:
- Проверка деления на ноль (Σ(w) > 0).
- Если мало точек — пропуск символа.
- Защита NaN/Inf.

### 1.2 MarketGaugeWidget

Рендеринг (paintEvent):
- Полудуга с градиентом: красный → жёлтый → зелёный.
- Угол стрелки θ = map(idx∈[-1,1]) на [θmin..θmax], напр. 45°..315°.
- Цвет стрелки: по знаку idx (красный/жёлтый/зелёный), альфа зависит от strength.
- Два горизонтальных бара под шкалой:
  - “Сила”: заполнение по strength [0..1].
  - “Уверенность”: заполнение по confidence [0..1].
- Подпись-вердикт label под барами.
- Баланс save()/restore() — без утечек состояния.

Настройки:
- Из окна доступны выбор интервала, веса, включение BTC, исключения активов.
- Сохраняются в QSettings: Tools/Overview/*

### 1.3 MarketOverviewWindow

UI:
- Панель настроек (QComboBox для интервала и веса, QCheckBox BTC, QListView с мультивыбором исключений).
- MarketGaugeWidget в центре.

Поведение:
- При изменении настроек → setConfig в MarketAnalyzer.
- На snapshotReady → обновление gauge.
- Закрытие окна останавливает таймеры анализатора (если они локальные) и отключает слоты.

---

## 2) Сравнение графиков (норм.)

Компонент: MultiCompareWindow — окно с QtCharts.

Назначение: показать на одном графике несколько активов, приведённых к общей временной сетке и нормализованных “относительно самих себя”.

Источники данных:
- Из каждого DynamicSpeedometerCharts берётся historySnapshot():
  - QVector<QPair<double ts, double x>> (ts — монотонная шкала времени; x — нормализованная величина или цена).

Этапы:
1) Сбор сырых рядов для выбранных активов.
2) Вычисление общей сетки времени:
   - t0 = max(min_ts по активам), t1 = min(max_ts по активам) — пересечение интервалов.
   - stepSec задаёт частоту (напр., 1–10 сек).
   - grid = {t0, t0+step, ..., t1}.
3) Ресэмплирование carry-forward:
   - Идём по исходному ряду и по grid; для каждой точки grid берём последнее известное x.
   - Если нет значения — помечаем как “нет данных” (можно пропускать точку).
4) Нормализация по выбору:
   - FromStart%: y = (x - x0)/x0 * 100, где x0 — первое значение на сетке.
   - MinMax 0..1: y = (x - xmin)/(xmax - xmin).
   - Z-Score: y = (x - mean)/std.
5) Сглаживание (опц.): EMA(y, α) точечно по сетке.
6) Отрисовка серий QLineSeries, легенда, оси.

Структуры:
```
struct SeriesRaw { QVector<double> t; QVector<double> x; };
struct SeriesResampled { QVector<double> tg; QVector<double> yg; QString name; QColor color; };

class MultiCompareWindow : public QMainWindow {
  Q_OBJECT
public:
  void setSelection(const QStringList& symbols);
  void setParams(int stepSec, NormMode mode, double emaAlpha, bool autoRefresh);
  void refresh(); // вызывает buildGrid + resample + normalize + draw

private:
  QVector<SeriesRaw> fetchAll();
  QVector<SeriesResampled> build(const QVector<SeriesRaw>&);
  void draw(const QVector<SeriesResampled>&);
};
```

Псевдокод ресэмплинга:
```
resample(raw, grid):
  j=0; last=NaN
  for t in grid:
    while j+1 < raw.t.size() && raw.t[j+1] <= t: j++
    if raw.t[j] <= t: last = raw.x[j]
    if isFinite(last): out.push(last) else out.push(NaN/skip)
```

Нормализация:
- FromStart%: защититься от x0≈0: если |x0|<eps, брать ближайшую ненулевую точку или пропустить ряд.
- MinMax: если xmax≈xmin — использовать fallback (горизонтальная линия 0.5 или пропуск).
- Z-Score: std≈0 → fallback std=eps.

Производительность:
- Для k активов, m точек сетки — O(k*m).
- stepSec увеличивает/уменьшает m (баланс детализации/скорости).

Настройки (QSettings):
```
Tools/MultiCompare/StepSec
Tools/MultiCompare/NormMode
Tools/MultiCompare/SmoothingEMA
Tools/MultiCompare/AutoRefreshSec
Tools/MultiCompare/Selections
```

Ошибки/устойчивость:
- Если пересечение интервалов пусто (t0>t1) — показываем сообщение “недостаточно общей истории”.
- На NaN/Inf — пропускаем точки/ряд.

---

## 3) Интеграция с MainWindow и потоками

- Оба инструмента создаются лениво по пунктам меню Tools.
- Подписки:
  - Overview: получает updateSymbol через MainWindow (проброс котировок) или запрашивает снапшоты у виджетов для реконструкции.
  - Compare: синхронно запрашивает historySnapshot() при нажатии Refresh/AutoRefresh.
- Таймеры:
  - Analyzer.tick для регулярной переоценки.
  - AutoRefresh в Compare (если включён).

Все вычисления держатся легковесными (регрессия за окно, ресэмплинг по сетке). При необходимости легко вынести анализ в QThread.

---

## 4) Визуальные детали и UX

- Gauge (Overview):
  - Полудуга 180°–270°; цвета: #E74C3C → #F1C40F → #2ECC71.
  - Стрелка с мягким glow при высокой strength.
  - Комментарий: короткий и чёткий; при низкой confidence — пометка “нестабильно”.

- Compare:
  - Автопалитра серий, легенда справа.
  - Подписи осей: время (X), нормализованная величина (Y).
  - Подсказка по точкам (hover) с временем и значением.

---

## 5) Тестирование, ошибки, логирование

Тесты (ручные):
- Overview: переключение интервалов и весов; режим без BTC; исключение активов; стабильность меток.
- Compare: разные нормализации и шаги; пустые пересечения; EMA-сглаживание.

Типовые ошибки и защита:
- Деление на 0: eps в нормализациях и весах.
- Пустые ряды: сообщения пользователю, пропуски.
- Несбалансированный QPainter state: строгие пары save/restore.

Логи:
- Создание/закрытие окон; применённые параметры; кол-во активов в расчёте; длительность пересчёта (ms) при уровне debug.

---

## 6) Масштабируемость и будущие улучшения

- Веса по капитализации/объёму (внешние источники).
- Перевод Analyzer в QThread при k > 100 активов.
- Downsampling больших историй в Compare (Ramer–Douglas–Peucker или Largest-Triangle-Three-Buckets).
- Сохранение снапшотов в локальную БД (опция) для оффлайн-анализа.

---

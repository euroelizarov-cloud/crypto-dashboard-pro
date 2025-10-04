# Crypto Dashboard Pro — Полное техническое описание проекта (v1.0.0)

Этот документ — максимально подробная спецификация проекта Crypto Dashboard Pro на русском языке: от технического задания и высокоуровневой архитектуры до низкоуровневых алгоритмов, структур данных, протоколов, взаимодействия компонентов, потоков, сигналов/слотов, настроек, логирования, тестирования, деплоя и эксплуатационных практик. Цель: чтобы любой разработчик или LLM могли полностью понять систему, восстановить её из документа и уверенно развивать дальше.

Версия документа: 1.0.0
Дата: 2025-10-04

---

## Содержание
- 1. Техническое задание (ТЗ)
- 2. Обзор архитектуры
- 3. Компоненты и взаимодействие
- 4. Потоки, таймеры, жизненный цикл
- 5. Данные и структуры хранения
- 6. Протоколы данных (WS) и унификация
- 7. Алгоритмы (автошкала, индикаторы, аномалии, агрегатор, ресэмплинг/нормализация)
- 8. UI/UX, темы, стили, рамки
- 9. Настройки (QSettings) и персистентность
- 10. Ошибки, исключения, устойчивость
- 11. Логирование, наблюдаемость
- 12. Производительность и масштабируемость
- 13. Безопасность и модель угроз
- 14. Тестирование
- 15. Сборка, упаковка, деплой
- 16. CI/CD (план)
- 17. Метрики и мониторинг (план)
- 18. Документация и стиль кода
- 19. Принципы и паттерны
- 20. Структура репозитория
- 21. Псевдокод ключевых участков
- 22. Сценарии использования
- 23. Траблшутинг
- 24. Лицензирование
- 25. Changelog 1.0.0

---

## 1. Техническое задание (ТЗ)

### 1.1 Цель
Реализовать настольное приложение реального времени для наблюдения за рынком криптовалют. Предоставить:
- Виджеты-спидометры для отдельных активов с гибкими визуализациями и индикаторами
- Глобальные настройки для массового управления параметрами всех виджетов
- Аналитические окна: «Общий обзор рынка» (агрегированный индикатор) и «Сравнение графиков (норм.)»
- Устойчивую работу при высоком потоке данных, корректный выход, отсутствия протечек ресурсов

### 1.2 Ключевые функции
- Подключение к Binance/Bybit по WebSocket (TRADE/TICKER)
- Нормализация TICKER с fallback на 24h для корректной второй стрелки объёма
- Автомасштабирование по окну истории с сохранением экстремумов, режимы Legacy/KiloCoder-like
- Объём: Sidebar, Fuel («топливный бак»), Second Needle (вторая стрелка)
- Индикаторы: RSI/MACD/BB; аномалии (композитный)
- Глобальное меню «Widgets» для массового применения настроек ко всем виджетам
- «Общий обзор рынка»: интервал, веса, включение BTC, исключения
- «Сравнение графиков (норм.)»: выравнивание по времени, нормализация (FromStart/MinMax/Z-Score), EMA-сглаживание, автообновление
- Персистентность в QSettings

### 1.3 Ограничения и предположения
- Секреты/ключи не используются; только публичные потоки
- Локальная БД отсутствует; история хранится в оперативной памяти с ограничением окна
- macOS (Qt6) как основная платформа; переносимость другими ОС возможна

---

## 2. Обзор архитектуры

Архитектура модульная, событийно-ориентированная (Qt signals/slots).

Высокоуровневые блоки:
- MainWindow — оболочка, меню, размещение виджетов, маршрутизация действий
- DataWorker — WebSocket клиент и парсер, единая шина котировок (сигналы)
- DynamicSpeedometerCharts — виджет визуализации символа (спидометр + оверлеи + история)
- MarketAnalyzer — агрегатор рыночных данных по множеству активов
- MarketGaugeWidget — визуализация агрегированного индекса
- MarketOverviewWindow — UI конфигурации агрегатора + gauge
- MultiCompareWindow — окно сравнения нормализованных рядов по нескольким активам
- ThemeManager / Frame Renderer — темы, палитры, рамки, стили рисок («Modern Ticks»)

Диаграмма (ASCII):
```
[WS Binance/Bybit]
     |  JSON
     v
[DataWorker] --signal:quoteReceived--> [MainWindow] --update--> [DynamicSpeedometerCharts]*N
                                         |                                ^
                                         |                                |
                                         +--update--> [MarketAnalyzer] --signal:snapshotReady--> [MarketOverviewWindow]
                                         |
                                         +--open--> [MultiCompareWindow] <-- historySnapshot() from widgets
```

---

## 3. Компоненты и взаимодействие

### 3.1 MainWindow
- Инициализирует DataWorker, создаёт виджеты на символы, настраивает меню
- Обрабатывает сигналы котировок, распределяет их по виджетам и в MarketAnalyzer
- Открывает окна «Общий обзор рынка» и «Сравнение графиков (норм.)», синхронизируя источники данных

### 3.2 DataWorker
- Управляет жизненным циклом WebSocket: open/close/reconnect, подписки по списку символов
- Разбирает приходящие JSON-фреймы и эмитит унифицированные сигналы quoteReceived(symbol, price, tsMs, volume)
- Выравнивает различия провайдеров

### 3.3 DynamicSpeedometerCharts
- Отображает текущее значение, историю, объём, индикаторы и аномалии
- Реализует автомасштабирование по окну истории с сохранением экстремумов
- Экспортирует historySnapshot() для внешних аналитик и сравнения
- Предоставляет API сеттеров для массового управления из «Widgets»

### 3.4 MarketAnalyzer + MarketGaugeWidget + MarketOverviewWindow
- Analyzer накапливает по символам временные ряды и считает агрегированные метрики
- GaugeWidget рисует итог: стрелка + цветовая дуга + бары силы/уверенности
- OverviewWindow содержит настройки (интервал, веса, исключения) и отображение gauge

### 3.5 MultiCompareWindow
- Получает снимки историй из виджетов, ресэмплирует на общую сетку времени
- Нормализует ряды по выбранному режиму, опционально сглаживает EMA
- Отрисовывает объединённый график по множеству активов

---

## 4. Потоки, таймеры, жизненный цикл

- UI-поток: все QWidget/Qt Charts, paintEvent, события UI
- DataWorker: асинхронный в рамках event loop; при необходимости выносится в worker thread
- QTimer: автообновление MultiCompareWindow, интервальная переоценка MarketAnalyzer

Жизненный цикл:
1) Запуск приложения → создание MainWindow → настройка DataWorker/виджетов/меню
2) Подключение WS → приём данных → сигналы в MainWindow → маршрутизация
3) Работа окон инструментов (Overview/Compare) по требованию
4) Корректное завершение: закрытие WS, освобождение ресурсов, отсутствие зависаний

---

## 5. Данные и структуры хранения

### 5.1 История символов
- Внутренне: std::deque или QVector; элемент: (t_ms, value)
- Обрезка по окну (seconds) регулирует объём памяти и время расчётов
- Импорт/экспорт: historySnapshot() → QVector<QPair<double,double>>

### 5.2 Состояние агрегатора рынка
- На символ: массивы t, x (timestamps и цены), оценка волатильности
- Итоговый снимок: consensusSlope, strength, confidence, label

### 5.3 Данные окна сравнения
- Ресэмплирование на сетку: [t0, t0+step, ..., tn]
- Заполнение по принципу «последнее известное значение»
- Нормализация: FromStart%, MinMax [0..1], Z-Score

---

## 6. Протоколы данных (WS) и унификация

### 6.1 Binance
- Потоки: @trade (реал-тайм), возможно тикерные потоки
- Парсинг: event time (E), цена (p), количество (q), символ из имени потока

### 6.2 Bybit
- Аналогичный разбор; полям назначается стандартная семантика

Унификация:
```
struct Quote {
  QString symbol;
  double  price;
  double  volume;   // оценка/вычисление по событию
  double  tsMs;     // абсолютная метка времени
};
```
DataWorker приводит сырой JSON к Quote и эмитит quoteReceived.

---

## 7. Алгоритмы

### 7.1 Автомасштабирование «историческое окно + экстремумы»
- Собираем min/max в окне N секунд; добавляем padding
- При равенстве min≈max используем небольшой эпсилон для видимости
- Режимы: Legacy (консервативный), KiloCoder-like (динамичнее реагирует на пики)

Формулы:
```
low  = min * (1 - padding)
high = max * (1 + padding)
```

### 7.2 Индикаторы
- RSI: классический RSI(n)
- MACD: EMA12, EMA26, сигнальная EMA9
- Bollinger Bands: SMA ± k·σ

### 7.3 Аномалии
- Композитный показатель на основе отклонений цены и/или всплесков объёма
- Подсветка в области графика спидометра/истории

### 7.4 Агрегатор (MarketAnalyzer)
- Для каждого символа: линейная регрессия на последнем окне (slope)
- Веса: равные или обратно пропорциональны волатильности
- Консенсус: средневзвешенный slope
- Сила/уверенность: производные от консенсуса и дисперсии

### 7.5 Сравнение графиков (MultiCompare)
- Ресэмплирование: создание сетки времени, перенос последнего известного значения вперёд
- Нормализация: FromStart%, MinMax, Z-Score
- EMA-сглаживание для уменьшения шумов

---

## 8. UI/UX, темы, стили, рамки
- «KiloCode Modern Ticks»: современный стиль рисок, многоарочность, аккуратные шрифты
- Рамки виджетов: стили с подсветкой, контур, яркость боковой панели
- Меню «Widgets»: массовое применение опций, персистентность
- Меню «Tools»: доступ к «Обзору рынка» и «Сравнению графиков (норм.)»

---

## 9. Настройки (QSettings)

Пример ключей:
```
Widgets/VolumeVis = sidebar|fuel|second_needle
Widgets/FrameStyle = flat|glow|...
Widgets/SidebarWidth = small|medium|large
Widgets/SidebarOutline = true
Widgets/SidebarBrightnessPct = 80
Widgets/Indicators/RSI = true
Widgets/Indicators/MACD = false
Widgets/Indicators/BB = true
Widgets/Anomalies/Enabled = true
Widgets/Anomalies/Mode = composite
Widgets/Overlays/Volatility = true
Widgets/Overlays/Change = false

Tools/Overview/WindowSec = 300
Tools/Overview/Weight = Equal|InverseVolatility
Tools/Overview/IncludeBTC = true
Tools/Overview/Excluded = XRP,TON

Tools/MultiCompare/IntervalSec = 900
Tools/MultiCompare/StepSec = 10
Tools/MultiCompare/NormMode = FromStart|MinMax|ZScore
Tools/MultiCompare/SmoothingEMA = 0.2
Tools/MultiCompare/AutoRefreshSec = 5
Tools/MultiCompare/Selections = BTC,ETH,BNB
```

Поведение: автоприменение на старте и сохранение при изменении.

---

## 10. Ошибки, исключения, устойчивость
- WS: обработка disconnect с кодами, мягкий backoff и reconnect
- JSON: защитный парсинг, значения по умолчанию при пропусках
- Нормализация: проверка деления на ноль, пустых массивов
- Рисование: обязательно сбалансированные save()/restore(), защита от nullptr

---

## 11. Логирование, наблюдаемость
- qDebug() в DataWorker: setMode, setProvider, setCurrencies, URLs, connect/disconnect
- Логи открытия окон и применений настроек
- В будущем: QLoggingCategory с уровнями, фильтрами

---

## 12. Производительность и масштабируемость
- История ограничена временным окном (обрезка старых значений)
- Ресэмплирование O(n) по сетке, SLA стабильное
- Рендер оптимизирован частотой перерисовок и объёмом данных
- Горизонт роста: вынесение парсинга в отдельный поток; батчинг сигналов

---

## 13. Безопасность и модель угроз
- Только публичные WS; нет секретов/ключей
- Локальные настройки — не чувствительные
- Угрозы: недоступность провайдера, аномальные данные — приводят к деградации, но не к крашу

---

## 14. Тестирование
- Смоук-тесты: сборка, запуск, подключение, визуальная проверка окон
- Юнит-тесты (план): математика агрегатора, нормализации, ресэмплинг
- Визуальные регрессии (план): мок-поток и сравнение скриншотов

---

## 15. Сборка, упаковка, деплой
- CMake + Ninja, Qt 6.9.1; цель: modular_dashboard.app
- Упаковка: zip .app (Debug/Release)
- Выпуск 1.0.0: версии bump в CMake/About, сборка, zip-артефакт

---

## 16. CI/CD (план)
- GitHub Actions (macOS): установка Qt, сборка, выгрузка артефактов
- Позже: подпись, нотризация, выкладка релизов

---

## 17. Метрики и мониторинг (план)
- Счётчики: частота тика, reconnects, задержки, FPS
- Отчёт в overlay или экспорт простых чисел (файл/порт)

---

## 18. Документация и стиль кода
- Докстринги для ключевых модулей, комментарии алгоритмов
- Рекомендация: добавить .clang-format и проверять в CI

---

## 19. Принципы и паттерны
- Observer (signals/slots), модульность окон, разделение данных/представления
- Конфигурация как данные (QSettings), SRP, явные API сеттеров

---

## 20. Структура репозитория
- CMakeLists.txt (root)
- modular_dashboard/
  - CMakeLists.txt
  - include/
    - MainWindow.h, DynamicSpeedometerCharts.h, MarketAnalyzer.h, MarketGaugeWidget.h, MarketOverviewWindow.h, MultiCompareWindow.h
  - src/
    - MainWindow.cpp, DynamicSpeedometerCharts.cpp, MarketAnalyzer.cpp, MarketGaugeWidget.cpp, MarketOverviewWindow.cpp, MultiCompareWindow.cpp
- docs/
  - ARCHITECTURE.md (EN)
  - ARCHITECTURE_RU.md (RU)

---

## 21. Псевдокод ключевых участков

### 21.1 DataWorker (connect/retry)
```
start(provider, mode, symbols):
  url = buildUrl(provider, mode, symbols)
  ws.open(url)

onConnected(): log("connected")

onDisconnected(code, reason):
  log("disconnected", code, reason)
  retryWithBackoff()

onMessage(msg):
  for e in parse(provider, mode, msg): emit quoteReceived(e.symbol, e.price, e.tsMs, e.volume)
```

### 21.2 MainWindow (data routing)
```
onQuote(sym, price, ts, vol):
  if w := widgets[sym]: w.update(ts, price, vol)
  analyzer.updateSymbol(sym, ts, price)
```

### 21.3 Autoscale
```
[min, max] = extremes(history in windowSec)
if nearlyEqual(min, max): [min, max] = expandByEps(min, max)
low  = min * (1 - padding)
high = max * (1 + padding)
applyScale(low, high)
```

### 21.4 MarketAnalyzer
```
sumW=0; sumWS=0
for s in symbols where !excluded:
  slope = regressionSlope(t[s], x[s])
  w = (weight==Equal) ? 1 : invVol(volatility[s])
  sumW += w; sumWS += w*slope
consensus = (sumW>0)? sumWS/sumW : 0
strength = sigmoid(consensus)
confidence = 1 - normalizedVar(slopes)
label = mapToLabel(consensus, strength, confidence)
```

### 21.5 MultiCompare resample/normalize
```
for each S:
  raw = historySnapshot(S)
  grid = buildGrid(tMin, tMax, step)
  rs = carryForward(raw, grid)
  rs = normalize(rs, mode)
  if alpha>0: rs = EMA(rs, alpha)
  chart.addSeries(S, rs)
```

---

## 22. Сценарии использования
- Мониторинг ТОП-альтов с визуализацией объёма и индикаторов
- Оценка рыночного фона (в т.ч. без BTC) через «Общий обзор рынка»
- Сравнение нескольких активов на одной нормализованной шкале

---

## 23. Траблшутинг
- Пустой график: проверьте сетку времени и наличие истории
- Вторая стрелка объёма на нуле: проверьте TICKER-нормализацию (24h fallback)
- Подлагивания: уменьшите окно истории/частоту автообновлений

---

## 24. Лицензирование
Проприетарно. Внутреннее использование.

---

## 25. Changelog 1.0.0
- Завершено окно «Сравнение графиков (норм.)» и интеграция в меню Tools
- Стабилизирован модуль «Общий обзор рынка»
- Добавлено глобальное меню Widgets
- Улучшено автомасштабирование: сохранение экстремумов, режимы
- Современные темы «KiloCode Modern Ticks», объём (Sidebar/Fuel/Second Needle), стабильность

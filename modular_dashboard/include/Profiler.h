#pragma once
#include <QElapsedTimer>
#include <QMap>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>

class Profiler {
public:
    static void setEnabled(bool en) { enabled() = en; }
    static bool isEnabled() { return enabled(); }
    class Scope {
    public:
        Scope(const char* name) : _name(name) { if (Profiler::isEnabled()) _timer.start(); }
        ~Scope() {
            if (!Profiler::isEnabled()) return;
            qint64 ns = _timer.nsecsElapsed();
            totals()[_name] += ns;
            counts()[_name] += 1;
            maybeDump();
        }
    private:
        const char* _name; QElapsedTimer _timer;
    };
    static void maybeDump() {
        if (!isEnabled()) return;
        qint64 now = dumpTimer().elapsed();
        if (now - lastDumpMs() < 5000) return; // dump each 5 seconds
        dumpToFile(); lastDumpMs() = now;
    }
    static void dumpToFile() {
        QString path = QCoreApplication::applicationDirPath() + "/profiler_stats.txt";
        QFile f(path);
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&f);
            out << "==== PROFILER DUMP " << QDateTime::currentDateTime().toString(Qt::ISODate) << " ====" << '\n';
            for (auto it = totals().cbegin(); it != totals().cend(); ++it) {
                qint64 ns = it.value(); qint64 c = counts().value(it.key());
                double totalMs = ns / 1e6; double avgMs = c>0 ? (totalMs / double(c)) : 0.0;
                out << it.key() << ": calls=" << c << ", total_ms=" << QString::number(totalMs,'f',3)
                    << ", avg_ms=" << QString::number(avgMs,'f',3) << '\n';
            }
            out << '\n';
        }
    }
private:
    static bool& enabled() { static bool e = false; return e; }
    static QMap<QString,qint64>& totals() { static QMap<QString,qint64> t; return t; }
    static QMap<QString,qint64>& counts() { static QMap<QString,qint64> c; return c; }
    static qint64& lastDumpMs() { static qint64 v = 0; return v; }
    static QElapsedTimer& dumpTimer() { static QElapsedTimer t = [](){ QElapsedTimer z; z.start(); return z; }(); return t; }
};

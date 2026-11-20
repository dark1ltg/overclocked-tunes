#pragma once

#include "JobGate.h"
#include "Types.h"

#include <QObject>
#include <QString>
#include <QVector>
#include <atomic>

class CatalogSync : public QObject {
    Q_OBJECT
public:
    explicit CatalogSync(QObject *parent = nullptr);

    void setDelayMs(int ms) { m_delayMs = ms; }
    void setJobGate(JobGate *gate) { m_gate = gate; }
    void cancel();
    bool isRunning() const { return m_running; }

public slots:
    void start();

signals:
    void progress(int current, int total, const QString &message);
    void remixesParsed(const QVector<RemixRecord> &records);
    void albumsParsed(const QVector<AlbumRecord> &records);
    void finished(bool ok, const QString &message);

private:
    bool fetchPages(const QString &listUrl, bool albums, int *outGot, int *outListed);
    int m_delayMs = 400;
    JobGate *m_gate = nullptr;
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_running{false};
};

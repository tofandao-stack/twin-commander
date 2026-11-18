#pragma once
#include <QThreadPool>

// Qt's raster paint engine can synchronously use the global pool. File jobs must
// not occupy those threads, especially on a single-core host or while paused.
inline QThreadPool *ioPool()
{
    static QThreadPool pool;
    static const bool configured = [] { pool.setMaxThreadCount(4); pool.setExpiryTimeout(30000); return true; }();
    Q_UNUSED(configured);
    return &pool;
}

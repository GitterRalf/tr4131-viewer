#pragma once

#include <QMouseEvent>

inline QPointF mousePosition(const QMouseEvent *event)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    return event->localPos();
#else
    return event->position();
#endif
}

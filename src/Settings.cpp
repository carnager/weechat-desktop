#include "Settings.h"

#include <QFile>

namespace {
Settings *g_settings = nullptr;
}

Settings::Settings(QObject *parent)
    : QObject(parent)
{
    g_settings = this;
    // The relay password is stored here, so keep the file private to the user.
    const QString path = m_s.fileName();
    if (QFile::exists(path))
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    connect(this, &Settings::passwordChanged, this, [this] {
        m_s.sync();
        QFile::setPermissions(m_s.fileName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    });
}

Settings *Settings::instance() { return g_settings; }

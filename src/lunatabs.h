// SPDX-License-Identifier: MIT
#pragma once
#include <QStringList>
namespace luna {
int runTabbedLibrary(const QStringList& files, const QString& report = {}, bool restore = false);
}

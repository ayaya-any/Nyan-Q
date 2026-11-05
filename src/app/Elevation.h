#pragma once

#include <QString>
#include <QStringList>

namespace Elevation {

enum class RelaunchResult {
    Started,
    Canceled,
    Failed,
};

// Restarts Nyan-Q elevated through the Windows UAC prompt. The caller is
// expected to quit after Started is returned; the new instance waits for the
// single-instance lock to be released.
RelaunchResult relaunchAsAdministrator(const QStringList& arguments,
                                       QString* error = nullptr);

}  // namespace Elevation

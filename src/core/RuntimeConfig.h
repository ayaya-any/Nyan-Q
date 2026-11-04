#pragma once

#include "core/Types.h"

#include <QString>

namespace RuntimeConfig {

// Builds a sanitized runtime copy of a profile. Top-level network/security
// settings are owned by Clash-Q so a subscription cannot silently expose the
// controller or change the listening ports.
QString build(const QString& source, const cq::AppSettings& settings);
bool write(const QString& sourcePath, const QString& destinationPath,
           const cq::AppSettings& settings, QString* error);

}  // namespace RuntimeConfig

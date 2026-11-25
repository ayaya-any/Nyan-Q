#pragma once

#include "core/Types.h"

#include <QString>
#include <QStringList>

namespace RuntimeConfig {

// Builds a sanitized runtime copy of a profile. Top-level network/security
// settings are owned by Nyan-Q so a subscription cannot silently expose the
// controller or change the listening ports.
//
// TUN settings keep the parts a profile may tune (stack, mtu, strict-route,
// route-address, route-exclude-address, ...) while Nyan-Q still owns enable,
// auto-route, auto-detect-interface and dns-hijack.
//
// `error` is set when a TUN value Nyan-Q cannot apply safely is found (for
// example a malformed route-exclude-address entry). `warnings` carries
// non-fatal notes: preserved route counts, merged deprecated keys, or a
// profile that could not be parsed for TUN extraction.
QString build(const QString& source, const cq::AppSettings& settings,
              QString* error = nullptr, QStringList* warnings = nullptr);
bool write(const QString& sourcePath, const QString& destinationPath,
           const cq::AppSettings& settings, QString* error,
           QStringList* warnings = nullptr);

}  // namespace RuntimeConfig

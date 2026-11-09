#pragma once

#include <QString>

// A representative Mihomo configuration used by the YAML and script tests.
// Kept in its own translation unit so moc never has to parse the raw string.
QString yamlSampleConfig();

QString yamlSampleConfigWithoutProxyProvider();

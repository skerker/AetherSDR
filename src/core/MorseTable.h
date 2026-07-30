#pragma once

#include <QChar>
#include <QHash>
#include <QString>

namespace AetherSDR {

// Standard ITU Morse table, shared by CwxLocalKeyer (sidetone element
// scheduling) and the sim NoiseMixer (demo CW channel). Uppercase keys;
// callers uppercase incoming text before lookup. Punctuation mirrors what
// FlexRadio's CWX accepts on the wire; on an unknown character both callers
// emit a word gap so the message stays time-aligned.
const QHash<QChar, QString>& morseTable();

} // namespace AetherSDR

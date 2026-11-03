#pragma once

#include "tjsString.h"
#include "tjsVariant.h"

namespace mdk {

// Parses MDK scenario text into the plugin's TJS Dictionary/Array tree.
// File decoding remains the engine text-stream boundary.
class Parser {
public:
    tTJSVariant parse(const ttstr &source);
};

} // namespace mdk

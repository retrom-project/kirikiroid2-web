#pragma once

#include <istream>
#include "tjs.h"

namespace DataPack {

bool ReadVariantArchive(std::istream &input, TJS::tTJSVariant &result,
                        tjs_uint32 seed, bool bigendian);

} // namespace DataPack

#ifndef TVP_HOST_STRUCTURED_SAVE_H
#define TVP_HOST_STRUCTURED_SAVE_H

#ifdef __EMSCRIPTEN__
#include "tjsString.h"

// Web publication boundary; the original serializer owns the file and result.
bool TVPIsImageBackedStructuredSave(const TJS::ttstr &name,
                                  const TJS::ttstr &mode) noexcept;
void *TVPBeginGameSaveObservation() noexcept;
void TVPEndGameSaveObservation(void *call, bool success) noexcept;

class tTVPStructuredSaveObservation final {
    void *call_;
public:
    tTVPStructuredSaveObservation(const TJS::ttstr &name,
                                 const TJS::ttstr &mode, bool string_name)
        : call_(string_name && TVPIsImageBackedStructuredSave(name, mode)
                    ? TVPBeginGameSaveObservation() : nullptr) {}
    ~tTVPStructuredSaveObservation() { TVPEndGameSaveObservation(call_, false); }
    void complete() noexcept {
        TVPEndGameSaveObservation(call_, true);
        call_ = nullptr;
    }
    tTVPStructuredSaveObservation(const tTVPStructuredSaveObservation &) = delete;
    tTVPStructuredSaveObservation &operator=(const tTVPStructuredSaveObservation &) = delete;
};
#endif
#endif

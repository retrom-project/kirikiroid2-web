// Supplemental PackinOne reader; evidence and platform boundary are recorded
// in analysis/datapack-reader.md. This does not change the built-in Scripts API.
#define NCB_MODULE_NAME TJS_W("PackinOne.dll")
#include "ncbind.hpp"
#include "Reader.h"
#include "VariantArchive.h"
#include "StorageIntf.h"
#include "MsgIntf.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <istream>
#include <memory>
#include <stdexcept>

namespace DataPack {
namespace {

class StorageReader final : public Reader {
    std::unique_ptr<tTJSBinaryStream> stream;

public:
    explicit StorageReader(const ttstr &name) :
        stream(TVPCreateStream(TVPNormalizeStorageName(name), TJS_BS_READ)) {}

    std::size_t Read(void *buffer, std::size_t size) override {
        return stream->Read(buffer, static_cast<tjs_uint>(size));
    }

    std::int64_t Seek(std::int64_t offset, int origin) override {
        return stream->Seek(offset, origin);
    }
};

class StringReader final : public Reader {
    const tjs_uint8 *data;
    std::size_t length;
    std::size_t position = 0;

public:
    explicit StringReader(tTJSVariantOctet *octet) :
        data(octet->GetData()), length(octet->GetLength()) {}

    std::size_t Read(void *buffer, std::size_t size) override {
        if(!data || !size)
            return 0;
        const auto count = std::min(size, length - position);
        std::memcpy(buffer, data + position, count);
        position += count;
        return count;
    }

    std::int64_t Seek(std::int64_t offset, int origin) override {
        const std::int64_t base = origin == SEEK_SET ? 0 :
            origin == SEEK_CUR ? position : length;
        if(offset < -base || offset > static_cast<std::int64_t>(length) - base)
            return -1;
        position = static_cast<std::size_t>(base + offset);
        return position;
    }
};

std::string VariantBytes(const tTJSVariant &value) {
    if(value.Type() == tvtString) {
        const ttstr text(value);
        return { reinterpret_cast<const char *>(text.c_str()),
                 (text.length() + 1) * sizeof(tjs_char) };
    }
    if(value.Type() == tvtOctet) {
        const auto *octet = value.AsOctetNoAddRef();
        if(octet && octet->GetLength())
            return { reinterpret_cast<const char *>(octet->GetData()),
                     octet->GetLength() };
    }
    return {};
}

Options ReadOptions(iTJSDispatch2 *object) {
    Options options;
    if(!object)
        return options;
    auto get = [&](const tjs_char *name, auto apply) {
        tTJSVariant value;
        if(TJS_SUCCEEDED(object->PropGet(TJS_MEMBERMUSTEXIST, name, nullptr, &value, object)))
            apply(value);
    };
    get(TJS_W("nocheck"), [&](const auto &v) { options.nocheck = bool(v); });
    get(TJS_W("bigendian"), [&](const auto &v) { options.bigendian = bool(v); });
    get(TJS_W("compress"), [&](const auto &v) { options.compress = tjs_int(v); });
    get(TJS_W("cryptmode"), [&](const auto &v) { options.cryptmode = tjs_int(v); });
    get(TJS_W("seed"), [&](const auto &v) { options.seed = tjs_int(v); });
    get(TJS_W("iv"), [&](const auto &v) { options.iv = VariantBytes(v); });
    get(TJS_W("outeriv"), [&](const auto &v) { options.outeriv = VariantBytes(v); });
    get(TJS_W("comment"), [](const auto &v) { const ttstr comment(v); });
    return options;
}

void WriteOptions(iTJSDispatch2 *object, const Options &options) {
    if(!object)
        return;
    auto put = [&](const tjs_char *name, const tTJSVariant &value) {
        object->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, object);
    };
    put(TJS_W("checkfail"), tTJSVariant(options.checkfail));
    put(TJS_W("bigendian"), tTJSVariant(options.bigendian));
    put(TJS_W("compress"), tTJSVariant(options.compress));
    put(TJS_W("cryptmode"), tTJSVariant(options.cryptmode));
    put(TJS_W("seed"), tTJSVariant(static_cast<tTVInteger>(options.seed)));
    put(TJS_W("iv"), tTJSVariant(
        reinterpret_cast<const tjs_uint8 *>(options.iv.data()), options.iv.size()));
}

} // namespace

class ScriptsDataPack {
public:
    static tjs_error loadDataPack(tTJSVariant *result, tjs_int count,
                                  tTJSVariant **parameters, iTJSDispatch2 *) {
        if(count < 1)
            return TJS_E_BADPARAMCOUNT;
        try {
            iTJSDispatch2 *optionObject = count > 1 && parameters[1]->Type() == tvtObject
                ? parameters[1]->AsObjectNoAddRef() : nullptr;
            auto options = ReadOptions(optionObject);
            std::unique_ptr<Reader> reader;
            if(parameters[0]->Type() == tvtString) {
                reader = std::make_unique<StorageReader>(ttstr(*parameters[0]));
            } else if(parameters[0]->Type() == tvtOctet) {
                auto *octet = parameters[0]->AsOctetNoAddRef();
                if(!octet)
                    return TJS_E_INVALIDPARAM;
                reader = std::make_unique<StringReader>(octet);
            } else {
                return TJS_E_INVALIDPARAM;
            }
            if(result)
                result->Clear();
            if(!UnwrapCarrier(reader))
                throw std::runtime_error("unrecognized data-pack stream");
            ReadHeader(*reader, options);
            reader = MakeReadFilters(std::move(reader), options);
            ArchiveStreamBuf buffer(*reader, options.compress ? 4100 : 4096);
            std::istream input(&buffer);
            tTJSVariant root;
            options.checkfail = !ReadVariantArchive(input, root, options.seed,
                                                   options.bigendian);
            if(!options.nocheck && options.checkfail)
                throw std::runtime_error("data-pack check failed");
            if(result)
                *result = root;
            WriteOptions(optionObject, options);
            return TJS_S_OK;
        } catch(const std::exception &error) {
            TVPThrowExceptionMessage(ttstr(error.what()).c_str());
        }
        return TJS_E_FAIL;
    }
};

} // namespace DataPack

using DataPack::ScriptsDataPack;

NCB_ATTACH_CLASS(ScriptsDataPack, Scripts) {
    RawCallback("loadDataPack", &ScriptsDataPack::loadDataPack, TJS_STATICMEMBER);
}

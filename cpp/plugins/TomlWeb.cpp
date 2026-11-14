#include "ncbind.hpp"
#include "DebugIntf.h"
#include "StorageIntf.h"
#include "TextStream.h"
#include "tjsArray.h"
#include "tjsDictionary.h"

#include <boost/locale/encoding_utf.hpp>
#include <toml.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#define NCB_MODULE_NAME TJS_W("toml.dll")

namespace {
    std::string utf8(const ttstr &text) {
        return boost::locale::conv::utf_to_utf<char>(
            text.c_str(), text.c_str() + text.length());
    }

    ttstr wide(const std::string &text) {
        const auto chars = boost::locale::conv::utf_to_utf<tjs_char>(
            text.data(), text.data() + text.size());
        return ttstr(chars.data(), chars.size());
    }

    tTJSVariant nullObject() {
        return tTJSVariant(static_cast<iTJSDispatch2 *>(nullptr), nullptr);
    }

    tTJSVariant octet(const std::vector<tjs_uint8> &bytes) {
        return tTJSVariant(bytes.empty() ? nullptr : bytes.data(), bytes.size());
    }

    tTJSVariant hexOctet(const std::string &text) {
        std::vector<tjs_uint8> bytes;
        tjs_uint8 value = 0;
        bool half = false;
        for(unsigned char c : text) {
            int digit;
            if(c >= '0' && c <= '9') digit = c - '0';
            else if(c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if(c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else continue;
            value = static_cast<tjs_uint8>((value << 4) | digit);
            if(half) bytes.push_back(value);
            half = !half;
        }
        if(half) bytes.push_back(static_cast<tjs_uint8>(value << 4));
        return octet(bytes);
    }

    tTJSVariant convert(const toml::value &value) {
        switch(value.type()) {
            case toml::value_t::boolean:
                return tTJSVariant(static_cast<tjs_int>(value.as_boolean()));
            case toml::value_t::integer:
                return tTJSVariant(static_cast<tjs_int64>(value.as_integer()));
            case toml::value_t::floating:
                return tTJSVariant(static_cast<tjs_real>(value.as_floating()));
            case toml::value_t::string:
                return tTJSVariant(wide(value.as_string()));
            case toml::value_t::array: {
                const auto &items = value.as_array();
                iTJSDispatch2 *array = TJSCreateArrayObject();
                tTJSVariant result(array, array);
                array->Release();
                tTJSVariant count(static_cast<tjs_int>(items.size()));
                array->PropSet(0, TJS_W("count"), nullptr, &count, array);
                for(tjs_int index = 0; index < static_cast<tjs_int>(items.size()); ++index) {
                    tTJSVariant item = convert(items[index]);
                    array->PropSetByNum(TJS_MEMBERENSURE, index, &item, array);
                }
                return result;
            }
            case toml::value_t::table: {
                const auto &items = value.as_table();
                const auto envelope = items.find("");
                if(items.size() == 1 && envelope != items.end()) {
                    const auto &item = envelope->second;
                    if(item.is_boolean())
                        return item.as_boolean() ? nullObject() : tTJSVariant();
                    if(item.is_integer())
                        return item.as_integer() ? nullObject() : tTJSVariant();
                    if(item.is_floating())
                        return item.as_floating() != 0 ? nullObject() : tTJSVariant();
                    if(item.is_string())
                        return hexOctet(item.as_string());
                    if(item.is_array()) {
                        std::vector<tjs_uint8> bytes;
                        for(const auto &byte : item.as_array())
                            bytes.push_back(static_cast<tjs_uint8>(byte.as_integer()));
                        return octet(bytes);
                    }
                }
                iTJSDispatch2 *dictionary = TJSCreateDictionaryObject();
                tTJSVariant result(dictionary, dictionary);
                dictionary->Release();
                for(const auto &entry : items) {
                    const ttstr key = wide(entry.first);
                    if(key.IsEmpty()) continue;
                    tTJSVariant item = convert(entry.second);
                    dictionary->PropSet(TJS_MEMBERENSURE, key.c_str(), nullptr,
                                        &item, dictionary);
                }
                return result;
            }
            case toml::value_t::offset_datetime:
            case toml::value_t::local_datetime:
            case toml::value_t::local_date:
            case toml::value_t::local_time: {
                static bool reported = false;
                if(!reported) {
                    reported = true;
                    TVPThrowExceptionMessage(TJS_W("toml: datetime format is not supported."));
                }
                return nullObject();
            }
            default:
                return tTJSVariant();
        }
    }

    tjs_error failure(tTJSVariant *result, const ttstr &message) {
        if(result) result->Clear();
        if(!message.IsEmpty()) TVPAddImportantLog(message);
        return TJS_E_FAIL;
    }

    tjs_error decode(tTJSVariant *result, const ttstr &text, const ttstr &name) {
        try {
            std::istringstream input(utf8(text));
            const auto parsed = toml::parse(input, utf8(name), toml::spec::v(1, 0, 0));
            tTJSVariant converted = convert(parsed);
            if(result) *result = converted;
            return TJS_S_OK;
        } catch(const toml::exception &error) {
            return failure(result, wide(error.what()));
        }
    }

    struct TextReaderDeleter {
        void operator()(iTJSTextReadStream *reader) const { reader->Destruct(); }
    };

    struct TomlScripts {
        static tjs_error tomlDecodeFromString(tTJSVariant *result, tjs_int count,
                                               tTJSVariant **args, iTJSDispatch2 *) {
            if(count < 1) return TJS_E_BADPARAMCOUNT;
            const ttstr text(*args[0]);
            const ttstr name = count >= 2 && args[1]->Type() == tvtString
                ? ttstr(*args[1]) : ttstr(TJS_W("{imm-string}"));
            return decode(result, text, name);
        }

        static tjs_error tomlDecodeFromStorage(tTJSVariant *result, tjs_int count,
                                                tTJSVariant **args, iTJSDispatch2 *) {
            if(count < 1) return TJS_E_BADPARAMCOUNT;
            const ttstr name(*args[0]);
            const ttstr mode = count >= 2 && args[1]->Type() != tvtVoid
                ? ttstr(*args[1]) : ttstr();
            if(!TVPIsExistentStorage(name))
                return failure(result, ttstr(TJS_W("cannot open:")) + name);
            ttstr text;
            {
                std::unique_ptr<iTJSTextReadStream, TextReaderDeleter> reader(
                    TVPCreateTextStreamForRead(name, mode));
                if(!reader)
                    return failure(result, ttstr(TJS_W("cannot open:")) + name);
                reader->Read(text, 0);
            }
            return decode(result, text, name);
        }
    };
}

NCB_ATTACH_CLASS(TomlScripts, Scripts) {
    RawCallback(TJS_W("tomlDecodeFromString"), &TomlScripts::tomlDecodeFromString,
                 TJS_STATICMEMBER);
    RawCallback(TJS_W("tomlDecodeFromStorage"), &TomlScripts::tomlDecodeFromStorage,
                 TJS_STATICMEMBER);
}

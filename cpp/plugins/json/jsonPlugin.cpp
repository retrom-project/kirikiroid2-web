#include "ncbind.hpp"
#include "StorageIntf.h"
#include "tjsArray.h"
#include "tjsConfig.h"
#include "tjsDictionary.h"

#include <vector>

#define NCB_MODULE_NAME TJS_W("json.dll")

namespace {

class JsonInput {
protected:
    bool error = false;

    virtual int read() = 0;
    virtual void unread() = 0;
    virtual void finish() = 0;

    int next() {
        for(;;) {
            int c = read();
            if(c == '#') {
                do { c = read(); } while(c != -1 && c != '\r' && c != '\n');
            } else if(c == '/') {
                c = read();
                if(c == '/') {
                    do { c = read(); } while(c != -1 && c != '\r' && c != '\n');
                } else if(c == '*') {
                    for(;;) {
                        c = read();
                        if(c == -1) {
                            error = true;
                            return -1;
                        }
                        if(c == '*') {
                            if(read() == '/') break;
                            unread();
                        }
                    }
                } else {
                    unread();
                    return '/';
                }
            } else if(c == -1 || c > 0x20) {
                return c;
            }
        }
    }

    static bool isHex(int c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F');
    }

    static tjs_int32 hexValue(const tjs_char *p) {
        while(*p && *p <= 0x20) ++p;
        tjs_uint32 result = 0;
        while(isHex(*p)) {
            int c = *p++;
            int digit = c <= '9' ? c - '0' : (c <= 'F' ? c - 'A' + 10 : c - 'a' + 10);
            result = result * 16 + digit;
        }
        return static_cast<tjs_int32>(result);
    }

    ttstr readCharacters(int count) {
        ttstr text;
        for(int i = 0; i < count; ++i) {
            int c = read();
            if(c == -1) break;
            text += static_cast<tjs_char>(c);
        }
        return text;
    }

    void string(int quote, tTJSVariant &result) {
        ttstr text;
        for(;;) {
            int c = read();
            if(c == '\\') {
                c = read();
                switch(c) {
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'n': c = '\n'; break;
                    case 'r': c = '\r'; break;
                    case 't': c = '\t'; break;
                    case 'u': {
                        ttstr digits = readCharacters(4);
                        c = hexValue(digits.c_str());
                        break;
                    }
                    case 'x': {
                        ttstr digits = readCharacters(2);
                        c = hexValue(digits.c_str());
                        break;
                    }
                    default: break;
                }
                text += static_cast<tjs_char>(c);
            } else if(c == 0 || c == '\n' || c == '\r') {
                error = true;
                return;
            } else if(c == quote) {
                result = text;
                return;
            } else {
                text += static_cast<tjs_char>(c);
            }
        }
    }

    void array(tTJSVariant &result) {
        iTJSDispatch2 *object = TJSCreateArrayObject();
        result.SetObject(object, object);
        object->Release();
        for(tjs_int index = 0;; ++index) {
            int c = next();
            if(c == ']') return;
            if(c == -1) { error = true; return; }
            unread();
            tTJSVariant item;
            if(c != ',' && c != ';') value(item);
            object->PropSetByNum(TJS_MEMBERENSURE, index, &item, object);
            c = next();
            if(c == ']') return;
            if(c != ',' && c != ';') { error = true; return; }
        }
    }

    void dictionary(tTJSVariant &result) {
        iTJSDispatch2 *object = TJSCreateDictionaryObject();
        result.SetObject(object, object);
        object->Release();
        for(;;) {
            int c = next();
            if(c == '}') return;
            if(c == -1) { error = true; return; }
            unread();
            if(c != ',' && c != ';') {
                tTJSVariant key;
                value(key);
                c = next();
                if(c == '=') {
                    if(read() != '>') unread();
                } else if(c != ':') {
                    error = true;
                    return;
                }
                tTJSVariant item;
                value(item);
                object->PropSet(TJS_MEMBERENSURE, key.GetString(), nullptr, &item, object);
            }
            c = next();
            if(c == '}') return;
            if(c != ',' && c != ';') { error = true; return; }
        }
    }

    void value(tTJSVariant &result) {
        int c = next();
        if(c == '"' || c == '\'') {
            string(c, result);
        } else if(c == '[') {
            array(result);
        } else if(c == '{') {
            dictionary(result);
        } else if((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.') {
            ttstr token;
            bool real = false;
            do {
                token += static_cast<tjs_char>(c);
                if(c == '.') real = true;
                c = read();
            } while((c >= '0' && c <= '9') || c == '+' || c == '-' ||
                    c == '.' || c == 'E' || c == 'e');
            unread();
            if(real) result = static_cast<tTVReal>(TJS_strtod(token.c_str(), nullptr));
            else result = static_cast<tTVInteger>(TJS_atoll(token.c_str()));
        } else if(c == '$') {
            ttstr token;
            while(isHex(c = read())) token += static_cast<tjs_char>(c);
            unread();
            result = static_cast<tTVInteger>(hexValue(token.c_str()));
        } else if(c >= 'a' && c <= 'z') {
            ttstr token;
            do {
                token += static_cast<tjs_char>(c);
                c = read();
            } while(c >= 'a' && c <= 'z');
            unread();
            if(token == TJS_W("true")) result = static_cast<tTVInteger>(1);
            else if(token == TJS_W("false")) result = static_cast<tTVInteger>(0);
            else if(token == TJS_W("null") || token == TJS_W("void")) result.Clear();
            else error = true;
        } else {
            error = true;
        }
    }

public:
    virtual ~JsonInput() = default;

    void parse(tTJSVariant *result) {
        if(result) value(*result);
        finish();
        if(error) TVPThrowExceptionMessage(TJS_W("Failed to parse json file"));
    }
};

class JsonStringInput : public JsonInput {
    ttstr text;
    const tjs_char *characters;
    tjs_uint length;
    tjs_uint position = 0;

protected:
    int read() override {
        return position < length ? characters[position++] : -1;
    }
    void unread() override { if(position) --position; }
    void finish() override {}

public:
    explicit JsonStringInput(const ttstr &input) :
        text(input), characters(text.c_str()), length(text.length()) {}
};

class JsonFileInput : public JsonInput {
    ttstr text;
    tjs_uint position = 0;
    bool unused = false;

protected:
    int read() override {
        return position < static_cast<tjs_uint>(text.length()) ? text[position++] : -1;
    }
    void unread() override { if(position) --position; }
    void finish() override {}

public:
    JsonFileInput(const ttstr &filename, int mode) {
        tTJSBinaryStream *stream = TVPCreateStream(filename, TJS_BS_READ);
        if(!stream) {
            ttstr message = TJS_W("cannot open : ");
            message += filename;
            TVPThrowExceptionMessage(message.c_str());
        }
        tjs_uint size = static_cast<tjs_uint>(stream->GetSize());
        if(size > 0) {
            std::vector<char> buffer(size + 2);
            buffer[size] = buffer[size + 1] = 0;
            stream->Read(buffer.data(), size);
            if(static_cast<unsigned char>(buffer[0]) == 0xff &&
               static_cast<unsigned char>(buffer[1]) == 0xfe) {
                text = reinterpret_cast<const tjs_char *>(buffer.data() + 2);
            } else if(mode == 8) {
                text = buffer.data();
            } else if(mode == 0) {
                size_t count = TJS_mbstowcs(nullptr, buffer.data(), 0);
                if(count == static_cast<size_t>(-1))
                    TVPThrowExceptionMessage(TJSNarrowToWideConversionError);
                tjs_char *wide = new tjs_char[count + 1];
                wide[count] = 0;
                TJS_mbstowcs(wide, buffer.data(), count);
                text = wide;
                delete[] wide;
            }
        }
    }
};

} // namespace

struct JsonScripts {
    static tjs_error evalJSON(tTJSVariant *result, tjs_int numparams,
                              tTJSVariant **param, iTJSDispatch2 *) {
        if(numparams < 1) return TJS_E_BADPARAMCOUNT;
        JsonStringInput input{ ttstr(param[0]->GetString()) };
        input.parse(result);
        return TJS_S_OK;
    }

    static tjs_error evalJSONStorage(tTJSVariant *result, tjs_int numparams,
                                     tTJSVariant **param, iTJSDispatch2 *) {
        if(numparams < 1) return TJS_E_BADPARAMCOUNT;
        JsonFileInput input(ttstr(param[0]->AsStringNoAddRef()), numparams > 1 && param[1] ? 8 : 0);
        input.parse(result);
        return TJS_S_OK;
    }
};

NCB_ATTACH_CLASS(JsonScripts, Scripts) {
    RawCallback("evalJSON", &JsonScripts::evalJSON, TJS_STATICMEMBER);
    RawCallback("evalJSONStorage", &JsonScripts::evalJSONStorage, TJS_STATICMEMBER);
}

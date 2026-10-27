#include "VariantArchive.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>

#include "DebugIntf.h"
#include "tjsArray.h"
#include "tjsDictionary.h"

namespace DataPack {
namespace {

using namespace TJS;

[[noreturn]] void Malformed() {
    throw std::runtime_error("malformed data");
}

tjs_uint32 Reverse32(tjs_uint32 value) {
    return (value >> 24) | ((value >> 8) & 0x0000ff00u) |
        ((value << 8) & 0x00ff0000u) | (value << 24);
}

tjs_uint64 Reverse64(tjs_uint64 value) {
    return (static_cast<tjs_uint64>(Reverse32(static_cast<tjs_uint32>(value)))
            << 32) |
        Reverse32(static_cast<tjs_uint32>(value >> 32));
}

class DefaultTypeByteChecker {
    int Status = 1;
    tjs_uint8 X;
    tjs_uint8 Y;
    tjs_uint8 Z;

public:
    explicit DefaultTypeByteChecker(tjs_uint32 seed) :
        X(static_cast<tjs_uint8>(seed ^ (seed >> 24))),
        Y(static_cast<tjs_uint8>(seed >> 8)),
        Z(static_cast<tjs_uint8>(seed >> 16)) {}

    tjs_uint8 Advance() {
        const auto value = static_cast<tjs_uint8>(X ^ (2 * X));
        X = Y;
        Y = Z;
        Z = static_cast<tjs_uint8>(
            value ^ Z ^ ((Z ^ (value >> 2)) >> 3));
        return Z;
    }

    virtual tjs_uint8 GetTypeCheck(tjs_uint8 type) {
        if(!Status)
            return 0;
        if(!type)
            return Z;
        return Advance();
    }

    virtual bool CheckType(tjs_uint8 actual, tjs_uint8 expected) {
        if(!Status || actual == expected)
            return true;
        Status = -1;
        return false;
    }

    void AdvanceThree() {
        Advance();
        Advance();
        Advance();
    }

    int GetStatus() const { return Status; }

    tjs_uint32 GetStateValue() const {
        if(!Status)
            return 0;
        return (static_cast<tjs_uint32>(X) << 16) |
            (static_cast<tjs_uint32>(Y) << 8) | Z;
    }
};

template <class Stream>
class BinaryStreamForRead {
    Stream &Input;
    bool BigEndian;

protected:
    ~BinaryStreamForRead() = default;

public:
    BinaryStreamForRead(Stream &input, bool bigendian) :
        Input(input), BigEndian(bigendian) {}

    virtual DefaultTypeByteChecker *GetChecker() { return nullptr; }

    void ReadRaw(void *output, tjs_uint count) {
        Input.read(static_cast<char *>(output), count);
        CheckStream();
    }

    void CheckStream() const {
        if(Input.fail())
            Malformed();
    }

    tjs_uint8 ReadByte() {
        tjs_uint8 value;
        ReadRaw(&value, sizeof(value));
        return value;
    }

    tjs_uint32 ReadUInt32() {
        tjs_uint32 value;
        ReadRaw(&value, sizeof(value));
        return BigEndian ? Reverse32(value) : value;
    }

    tjs_int32 ReadInt32() {
        return static_cast<tjs_int32>(ReadUInt32());
    }

    tjs_int64 ReadInt64() {
        tjs_uint64 value;
        ReadRaw(&value, sizeof(value));
        if(BigEndian)
            value = Reverse64(value);
        return static_cast<tjs_int64>(value);
    }

    tjs_real ReadReal() {
        tjs_uint64 bits;
        ReadRaw(&bits, sizeof(bits));
        if(BigEndian)
            bits = Reverse64(bits);
        tjs_real value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
};

template <class Archive>
class CheckerByteArchive final : public Archive {
    DefaultTypeByteChecker *Checker;

public:
    CheckerByteArchive(std::istream &input, bool bigendian,
                       DefaultTypeByteChecker *checker) :
        Archive(input, bigendian), Checker(checker) {}

    DefaultTypeByteChecker *GetChecker() override { return Checker; }
};

using VariantArchive = BinaryStreamForRead<std::istream>;

void LogObject(const ttstr &message, const tTJSVariantClosure &object) {
    ttstr line(message);
    tTJSVariantString *description = TJSObjectToString(object);
    if(description) {
        line += TJS_W(": ");
        line += static_cast<const tjs_char *>(*description);
        description->Release();
    }
    TVPAddLog(line);
}

tjs_uint8 ReadType(VariantArchive &archive) {
    const tjs_uint8 type = archive.ReadByte();
    const tjs_uint8 check = archive.ReadByte();
    DefaultTypeByteChecker *checker = archive.GetChecker();
    if(checker)
        checker->CheckType(check, checker->GetTypeCheck(type));
    return type;
}

void ReadString(VariantArchive &archive, ttstr &value) {
    const tjs_int32 count = archive.ReadInt32();
    value.Clear();
    if(count > 0) {
        tjs_char *buffer = value.AllocBuffer(count);
        if(!buffer)
            throw std::bad_alloc();
        archive.ReadRaw(buffer, static_cast<tjs_uint>(count) * 2u);
    }
}

void ReadBytes(VariantArchive &archive, std::string &value) {
    tjs_uint32 remaining = archive.ReadUInt32();
    value.clear();
    char buffer[4096];
    while(remaining) {
        const tjs_uint32 count =
            std::min(remaining, static_cast<tjs_uint32>(sizeof(buffer)));
        archive.ReadRaw(buffer, count);
        const std::string part(buffer, count);
        value.append(part, 0, std::string::npos);
        remaining -= count;
    }
}

bool GetDictionaryClass(tTJSVariantClosure *result) {
    static iTJSDispatch2 *dictionaryClass = nullptr;
    if(result) {
        if(!dictionaryClass) {
            iTJSDispatch2 *temporary =
                TJSCreateDictionaryObject(&dictionaryClass);
            if(temporary)
                temporary->Release();
        }
        if(dictionaryClass) {
            result->Object = dictionaryClass;
            result->ObjThis = dictionaryClass;
        }
    } else {
        if(dictionaryClass)
            dictionaryClass->Release();
        dictionaryClass = nullptr;
    }
    return true;
}

iTJSDispatch2 *CreateDictionary(tjs_int32 count) {
    iTJSDispatch2 *dictionary = nullptr;
    if(count >= 0) {
        tTJSVariantClosure dictionaryClass;
        if(GetDictionaryClass(&dictionaryClass)) {
            tTJSVariant value(count);
            tTJSVariant *arguments[] = { &value };
            if(TJS_FAILED(dictionaryClass.CreateNew(
                   0, nullptr, nullptr, &dictionary, 1, arguments, nullptr))) {
                LogObject(TJS_W("Dictionary.CreateNew failed"),
                          dictionaryClass);
            }
        }
    }
    return dictionary;
}

void ReadVariant(VariantArchive &archive, tTJSVariant &result);

void ReadArray(VariantArchive &archive, const tTJSVariantClosure &array) {
    static ttstr countName(TJS_W("count"));
    const tjs_int32 count = archive.ReadInt32();
    tTJSVariant value(count);
    if(TJS_FAILED(array.PropSet(TJS_MEMBERMUSTEXIST, countName.c_str(),
                                countName.GetHint(), &value, nullptr))) {
        LogObject(TJS_W("ArrayPropSet Failed:") + countName, array);
    }

    const tjs_char *errorPrefix = TJS_W("ArrayPropSet Failed:");
    for(tjs_int32 index = 0; index < count; ++index) {
        value.Clear();
        ReadVariant(archive, value);
        if(TJS_FAILED(array.PropSetByNum(
               TJS_MEMBERENSURE | TJS_IGNOREPROP, index, &value, nullptr))) {
            LogObject(errorPrefix, array);
        }
        ++errorPrefix;
    }
}

iTJSDispatch2 *ReadDictionary(VariantArchive &archive) {
    const tjs_int32 count = archive.ReadInt32();
    iTJSDispatch2 *dictionary = CreateDictionary(count);
    if(!dictionary)
        return nullptr;
    const tTJSVariantClosure closure(dictionary, dictionary);
    ttstr key;
    tTJSVariant value;
    for(tjs_int32 index = 0; index < count; ++index) {
        key.Clear();
        value.Clear();
        ReadString(archive, key);
        ReadVariant(archive, value);
        if(TJS_FAILED(closure.PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                                     key.c_str(), key.GetHint(), &value,
                                     nullptr))) {
            LogObject(TJS_W("DictionaryPropSet Failed:") + key, closure);
        }
    }
    return dictionary;
}

void ReadVariant(VariantArchive &archive, tTJSVariant &result) {
    result.Clear();
    switch(ReadType(archive)) {
        case 0:
            return;
        case 1:
            result = tTJSVariant(static_cast<iTJSDispatch2 *>(nullptr));
            return;
        case 2: {
            ttstr value;
            ReadString(archive, value);
            result = value;
            return;
        }
        case 3: {
            std::string value;
            ReadBytes(archive, value);
            result = tTJSVariant(
                reinterpret_cast<const tjs_uint8 *>(value.data()),
                static_cast<tjs_uint>(value.size()));
            return;
        }
        case 4:
            result = archive.ReadInt64();
            return;
        case 5:
            result = archive.ReadReal();
            return;
        case 0x81: {
            iTJSDispatch2 *array = TJSCreateArrayObject();
            result = tTJSVariant(array, array);
            array->Release();
            ReadArray(archive, tTJSVariantClosure(array, array));
            archive.CheckStream();
            return;
        }
        case 0xc1: {
            iTJSDispatch2 *dictionary = ReadDictionary(archive);
            if(!dictionary) {
                dictionary = TJSCreateDictionaryObject();
                LogObject(TJS_W("Invalid Dictionary item counts"),
                          tTJSVariantClosure(dictionary, dictionary));
            }
            result = tTJSVariant(dictionary, dictionary);
            dictionary->Release();
            archive.CheckStream();
            return;
        }
        default:
            Malformed();
    }
}

} // namespace

bool ReadVariantArchive(std::istream &input, TJS::tTJSVariant &result,
                        tjs_uint32 seed, bool bigendian) {
    DefaultTypeByteChecker checker(seed);
    CheckerByteArchive<VariantArchive> archive(input, bigendian, &checker);
    ReadVariant(archive, result);
    const tjs_uint32 actual = archive.ReadUInt32();
    checker.AdvanceThree();
    tjs_uint32 expected = checker.GetStateValue();
    if(bigendian)
        expected = Reverse32(expected);
    return checker.GetStatus() >= 0 && actual == expected;
}

} // namespace DataPack

#include "tjsCommHead.h"
#include "MDKParser.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "tjsError.h"
#include "tjsLex.h"

#include <cctype>
#include <deque>
#include <limits>
#include <map>
#include <vector>

namespace mdk {
namespace {

enum TokenType {
    End = 0, Next = 1, BeginTrans = 2, EndTrans = 3, Speaker = 4,
    Comment = 5, Selection = 6, Label = 7, DefaultTag = 8, ClearDefault = 9,
    OpenTag = 10, Text = 11, RubyStart = 12, LineBreak = 13, Ruby = 14,
    RubyEnd = 15, TextStyle = 16, InlineImage = 18, Greater = 19,
    Void = 20, Less = 21, Equal = 22, Sync = 23, NoWait = 24, Dot = 25,
    Plus = 26, Minus = 27, Star = 28, Slash = 29, Backslash = 30,
    Percent = 31, Caret = 32, LeftBracket = 33, RightBracket = 34,
    LeftParen = 35, RightParen = 36, Tilde = 37, Question = 38, Colon = 39,
    Scope = 40, Comma = 41, Semicolon = 42, LeftBrace = 43,
    RightBrace = 44, Hash = 45, Dollar = 46, Voice = 47, Storage = 48,
    Number = 49, Octet = 50, Identifier = 57
};

tTJSVariant dictionary() {
    auto *object = TJSCreateDictionaryObject();
    tTJSVariant value(object, object);
    object->Release();
    return value;
}

tTJSVariant array() {
    auto *object = TJSCreateArrayObject();
    tTJSVariant value(object, object);
    object->Release();
    return value;
}

void set(const tTJSVariant &object, const tjs_char *name,
         const tTJSVariant &value) {
    auto *dispatch = object.AsObjectNoAddRef();
    dispatch->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, dispatch);
}

bool contains(const tTJSVariant &object, const tjs_char *name) {
    if(object.Type() != tvtObject || !object.AsObjectNoAddRef())
        return false;
    tTJSVariant value;
    auto *dispatch = object.AsObjectNoAddRef();
    return dispatch->PropGet(TJS_IGNOREPROP, name, nullptr, &value,
                             dispatch) != TJS_E_MEMBERNOTFOUND;
}

void append(const tTJSVariant &object, tjs_int index,
            const tTJSVariant &value) {
    auto *dispatch = object.AsObjectNoAddRef();
    dispatch->PropSetByNum(TJS_MEMBERENSURE, index, &value, dispatch);
}

struct Tag {
    tTJSVariant root, attribute, parameter, command;
    tjs_int commandCount = 0;

    void clear() {
        root.Clear();
        attribute.Clear();
        parameter.Clear();
        command.Clear();
        commandCount = 0;
    }

    void ensure() {
        if(root.Type() != tvtObject || !root.AsObjectNoAddRef())
            root = dictionary();
    }

    void name(const tTJSVariant &value) {
        ensure();
        set(root, TJS_W("tag"), value);
    }

    void property(const tjs_char *key, const tTJSVariant &value) {
        ensure();
        set(root, key, value);
    }

    void argument(const ttstr &key, const tTJSVariant &value,
                  bool isParameter = false) {
        ensure();
        auto &target = isParameter ? parameter : attribute;
        if(target.Type() != tvtObject || !target.AsObjectNoAddRef()) {
            target = dictionary();
            set(root, isParameter ? TJS_W("parameter") : TJS_W("attribute"),
                target);
        }
        set(target, key.c_str(), value);
    }

    void addCommand(const tTJSVariant &value) {
        ensure();
        if(command.Type() != tvtObject || !command.AsObjectNoAddRef()) {
            command = array();
            set(root, TJS_W("command"), command);
        }
        append(command, commandCount++, value);
    }
};

struct Token {
    int type = End;
    int value = 0;
};

struct ParseError {
    ttstr message;
};

class ScenarioParser {
    ttstr source, line, defaultTag, firstError;
    std::vector<tjs_int> lineOffsets, lineLengths;
    std::vector<tTJSVariant> values;
    std::deque<Token> tokens;
    std::deque<tTJSVariant> placeholders;
    const tjs_char *cursor = nullptr;
    tjs_int previousOffset = 0, currentLine = 0;
    Tag tag;
    tTJSVariant lines, text;
    tjs_int textCount = 0;
    bool lineTag = false, continuation = false, selection = false;
    bool style = false;

    [[noreturn]] void error(const tjs_char *message) {
        throw ParseError{ ttstr(TJS_W("MDKParser line ")) +
                          ttstr(currentLine + 1) + TJS_W(": ") + message };
    }

    static bool space(tjs_char ch) {
        return ch < 256 && std::isspace(static_cast<unsigned char>(ch));
    }

    static bool identifier(tjs_char ch) {
        return ch > 255 || ch == TJS_W('_') ||
            (ch < 256 && std::isalnum(static_cast<unsigned char>(ch)));
    }

    void advance() {
        if(*cursor)
            ++cursor;
        while(*cursor == 0xffff)
            ++cursor;
    }

    void skipSpace() {
        while(*cursor == 0xffff || space(*cursor))
            ++cursor;
    }

    int value(const tTJSVariant &item) {
        values.push_back(item);
        return static_cast<int>(values.size() - 1);
    }

    Token literal(int type, const tTJSVariant &item) {
        return { type, value(item) };
    }

    bool take(const tjs_char *word) {
        auto *saved = cursor;
        for(; *word; ++word) {
            if(*cursor != *word) {
                cursor = saved;
                return false;
            }
            advance();
        }
        return true;
    }

    int until(tjs_char delimiter, bool required = false) {
        if(!*cursor)
            return -1;
        ttstr result;
        while(*cursor && *cursor != delimiter)
            result += *cursor++;
        if(*cursor)
            ++cursor;
        else if(required)
            return -1;
        return result.IsEmpty() ? -1 : value(tTJSVariant(result));
    }

    Token readText() {
        if(!tokens.empty()) {
            auto token = tokens.front();
            tokens.pop_front();
            return token;
        }
        if(!*cursor)
            return {};
        previousOffset = static_cast<tjs_int>(cursor - line.c_str());
        ttstr result;
        for(;;) {
            auto ch = *cursor;
            int type = 0, length = 1;
            if(ch == TJS_W('\\')) {
                ++cursor;
                if(!*cursor)
                    break;
                result += *cursor++;
                continue;
            }
            switch(ch) {
                case 0: break;
                case TJS_W('['): type = OpenTag; break;
                case TJS_W('|'): type = RubyStart; break;
                case TJS_W('>'): type = LineBreak; break;
                case 0x300a: type = Ruby; break;
                case 0x300b: type = RubyEnd; break;
                case TJS_W('{'): type = TextStyle; break;
                case TJS_W(':'):
                    type = cursor[1] == TJS_W('(') ? InlineImage : Colon;
                    if(type == InlineImage)
                        length = 2;
                    break;
                default: result += ch; ++cursor; continue;
            }
            if(!result.IsEmpty())
                return literal(Text, tTJSVariant(result));
            if(!ch)
                return {};
            cursor += length;
            return { type, 0 };
        }
        return result.IsEmpty() ? Token{} : literal(Text, tTJSVariant(result));
    }

    Token readStart() {
        if(!*cursor)
            return {};
        previousOffset = static_cast<tjs_int>(cursor - line.c_str());
        switch(*cursor) {
            case TJS_W('#'): advance(); return { Label, 0 };
            case TJS_W('@'): advance(); return { Speaker, 0 };
            case TJS_W('/'):
                if(cursor[1] == TJS_W('/')) {
                    cursor += 2;
                    return { Comment, 0 };
                }
                break;
            case TJS_W('>'):
                if(take(TJS_W(">>>"))) return { BeginTrans, 0 };
                advance();
                return { Next, 0 };
            case TJS_W('<'):
                if(take(TJS_W("<<<"))) return { EndTrans, 0 };
                if(take(TJS_W("<="))) return { DefaultTag, 0 };
                break;
            case TJS_W('='):
                if(cursor[1] == TJS_W('>')) {
                    cursor += 2;
                    return { ClearDefault, 0 };
                }
                break;
            default:
                if(*cursor >= TJS_W('0') && *cursor <= TJS_W('9')) {
                    auto *next = cursor;
                    tjs_uint number = 0;
                    while(*next >= TJS_W('0') && *next <= TJS_W('9'))
                        number = number * 10 + *next++ - TJS_W('0');
                    if(*next == TJS_W('.')) {
                        cursor = next + 1;
                        return { Selection, static_cast<int>(number) };
                    }
                }
                break;
        }
        return readText();
    }

    Token readOctet() {
        cursor += 2;
        std::vector<tjs_uint8> bytes;
        bool leading = true;
        tjs_uint8 current = 0;
        while(*cursor) {
            if(cursor[0] == TJS_W('%') && cursor[1] == TJS_W('>')) {
                cursor += 2;
                if(!leading)
                    bytes.push_back(current);
                return literal(Octet, tTJSVariant(bytes.data(), bytes.size()));
            }
            if(cursor[0] == TJS_W('/') && cursor[1] == TJS_W('/')) {
                while(*cursor) ++cursor;
                break;
            }
            if(cursor[0] == TJS_W('/') && cursor[1] == TJS_W('*')) {
                cursor += 2;
                int depth = 1;
                while(*cursor && depth) {
                    if(cursor[0] == TJS_W('/') && cursor[1] == TJS_W('*')) {
                        ++depth; cursor += 2;
                    } else if(cursor[0] == TJS_W('*') && cursor[1] == TJS_W('/')) {
                        --depth; cursor += 2;
                    } else advance();
                }
                if(depth) break;
                continue;
            }
            int digit = TJSHexNum(*cursor);
            if(digit >= 0) {
                if(leading) { current = digit; leading = false; }
                else {
                    current = (current << 4) + digit;
                    bytes.push_back(current);
                    leading = true;
                }
            }
            if(!leading && *cursor == TJS_W(',')) {
                bytes.push_back(current);
                leading = true;
            }
            advance();
        }
        error(TJS_W("Unterminated octet literal"));
    }

    Token readTagToken() {
        if(!tokens.empty()) {
            auto token = tokens.front();
            tokens.pop_front();
            return token;
        }
        previousOffset = static_cast<tjs_int>(cursor - line.c_str());
        skipSpace();
        if(!*cursor)
            return {};
        const auto ch = *cursor;
        if(ch == TJS_W('\'') || ch == TJS_W('"')) {
            tTJSVariant result;
            if(!TJSParseString(result, &cursor))
                error(TJS_W("Unterminated string literal"));
            return literal(ch == TJS_W('\'') ? Voice : Storage, result);
        }
        if((ch >= TJS_W('0') && ch <= TJS_W('9')) ||
           (ch == TJS_W('.') && cursor[1] >= TJS_W('0') &&
            cursor[1] <= TJS_W('9'))) {
            tTJSVariant result;
            if(!TJSParseNumber(result, &cursor))
                error(TJS_W("Cannot be parsed as a number"));
            return literal(Number, result);
        }
        if(ch == TJS_W('<') && cursor[1] == TJS_W('%'))
            return readOctet();
        if(ch == TJS_W(':') && cursor[1] == TJS_W(':')) {
            cursor += 2;
            return { Scope, 0 };
        }
        static const std::map<tjs_char, int> punctuation = {
            { TJS_W('!'), Sync }, { TJS_W('#'), Hash },
            { TJS_W('$'), Dollar }, { TJS_W('%'), Percent },
            { TJS_W('&'), NoWait }, { TJS_W('('), LeftParen },
            { TJS_W(')'), RightParen }, { TJS_W('*'), Star },
            { TJS_W('+'), Plus }, { TJS_W(','), Comma },
            { TJS_W('-'), Minus }, { TJS_W('.'), Dot },
            { TJS_W('/'), Slash }, { TJS_W(':'), Colon },
            { TJS_W(';'), Semicolon }, { TJS_W('<'), Less },
            { TJS_W('='), Equal }, { TJS_W('>'), Greater },
            { TJS_W('?'), Question }, { TJS_W('@'), Speaker },
            { TJS_W('['), LeftBracket }, { TJS_W('\\'), Backslash },
            { TJS_W(']'), RightBracket }, { TJS_W('^'), Caret },
            { TJS_W('{'), LeftBrace }, { TJS_W('|'), RubyStart },
            { TJS_W('}'), RightBrace }, { TJS_W('~'), Tilde }
        };
        auto punctuationToken = punctuation.find(ch);
        if(punctuationToken != punctuation.end()) {
            advance();
            return { punctuationToken->second, 0 };
        }
        if(!identifier(ch) || (ch >= TJS_W('0') && ch <= TJS_W('9')))
            error(TJS_W("Invalid character in tag"));
        const auto *start = cursor;
        while(*cursor && identifier(*cursor))
            ++cursor;
        ttstr name(start, static_cast<tjs_int>(cursor - start));
        if(name == TJS_W("true")) return literal(Number, tTJSVariant(1));
        if(name == TJS_W("false")) return literal(Number, tTJSVariant(0));
        if(name == TJS_W("null"))
            return literal(53, tTJSVariant(static_cast<iTJSDispatch2 *>(nullptr)));
        if(name == TJS_W("void")) return literal(Void, tTJSVariant());
        if(name == TJS_W("NaN"))
            return literal(Number, tTJSVariant(std::numeric_limits<double>::quiet_NaN()));
        if(name == TJS_W("Infinity"))
            return literal(Number, tTJSVariant(std::numeric_limits<double>::infinity()));
        return literal(Identifier, tTJSVariant(name));
    }

    void addLine(const tTJSVariant &item) {
        append(lines, currentLine, item);
    }

    void addText(const tTJSVariant &item) {
        if(text.Type() != tvtObject || !text.AsObjectNoAddRef()) {
            text = array();
            addLine(text);
        }
        append(text, textCount++, item);
    }

    void emitTag() {
        if(tag.root.Type() == tvtObject && tag.root.AsObjectNoAddRef())
            addText(tag.root);
        tag.clear();
    }

    void standalone() {
        if(tag.root.Type() == tvtObject && tag.root.AsObjectNoAddRef())
            addLine(tag.root);
        tag.clear();
    }

    bool shortcut(Token token) {
        const tjs_char *name = nullptr;
        int close = 0;
        switch(token.type) {
            case Voice: name = TJS_W("voice"); break;
            case Storage: name = TJS_W("storage"); break;
            case Less: name = TJS_W("time"); close = Greater; break;
            case LeftParen: name = TJS_W("fade"); close = RightParen; break;
            case LeftBrace: name = TJS_W("wait"); close = RightBrace; break;
            default: return false;
        }
        if(close) {
            token = readTagToken();
            if(token.type != Number)
                error(TJS_W("Expected numeric shortcut value"));
        }
        tag.argument(ttstr(name), values[token.value]);
        if(close && readTagToken().type != close)
            error(TJS_W("Missing shortcut delimiter"));
        return true;
    }

    ttstr referencePath(ttstr result) {
        for(;;) {
            auto token = readTagToken();
            if(token.type != Dot) {
                tokens.push_back(token);
                return result;
            }
            token = readTagToken();
            if(token.type != Identifier)
                error(TJS_W("Expected identifier after '.'"));
            result += TJS_W(".");
            result += ttstr(values[token.value]);
        }
    }

    tTJSVariant reference(const tTJSVariant &first) {
        ttstr path(first);
        auto token = readTagToken();
        if(token.type == Dot) {
            auto next = readTagToken();
            if(next.type != Identifier)
                error(TJS_W("Expected identifier after '.'"));
            path += TJS_W(".");
            path += ttstr(values[next.value]);
            path = referencePath(path);
            token = readTagToken();
        }
        auto result = dictionary();
        if(token.type == Scope) {
            auto next = readTagToken();
            if(next.type != Identifier)
                error(TJS_W("Expected property after '::'"));
            const ttstr property = referencePath(ttstr(values[next.value]));
            set(result, TJS_W("file"), tTJSVariant(path));
            set(result, TJS_W("prop"), tTJSVariant(property));
        } else {
            tokens.push_back(token);
            set(result, TJS_W("ref"), tTJSVariant(path));
        }
        return result;
    }

    void argument(const Token &name, bool parameter) {
        auto token = readTagToken();
        if(token.type != Equal) {
            if(parameter)
                tag.argument(ttstr(values[name.value]),
                    tTJSVariant(static_cast<iTJSDispatch2 *>(nullptr)), true);
            else
                tag.addCommand(values[name.value]);
            tokens.push_back(token);
            return;
        }
        token = readTagToken();
        tTJSVariant item;
        switch(token.type) {
            case Void: case Voice: case Storage: case Number: case Octet:
                item = values[token.value];
                break;
            case Plus:
            case Minus: {
                const bool negative = token.type == Minus;
                token = readTagToken();
                if(token.type != Number)
                    error(TJS_W("Expected number after sign"));
                item = values[token.value];
                if(negative)
                    item.changesign();
                break;
            }
            case Identifier: item = reference(values[token.value]); break;
            default: return;
        }
        tag.argument(ttstr(values[name.value]), item, parameter);
    }

    void attributes() {
        for(;;) {
            auto token = readTagToken();
            if(token.type == End) {
                if(lineTag)
                    lineTag = false;
                else
                    continuation = true;
                return;
            }
            if(token.type == RightBracket) {
                if(style)
                    error(TJS_W("Expected '}' to close text style"));
                continuation = false;
                return;
            }
            if(token.type == RightBrace && style) {
                continuation = false;
                return;
            }
            if(token.type == Identifier) {
                argument(token, false);
            } else if(token.type == Dollar) {
                token = readTagToken();
                if(token.type != Identifier)
                    error(TJS_W("Expected parameter name"));
                argument(token, true);
            } else if(!shortcut(token)) {
                error(TJS_W("Unexpected token in tag attributes"));
            }
        }
    }

    const tjs_char *command(int type) {
        switch(type) {
            case Plus: return TJS_W("add");
            case Minus: return TJS_W("del");
            case Star: return TJS_W("all");
            case Sync: return TJS_W("sync");
            case NoWait: return TJS_W("nowait");
            default: return nullptr;
        }
    }

    void readTag() {
        if(!continuation)
            tag.clear();
        if(!contains(tag.root, TJS_W("tag"))) {
            if(!defaultTag.IsEmpty()) {
                tag.name(tTJSVariant(defaultTag));
            } else {
                for(;;) {
                    auto token = readTagToken();
                    if(token.type == End) {
                        if(!continuation) {
                            if(lineTag) lineTag = false;
                            else continuation = true;
                            tag.ensure();
                            addText(tag.root);
                        }
                        return;
                    }
                    if(token.type == RightBracket) {
                        if(continuation) continuation = false;
                        else if(tag.root.Type() == tvtObject) emitTag();
                        tag.clear();
                        return;
                    }
                    if(token.type == Identifier) {
                        tag.name(values[token.value]);
                        break;
                    }
                    if(shortcut(token)) break;
                    if(const auto *name = command(token.type)) {
                        tag.addCommand(tTJSVariant(name));
                        continue;
                    }
                }
            }
        }
        const bool wasContinuation = continuation;
        attributes();
        if(wasContinuation) {
            if(!continuation) tag.clear();
        } else if(continuation) {
            if(tag.root.Type() == tvtObject) addText(tag.root);
        } else emitTag();
    }

    void readInline(Token token) {
        switch(token.type) {
            case OpenTag: readTag(); break;
            case Text: addText(values[token.value]); break;
            case RubyStart: {
                auto placeholder = dictionary();
                placeholders.push_back(placeholder);
                addText(placeholder);
                break;
            }
            case LineBreak: {
                Tag item;
                item.name(tTJSVariant(TJS_W("l")));
                addText(item.root);
                break;
            }
            case Ruby:
            case RubyEnd:
            case TextStyle: {
                if(placeholders.empty())
                    error(TJS_W("Ruby or style has no preceding '|'"));
                Tag item;
                item.root = placeholders.back();
                placeholders.pop_back();
                if(token.type != TextStyle) {
                    item.name(tTJSVariant(TJS_W("ruby")));
                    if(token.type == Ruby) {
                        int reading = until(0x300b, true);
                        if(reading < 0)
                            error(TJS_W("Unterminated ruby"));
                        item.argument(TJS_W("text"), values[reading]);
                    }
                    Tag end;
                    end.name(tTJSVariant(TJS_W("endruby")));
                    addText(end.root);
                } else {
                    auto previous = tag;
                    tag = item;
                    tag.name(tTJSVariant(TJS_W("textstyle")));
                    style = true;
                    attributes();
                    style = false;
                    tag.clear();
                    tag = previous;
                    Tag end;
                    end.name(tTJSVariant(TJS_W("endtextstyle")));
                    addText(end.root);
                }
                break;
            }
            case InlineImage:
            case Colon: {
                int storage = until(token.type == InlineImage ? TJS_W(')') :
                                     TJS_W(':'), true);
                if(storage < 0)
                    error(TJS_W("Unterminated inline image or emoji"));
                Tag item;
                item.name(tTJSVariant(token.type == InlineImage ?
                    TJS_W("inlineimage") : TJS_W("emoji")));
                item.argument(TJS_W("storage"), values[storage]);
                addText(item.root);
                break;
            }
            default: error(TJS_W("Unexpected text token"));
        }
    }

    void readLabel() {
        tag.clear();
        tag.name(tTJSVariant(TJS_W("label")));
        auto token = readTagToken();
        if(token.type == Identifier) {
            tag.property(TJS_W("name"), values[token.value]);
            token = readTagToken();
        } else if(token.type != RubyStart) {
            error(TJS_W("Expected label name"));
        }
        if(token.type == RubyStart && *cursor)
            tag.property(TJS_W("description"), tTJSVariant(ttstr(cursor)));
        standalone();
    }

    static bool nameToken(const Token &token) {
        return token.type == Identifier || token.type == Voice ||
            token.type == Storage;
    }

    void readSpeaker() {
        tag.clear();
        tag.name(tTJSVariant(TJS_W("charname")));
        auto token = readTagToken();
        if(!nameToken(token)) error(TJS_W("Expected speaker name"));
        tag.argument(TJS_W("name"), values[token.value]);
        token = readTagToken();
        if(token.type == Slash) {
            token = readTagToken();
            if(nameToken(token)) tag.argument(TJS_W("alias"), values[token.value]);
            else {
                tag.argument(TJS_W("alias"), tTJSVariant());
                tokens.push_back(token);
            }
        } else tokens.push_back(token);
        lineTag = true;
        attributes();
        standalone();
    }

    void readTransition() {
        tag.clear();
        tag.name(tTJSVariant(TJS_W("endtrans")));
        lineTag = true;
        auto token = readTagToken();
        if(token.type == Identifier) {
            auto next = readTagToken();
            if(next.type != Equal)
                tag.argument(TJS_W("trans"), values[token.value]);
            tokens.push_back(next);
        } else tokens.push_back(token);
        attributes();
        standalone();
    }

    static ttstr trim(const ttstr &input) {
        const auto trimCharacter = [](tjs_char ch) {
            return (ch >= 1 && ch <= 32) || ch == 127;
        };
        tjs_int first = 0, last = input.length();
        while(first < last && trimCharacter(input[first])) ++first;
        if(first == last)
            return input;
        while(last > first && trimCharacter(input[last - 1])) --last;
        return ttstr(input.c_str() + first, last - first);
    }

    void readSelection(int number) {
        tag.clear();
        tag.name(tTJSVariant(TJS_W("select")));
        tag.property(TJS_W("number"), tTJSVariant(number));
        auto token = readTagToken();
        if(token.type == Star) {
            tag.argument(TJS_W("text"),
                         tTJSVariant(static_cast<iTJSDispatch2 *>(nullptr)));
            if(readTagToken().type != RubyStart)
                error(TJS_W("Expected selection separator"));
        } else if(token.type == RubyStart) {
            int image = until(TJS_W('|'));
            if(image >= 0)
                tag.argument(TJS_W("image"), tTJSVariant(trim(ttstr(values[image]))));
        } else {
            cursor = line.c_str() + previousOffset;
            int caption = until(TJS_W('|'));
            if(caption >= 0) tag.argument(TJS_W("text"), values[caption]);
        }
        int target = until(TJS_W('|'));
        if(target >= 0)
            tag.argument(TJS_W("target"), tTJSVariant(trim(ttstr(values[target]))));
        lineTag = true;
        attributes();
        standalone();
    }

    void readNext() {
        tag.clear();
        tag.name(tTJSVariant(TJS_W("next")));
        skipSpace();
        int target = until(TJS_W(' '));
        if(target >= 0) tag.argument(TJS_W("target"), values[target]);
        skipSpace();
        int condition = until(TJS_W(' '));
        if(condition >= 0) {
            if(ttstr(values[condition]) != TJS_W("if"))
                error(TJS_W("Expected 'if' after jump target"));
            skipSpace();
            if(!*cursor) error(TJS_W("Expected jump condition"));
            tag.argument(TJS_W("cond"), tTJSVariant(ttstr(cursor)));
        }
        standalone();
    }

    void parseLine() {
        placeholders.clear();
        lineTag = style = false;
        if(!continuation) tag.clear();
        if(line.IsEmpty()) {
            if(continuation) addLine(tTJSVariant());
            else if(selection) {
                selection = false;
                tag.name(tTJSVariant(TJS_W("selopt")));
                standalone();
            } else addLine(tTJSVariant(0));
            return;
        }
        if(continuation) {
            readTag();
            if(continuation) { addLine(tTJSVariant()); return; }
            auto token = readText();
            if(!token.type) { addLine(tTJSVariant()); return; }
            do {
                readInline(token);
                token = readText();
            } while(token.type);
            return;
        }
        auto token = readStart();
        if(selection && token.type != Selection) {
            selection = false;
            tag.name(tTJSVariant(TJS_W("selopt")));
            cursor = line.c_str() + previousOffset;
            lineTag = true;
            attributes();
            standalone();
            return;
        }
        switch(token.type) {
            case End: addLine(tTJSVariant(0)); return;
            case Comment: addLine(tTJSVariant()); return;
            case Label: readLabel(); return;
            case Speaker: readSpeaker(); return;
            case EndTrans: readTransition(); return;
            case BeginTrans:
                tag.name(tTJSVariant(TJS_W("begintrans")));
                standalone(); return;
            case Next: readNext(); return;
            case Selection:
                readSelection(token.value); selection = true; return;
            case DefaultTag: {
                defaultTag.Clear();
                int name = until(TJS_W(' '));
                if(name >= 0) defaultTag = ttstr(values[name]);
                addLine(tTJSVariant()); return;
            }
            case ClearDefault:
                defaultTag.Clear(); addLine(tTJSVariant()); return;
            default:
                do {
                    readInline(token);
                    token = readText();
                } while(token.type);
        }
    }

public:
    tTJSVariant parse(const ttstr &input) {
        if(input.IsEmpty())
            return tTJSVariant(static_cast<iTJSDispatch2 *>(nullptr));
        source = input;
        const auto *begin = source.c_str();
        tjs_int start = 0, position = 0;
        while(begin[position]) {
            if(begin[position] == TJS_W('\r') || begin[position] == TJS_W('\n')) {
                lineOffsets.push_back(start);
                lineLengths.push_back(position - start);
                if(begin[position] == TJS_W('\r') && begin[position + 1] == TJS_W('\n'))
                    ++position;
                start = position + 1;
            }
            ++position;
        }
        if(start != position) {
            lineOffsets.push_back(start);
            lineLengths.push_back(position - start);
        }
        lines = array();
        for(currentLine = 0; currentLine < static_cast<tjs_int>(lineOffsets.size());
            ++currentLine) {
            line = ttstr(begin + lineOffsets[currentLine], lineLengths[currentLine]);
            cursor = line.c_str();
            previousOffset = 0;
            tokens.clear();
            values.clear();
            values.emplace_back();
            text.Clear();
            textCount = 0;
            try { parseLine(); }
            catch(const ParseError &failure) {
                if(firstError.IsEmpty()) firstError = failure.message;
                addLine(tTJSVariant());
            }
        }
        if(continuation && firstError.IsEmpty())
            firstError = TJS_W("MDKParser: unterminated tag");
        if(!firstError.IsEmpty())
            TJS_eTJSError(firstError.c_str());
        auto result = dictionary();
        set(result, TJS_W("lines"), lines);
        return result;
    }
};

} // namespace

tTJSVariant Parser::parse(const ttstr &source) {
    return ScenarioParser().parse(source);
}

} // namespace mdk

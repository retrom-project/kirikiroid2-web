#include <cassert>
#include <iostream>
#include <stdexcept>
#include "tjsVariant.h"

// Only scalar variants cross this constructor seam. Use the production
// variant constructors and conversion overloads; replace error reporting and
// scalar cleanup rather than simulating TJS conversion resolution.
namespace TJS {
tTJSVariant::~tTJSVariant() {
    assert(Type() == tvtVoid || Type() == tvtInteger || Type() == tvtReal);
}
void TJSSetFPUE() {}
void TJSThrowVariantConvertError(const tTJSVariant &, tTJSVariantType to) {
    throw std::runtime_error(to == tvtObject ? "Cannot convert integer readonly to Object" : "unexpected conversion");
}
tTVInteger tTJSVariantString::ToInteger() const {
    throw std::runtime_error("string conversion outside this scalar constructor test");
}
}

using namespace TJS;
#include "sqlite-constructor-readonly.h"

int main() {
    try {
        tTJSVariant yes(tjs_int(1)), no(tjs_int(0)), negative(tjs_int(-1)), real(0.5), empty;
        tTJSVariant *arguments[] = {nullptr, &yes};
        assert(constructorReadonly(2, arguments));
        arguments[1] = &no; assert(!constructorReadonly(2, arguments));
        arguments[1] = &negative; assert(constructorReadonly(2, arguments));
        arguments[1] = &real; assert(constructorReadonly(2, arguments));
        arguments[1] = &empty; assert(!constructorReadonly(2, arguments));
        assert(!constructorReadonly(1, nullptr));
    } catch(const std::exception &error) {
        std::cerr << "Sqlite factory must accept actual TJS scalar readonly: " << error.what() << '\n';
        return 1;
    }
    std::cout << "Actual TJS variant conversion in Sqlite constructor passed\n";
}

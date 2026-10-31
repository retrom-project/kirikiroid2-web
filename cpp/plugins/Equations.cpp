#define NCB_MODULE_NAME TJS_W("Equations.dll")
#include "tjsCommHead.h"
#include "ncbind.hpp"
#include "DebugIntf.h"

#include <cmath>


class Equations {
    using Function_guess = double (Equations::*)(double, double, double, double);
    Function_guess functions_guess[256];
    tjs_int functionCount_guess;

    static double power_guess(double value_guess, unsigned exponent_guess) {
        double result_guess = 1.0;
        for(;;) {
            if(exponent_guess & 1u)
                result_guess = result_guess * value_guess;
            exponent_guess >>= 1;
            if(!exponent_guess)
                break;
            value_guess = value_guess * value_guess;
        }
        return result_guess;
    }

public:
    Equations() {
        functions_guess[0] = &Equations::easeNone;
        functions_guess[1] = &Equations::easeNone;
        functions_guess[2] = &Equations::easeInQuad;
        functions_guess[3] = &Equations::easeOutQuad;
        functions_guess[4] = &Equations::easeInOutQuad;
        functions_guess[5] = &Equations::easeOutInQuad;
        functions_guess[6] = &Equations::easeInCubic;
        functions_guess[7] = &Equations::easeOutCubic;
        functions_guess[8] = &Equations::easeInOutCubic;
        functions_guess[9] = &Equations::easeOutInCubic;
        functions_guess[10] = &Equations::easeInQuart;
        functions_guess[11] = &Equations::easeOutQuart;
        functions_guess[12] = &Equations::easeInOutQuart;
        functions_guess[13] = &Equations::easeOutInQuart;
        functions_guess[14] = &Equations::easeInQuint;
        functions_guess[15] = &Equations::easeOutQuint;
        functions_guess[16] = &Equations::easeInOutQuint;
        functions_guess[17] = &Equations::easeOutInQuint;
        functions_guess[18] = &Equations::easeInSine;
        functions_guess[19] = &Equations::easeOutSine;
        functions_guess[20] = &Equations::easeInOutSine;
        functions_guess[21] = &Equations::easeOutInSine;
        functions_guess[22] = &Equations::easeInCirc;
        functions_guess[23] = &Equations::easeOutCirc;
        functions_guess[24] = &Equations::easeInOutCirc;
        functions_guess[25] = &Equations::easeOutInCirc;
        functions_guess[26] = &Equations::easeInExpo;
        functions_guess[27] = &Equations::easeOutExpo;
        functions_guess[28] = &Equations::easeInOutExpo;
        functions_guess[29] = &Equations::easeOutInExpo;
        functions_guess[30] = &Equations::easeInElastic;
        functions_guess[31] = &Equations::easeOutElastic;
        functions_guess[32] = &Equations::easeInOutElastic;
        functions_guess[33] = &Equations::easeOutInElastic;
        functions_guess[34] = &Equations::easeInBack;
        functions_guess[35] = &Equations::easeOutBack;
        functions_guess[36] = &Equations::easeInOutBack;
        functions_guess[37] = &Equations::easeOutInBack;
        functions_guess[38] = &Equations::easeInBounce;
        functions_guess[39] = &Equations::easeOutBounce;
        functions_guess[40] = &Equations::easeInOutBounce;
        functions_guess[41] = &Equations::easeOutInBounce;
        functionCount_guess = 42;
    }

    double calc(tjs_int index_guess, double t_guess, double b_guess,
                double c_guess, double d_guess) {
        if(index_guess < 0 || index_guess >= functionCount_guess)
            index_guess = 0;
        return (this->*functions_guess[index_guess])(t_guess, b_guess, c_guess, d_guess);
    }

    double easeNone(double t_guess, double b_guess, double c_guess, double d_guess) {
        return t_guess * c_guess / d_guess + b_guess;
    }
    double easeInQuad(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double x_guess = t_guess / d_guess;
        return x_guess * (c_guess * x_guess) + b_guess;
    }
    double easeOutQuad(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double x_guess = t_guess / d_guess;
        return b_guess - c_guess * x_guess * (x_guess - 2.0);
    }
    double easeInOutQuad(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double x_guess = t_guess / (d_guess * 0.5);
        if(x_guess >= 1.0)
            return b_guess - ((x_guess - 1.0) * (x_guess - 1.0 - 2.0) - 1.0) * (0.5 * c_guess);
        return power_guess(x_guess, 2) * (0.5 * c_guess) + b_guess;
    }
    double easeOutInQuad(double t_guess, double b_guess, double c_guess, double d_guess) {
        if(t_guess >= d_guess * 0.5) {
            const double x_guess = (t_guess + t_guess - d_guess) / d_guess;
            return x_guess * (0.5 * c_guess * x_guess) + 0.5 * c_guess + b_guess;
        }
        const double x_guess = t_guess * 2.0 / d_guess;
        return b_guess - 0.5 * c_guess * x_guess * (x_guess - 2.0);
    }
    double easeInCubic(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double x_guess = t_guess / d_guess;
        return power_guess(x_guess, 2) * (x_guess * c_guess) + b_guess;
    }
    double easeOutCubic(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double x_guess = t_guess / d_guess - 1.0;
        return (x_guess * power_guess(x_guess, 2) + 1.0) * c_guess + b_guess;
    }
    double easeInOutCubic(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double x_guess = t_guess / (d_guess * 0.5);
        if(x_guess >= 1.0) {
            const double shifted_guess = x_guess - 2.0;
            return (shifted_guess * power_guess(shifted_guess, 2) + 2.0) * (0.5 * c_guess) + b_guess;
        }
        return power_guess(x_guess, 3) * (0.5 * c_guess) + b_guess;
    }
    double easeOutInCubic(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double doubled_guess = t_guess + t_guess;
        if(t_guess >= d_guess * 0.5) {
            const double x_guess = (doubled_guess - d_guess) / d_guess;
            return x_guess * (0.5 * c_guess) * power_guess(x_guess, 2) + 0.5 * c_guess + b_guess;
        }
        const double x_guess = doubled_guess / d_guess - 1.0;
        return (x_guess * power_guess(x_guess, 2) + 1.0) * (0.5 * c_guess) + b_guess;
    }
    double easeInQuart(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double x_guess = t_guess / d_guess;
        return power_guess(x_guess, 3) * (x_guess * c_guess) + b_guess;
    }
    double easeOutQuart(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double x_guess = t_guess / d_guess - 1.0;
        return b_guess - (x_guess * power_guess(x_guess, 3) - 1.0) * c_guess;
    }
    double easeInOutQuart(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double x_guess = t_guess / (d_guess * 0.5);
        if(x_guess >= 1.0) {
            const double shifted_guess = x_guess - 2.0;
            return b_guess - (shifted_guess * power_guess(shifted_guess, 3) - 2.0) * (0.5 * c_guess);
        }
        return power_guess(x_guess, 4) * (0.5 * c_guess) + b_guess;
    }
    double easeOutInQuart(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double doubled_guess = t_guess + t_guess;
        if(t_guess >= d_guess * 0.5) {
            const double x_guess = (doubled_guess - d_guess) / d_guess;
            return x_guess * (0.5 * c_guess) * power_guess(x_guess, 3) + 0.5 * c_guess + b_guess;
        }
        const double x_guess = doubled_guess / d_guess - 1.0;
        return b_guess - (x_guess * power_guess(x_guess, 3) - 1.0) * (0.5 * c_guess);
    }
    double easeInQuint(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double x_guess = t_guess / d_guess;
        return power_guess(x_guess, 4) * (x_guess * c_guess) + b_guess;
    }
    double easeOutQuint(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double x_guess = t_guess / d_guess - 1.0;
        return (x_guess * power_guess(x_guess, 4) + 1.0) * c_guess + b_guess;
    }
    double easeInOutQuint(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double x_guess = t_guess / (d_guess * 0.5);
        if(x_guess >= 1.0) {
            const double shifted_guess = x_guess - 2.0;
            return (shifted_guess * power_guess(shifted_guess, 4) + 2.0) * (0.5 * c_guess) + b_guess;
        }
        return power_guess(x_guess, 5) * (0.5 * c_guess) + b_guess;
    }
    double easeOutInQuint(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double doubled_guess = t_guess + t_guess;
        if(t_guess >= d_guess * 0.5) {
            const double x_guess = (doubled_guess - d_guess) / d_guess;
            return x_guess * (0.5 * c_guess) * power_guess(x_guess, 4) + 0.5 * c_guess + b_guess;
        }
        const double x_guess = doubled_guess / d_guess - 1.0;
        return (x_guess * power_guess(x_guess, 4) + 1.0) * (0.5 * c_guess) + b_guess;
    }
    double easeInSine(double t_guess, double b_guess, double c_guess, double d_guess) {
        return c_guess - std::cos(t_guess / d_guess * 1.5707963267948966) * c_guess + b_guess;
    }
    double easeOutSine(double t_guess, double b_guess, double c_guess, double d_guess) {
        return std::sin(t_guess / d_guess * 1.5707963267948966) * c_guess + b_guess;
    }
    double easeInOutSine(double t_guess, double b_guess, double c_guess, double d_guess) {
        return b_guess - (std::cos(t_guess * 3.141592653589793 / d_guess) - 1.0) * (c_guess * 0.5);
    }
    double easeOutInSine(double t_guess, double b_guess, double c_guess, double d_guess) {
        if(t_guess >= d_guess * 0.5)
            return 0.5 * c_guess - std::cos((t_guess + t_guess - d_guess) / d_guess * 1.5707963267948966) * (0.5 * c_guess) + 0.5 * c_guess + b_guess;
        return std::sin((t_guess + t_guess) / d_guess * 1.5707963267948966) * (c_guess * 0.5) + b_guess;
    }
    double easeInCirc(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double x_guess = t_guess / d_guess;
        return b_guess - (std::sqrt(1.0 - x_guess * x_guess) - 1.0) * c_guess;
    }
    double easeOutCirc(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double x_guess = t_guess / d_guess - 1.0;
        return std::sqrt(1.0 - x_guess * x_guess) * c_guess + b_guess;
    }
    double easeInOutCirc(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double x_guess = t_guess / (d_guess * 0.5);
        if(x_guess >= 1.0) {
            const double shifted_guess = x_guess - 2.0;
            return (std::sqrt(1.0 - shifted_guess * shifted_guess) + 1.0) * (c_guess * 0.5) + b_guess;
        }
        return b_guess - (std::sqrt(1.0 - power_guess(x_guess, 2)) - 1.0) * (c_guess * 0.5);
    }
    double easeOutInCirc(double t_guess, double b_guess, double c_guess, double d_guess) {
        if(t_guess >= d_guess * 0.5) {
            const double x_guess = (t_guess + t_guess - d_guess) / d_guess;
            return 0.5 * c_guess + b_guess - (std::sqrt(1.0 - x_guess * x_guess) - 1.0) * (0.5 * c_guess);
        }
        const double x_guess = (t_guess + t_guess) / d_guess - 1.0;
        return std::sqrt(1.0 - x_guess * x_guess) * (c_guess * 0.5) + b_guess;
    }
    double easeInExpo(double t_guess, double b_guess, double c_guess, double d_guess) {
        if(t_guess == 0.0)
            return b_guess;
        return std::pow(2.0, (t_guess / d_guess - 1.0) * 10.0) * c_guess + b_guess - c_guess * 0.001;
    }
    double easeOutExpo(double t_guess, double b_guess, double c_guess, double d_guess) {
        if(t_guess == d_guess)
            return b_guess + c_guess;
        return (1.0 - std::pow(2.0, t_guess * -10.0 / d_guess)) * (c_guess * 1.001) + b_guess;
    }
    double easeInOutExpo(double t_guess, double b_guess, double c_guess, double d_guess) {
        if(t_guess == 0.0)
            return b_guess;
        if(d_guess == t_guess)
            return b_guess + c_guess;
        const double normalized_guess = t_guess / (d_guess * 0.5);
        const double shifted_guess = normalized_guess - 1.0;
        if(normalized_guess >= 1.0)
            return (2.0 - std::pow(2.0, shifted_guess * -10.0)) * (c_guess * 0.5 * 1.0005) + b_guess;
        return std::pow(2.0, shifted_guess * 10.0) * (0.5 * c_guess) + b_guess - c_guess * 0.0005;
    }
    double easeOutInExpo(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double doubled_guess = t_guess * 2.0;
        if(t_guess >= d_guess * 0.5) {
            if(0.0 == doubled_guess - d_guess)
                return 0.5 * c_guess + b_guess;
            return std::pow(2.0, ((doubled_guess - d_guess) / d_guess - 1.0) * 10.0) * (0.5 * c_guess) + 0.5 * c_guess + b_guess - 0.5 * c_guess * 0.001;
        }
        const double half_guess = 0.5 * c_guess;
        if(d_guess == doubled_guess)
            return half_guess + b_guess;
        return (1.0 - std::pow(2.0, doubled_guess * -10.0 / d_guess)) * (half_guess * 1.001) + b_guess;
    }
    double easeInElastic(double t_guess, double b_guess, double c_guess, double d_guess) {
        return elasticIn_guess(t_guess, b_guess, c_guess, d_guess, 0.0, 0.0);
    }
    double easeOutElastic(double t_guess, double b_guess, double c_guess, double d_guess) {
        return elasticOut_guess(t_guess, b_guess, c_guess, d_guess, 0.0, 0.0);
    }
    double easeInOutElastic(double t_guess, double b_guess, double c_guess, double d_guess) {
        return elasticInOut_guess(t_guess, b_guess, c_guess, d_guess, 0.0, 0.0);
    }
    double easeOutInElastic(double t_guess, double b_guess, double c_guess, double d_guess) {
        return elasticOutIn_guess(t_guess, b_guess, c_guess, d_guess, 0.0, 0.0);
    }
    double easeInBack(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double x_guess = t_guess / d_guess;
        return c_guess * x_guess * x_guess * (x_guess * 2.70158 - 1.70158) + b_guess;
    }
    double easeOutBack(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double x_guess = t_guess / d_guess - 1.0;
        return (x_guess * x_guess * (x_guess * 2.70158 + 1.70158) + 1.0) * c_guess + b_guess;
    }
    double easeInOutBack(double t_guess, double b_guess, double c_guess, double d_guess) {
        return backInOut_guess(t_guess, b_guess, c_guess, d_guess, 1.70158);
    }
    double easeOutInBack(double t_guess, double b_guess, double c_guess, double d_guess) {
        return backOutIn_guess(t_guess, b_guess, c_guess, d_guess, 1.70158);
    }
    double easeInBounce(double t_guess, double b_guess, double c_guess, double d_guess) {
        return c_guess - easeOutBounce(d_guess - t_guess, 0.0, c_guess, d_guess) + b_guess;
    }
    double easeOutBounce(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double x_guess = t_guess / d_guess;
        if(x_guess >= 0.36363636363636365) {
            if(x_guess >= 0.7272727272727273) {
                if(x_guess >= 0.9090909090909091) {
                    const double shifted_guess = x_guess - 0.9545454545454546;
                    return (shifted_guess * (7.5625 * shifted_guess) + 0.984375) * c_guess + b_guess;
                }
                const double shifted_guess = x_guess - 0.8181818181818182;
                return (shifted_guess * (7.5625 * shifted_guess) + 0.9375) * c_guess + b_guess;
            }
            const double shifted_guess = x_guess - 0.5454545454545454;
            return (shifted_guess * (7.5625 * shifted_guess) + 0.75) * c_guess + b_guess;
        }
        return power_guess(x_guess, 2) * 7.5625 * c_guess + b_guess;
    }
    double easeInOutBounce(double t_guess, double b_guess, double c_guess, double d_guess) {
        const double doubled_guess = t_guess + t_guess;
        if(t_guess >= 0.5 * d_guess)
            return easeOutBounce(doubled_guess - d_guess, 0.0, c_guess, d_guess) * 0.5 + 0.5 * c_guess + b_guess;
        return (c_guess - easeOutBounce(d_guess - doubled_guess, 0.0, c_guess, d_guess)) * 0.5 + 0.5 * 0.0 + b_guess;
    }
    double easeOutInBounce(double t_guess, double b_guess, double c_guess, double d_guess) {
        if(t_guess >= d_guess * 0.5)
            return 0.5 * c_guess - easeOutBounce(d_guess - (t_guess + t_guess - d_guess), 0.0, 0.5 * c_guess, d_guess) + 0.5 * c_guess + b_guess;
        return easeOutBounce(t_guess + t_guess, b_guess, 0.5 * c_guess, d_guess);
    }

private:
    static double elasticIn_guess(double t_guess, double b_guess, double c_guess,
                                   double d_guess, double amplitude_guess,
                                   double period_guess) {
        if(t_guess == 0.0)
            return b_guess;
        const double normalized_guess = t_guess / d_guess;
        if(1.0 == normalized_guess)
            return b_guess + c_guess;
        if(period_guess == 0.0)
            period_guess = d_guess * 0.3;
        double phase_guess;
        if(amplitude_guess == 0.0 || std::fabs(c_guess) > amplitude_guess) {
            amplitude_guess = c_guess;
            phase_guess = period_guess * 0.25;
        } else {
            phase_guess = std::asin(c_guess / amplitude_guess) * (period_guess / 6.283185307179586);
        }
        return b_guess - std::pow(2.0, (normalized_guess - 1.0) * 10.0) * amplitude_guess *
            std::sin((d_guess * (normalized_guess - 1.0) - phase_guess) * 6.283185307179586 / period_guess);
    }
    static double elasticOut_guess(double t_guess, double b_guess, double c_guess,
                                    double d_guess, double amplitude_guess,
                                    double period_guess) {
        if(t_guess == 0.0)
            return b_guess;
        const double normalized_guess = t_guess / d_guess;
        if(1.0 == normalized_guess)
            return b_guess + c_guess;
        if(period_guess == 0.0)
            period_guess = d_guess * 0.3;
        double phase_guess;
        if(amplitude_guess == 0.0 || std::fabs(c_guess) > amplitude_guess) {
            amplitude_guess = c_guess;
            phase_guess = period_guess * 0.25;
        } else {
            phase_guess = std::asin(c_guess / amplitude_guess) * (period_guess / 6.283185307179586);
        }
        return std::pow(2.0, normalized_guess * -10.0) * amplitude_guess *
            std::sin((d_guess * normalized_guess - phase_guess) * 6.283185307179586 / period_guess) + c_guess + b_guess;
    }
    static double elasticInOut_guess(double t_guess, double b_guess, double c_guess,
                                      double d_guess, double amplitude_guess,
                                      double period_guess) {
        if(t_guess == 0.0)
            return b_guess;
        const double normalized_guess = t_guess / (0.5 * d_guess);
        if(2.0 == normalized_guess)
            return b_guess + c_guess;
        if(period_guess == 0.0)
            period_guess = d_guess * (0.3 * 1.5);
        double phase_guess;
        if(amplitude_guess == 0.0 || std::fabs(c_guess) > amplitude_guess) {
            amplitude_guess = c_guess;
            phase_guess = 0.25 * period_guess;
        } else {
            phase_guess = period_guess / 6.283185307179586 * std::asin(c_guess / amplitude_guess);
        }
        const double shifted_guess = normalized_guess - 1.0;
        const double angle_guess = 6.283185307179586 * (d_guess * shifted_guess - phase_guess) / period_guess;
        if(normalized_guess >= 1.0)
            return std::pow(2.0, shifted_guess * -10.0) * amplitude_guess * std::sin(angle_guess) * 0.5 + c_guess + b_guess;
        return b_guess - std::pow(2.0, shifted_guess * 10.0) * amplitude_guess * std::sin(angle_guess) * 0.5;
    }
    static double elasticOutIn_guess(double t_guess, double b_guess, double c_guess,
                                      double d_guess, double amplitude_guess,
                                      double period_guess) {
        if(t_guess >= d_guess * 0.5)
            return elasticIn_guess(t_guess + t_guess - d_guess, 0.5 * c_guess + b_guess,
                                   0.5 * c_guess, d_guess, amplitude_guess, period_guess);
        return elasticOut_guess(t_guess + t_guess, b_guess, 0.5 * c_guess, d_guess,
                                amplitude_guess, period_guess);
    }
    static double backInOut_guess(double t_guess, double b_guess, double c_guess,
                                   double d_guess, double overshoot_guess) {
        const double normalized_guess = t_guess / (d_guess * 0.5);
        const double scaled_guess = overshoot_guess * 1.525;
        if(normalized_guess >= 1.0) {
            const double shifted_guess = normalized_guess - 2.0;
            return (shifted_guess * shifted_guess * (scaled_guess + (scaled_guess + 1.0) * shifted_guess) + 2.0) * (0.5 * c_guess) + b_guess;
        }
        return (normalized_guess * (scaled_guess + 1.0) - scaled_guess) *
            power_guess(normalized_guess, 2) * (0.5 * c_guess) + b_guess;
    }
    static double backOutIn_guess(double t_guess, double b_guess, double c_guess,
                                   double d_guess, double overshoot_guess) {
        const double doubled_guess = t_guess + t_guess;
        if(t_guess >= d_guess * 0.5) {
            const double x_guess = (doubled_guess - d_guess) / d_guess;
            return x_guess * (0.5 * c_guess * x_guess) *
                ((overshoot_guess + 1.0) * x_guess - overshoot_guess) + 0.5 * c_guess + b_guess;
        }
        const double x_guess = doubled_guess / d_guess - 1.0;
        return (x_guess * x_guess * (overshoot_guess + (overshoot_guess + 1.0) * x_guess) + 1.0) * (0.5 * c_guess) + b_guess;
    }
};

NCB_REGISTER_CLASS(Equations) {
    Constructor();
    NCB_METHOD(calc);
    NCB_METHOD(easeNone);
    NCB_METHOD(easeInQuad);
    NCB_METHOD(easeOutQuad);
    NCB_METHOD(easeInOutQuad);
    NCB_METHOD(easeOutInQuad);
    NCB_METHOD(easeInCubic);
    NCB_METHOD(easeOutCubic);
    NCB_METHOD(easeInOutCubic);
    NCB_METHOD(easeOutInCubic);
    NCB_METHOD(easeInQuart);
    NCB_METHOD(easeOutQuart);
    NCB_METHOD(easeInOutQuart);
    NCB_METHOD(easeOutInQuart);
    NCB_METHOD(easeInQuint);
    NCB_METHOD(easeOutQuint);
    NCB_METHOD(easeInOutQuint);
    NCB_METHOD(easeOutInQuint);
    NCB_METHOD(easeInSine);
    NCB_METHOD(easeOutSine);
    NCB_METHOD(easeInOutSine);
    NCB_METHOD(easeOutInSine);
    NCB_METHOD(easeInCirc);
    NCB_METHOD(easeOutCirc);
    NCB_METHOD(easeInOutCirc);
    NCB_METHOD(easeOutInCirc);
    NCB_METHOD(easeInExpo);
    NCB_METHOD(easeOutExpo);
    NCB_METHOD(easeInOutExpo);
    NCB_METHOD(easeOutInExpo);
    NCB_METHOD(easeInElastic);
    NCB_METHOD(easeOutElastic);
    NCB_METHOD(easeInOutElastic);
    NCB_METHOD(easeOutInElastic);
    NCB_METHOD(easeInBack);
    NCB_METHOD(easeOutBack);
    NCB_METHOD(easeInOutBack);
    NCB_METHOD(easeOutInBack);
    NCB_METHOD(easeInBounce);
    NCB_METHOD(easeOutBounce);
    NCB_METHOD(easeInOutBounce);
    NCB_METHOD(easeOutInBounce);
}

static void PrintEquationsLicense_guess() {
    static const char license_guess[] = R"LICENSE(--- Equations License ---
Disclaimer for Robert Penner's Easing Equations license:

TERMS OF USE - EASING EQUATIONS

Open source under the BSD License.

Copyright (c) 2001 Robert Penner
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--- end Equations License ---)LICENSE";
    TVPAddImportantLog(ttstr(license_guess));
}

NCB_POST_REGIST_CALLBACK(PrintEquationsLicense_guess);

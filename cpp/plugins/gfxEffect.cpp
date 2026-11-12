#define NCB_MODULE_NAME TJS_W("gfxEffect.dll")
#include "ncbind.hpp"
#include "gfx/GfxFire.h"

NCB_REGISTER_CLASS(gfxFire) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD_RAW_CALLBACK(init, &Class::init, 0);
    NCB_METHOD_RAW_CALLBACK(clearFireSeed, &Class::clearFireSeed, 0);
    NCB_METHOD_RAW_CALLBACK(setFireSeed, &Class::setFireSeed, 0);
    NCB_METHOD_RAW_CALLBACK(setFireSeedPos, &Class::setFireSeedPos, 0);
    NCB_METHOD(updateWarpMap);
    NCB_METHOD(updateCoolMap);
    NCB_METHOD(applyRed);
    NCB_METHOD(applyBlue);
    NCB_METHOD(applyWhite);
    NCB_METHOD(applyCustom);
    NCB_METHOD(setCustomColorTable);
    NCB_METHOD(isRed);
    NCB_METHOD(isBlue);
    NCB_METHOD(isWhite);
    NCB_METHOD(cycle);
    NCB_PROPERTY_WO(seedLayer, setSeedLayer);
    NCB_PROPERTY_WO(targetLayer, setTargetLayer);
    NCB_PROPERTY(seedX, getSeedX, setSeedX);
    NCB_PROPERTY(seedY, getSeedY, setSeedY);
    NCB_PROPERTY_RO(numOfSeeds, getNumOfSeeds);
    NCB_PROPERTY(randomSeed, getRandomSeed, setRandomSeed);
    NCB_PROPERTY(forceH, getForceH, setForceH);
    NCB_PROPERTY(forceV, getForceV, setForceV);
    NCB_PROPERTY(boundRangeH, getBoundRangeH, setBoundRangeH);
    NCB_PROPERTY(boundRangeV, getBoundRangeV, setBoundRangeV);
    NCB_PROPERTY(scalingCoeff, getScalingCoeff, setScalingCoeff);
    NCB_PROPERTY(numOfBlurForCoolMap, getNumOfBlurForCoolMap, setNumOfBlurForCoolMap);
    NCB_PROPERTY(textureFilterType, getTextureFilterType, setTextureFilterType);
    NCB_PROPERTY(coolRange, getCoolRange, setCoolRange);
    NCB_PROPERTY(coolStrength, getCoolStrength, setCoolStrength);
    NCB_PROPERTY(coolParticleDensityDenominator, getCoolParticleDensityDenominator, setCoolParticleDensityDenominator);
    NCB_PROPERTY(coolParticleDensityNumerator, getCoolParticleDensityNumerator, setCoolParticleDensityNumerator);
    NCB_PROPERTY(edgeSmoothing, getEdgeSmoothing, setEdgeSmoothing);
}

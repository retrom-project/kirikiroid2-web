#ifndef LAYER_EX_PARTICLE_MANAGER_HPP
#define LAYER_EX_PARTICLE_MANAGER_HPP

#include "tjsNative.h"

namespace layerexparticle {

struct Range_guess {
    double max;
    double min;
    double span;

    Range_guess() = default;
    Range_guess(double maximum, double minimum) { set(maximum, minimum); }
    void set(double maximum, double minimum) {
        max = maximum;
        min = minimum;
        span = maximum - minimum;
    }
};

struct VariationRange_guess {
    Range_guess value;
    Range_guess variation;

    VariationRange_guess() = default;
    VariationRange_guess(double maximum, double minimum,
                         double variationMaximum, double variationMinimum) :
        value(maximum, minimum), variation(variationMaximum, variationMinimum) {}
};

class ParticleManager;

struct Particle {
    explicit Particle(ParticleManager *manager) : manager_guess(manager) {}
    virtual void assign_guess(const Particle &source);
    virtual void initialize_guess(double initialX, double initialY,
                                  double initialOpacity);
    virtual void update_guess(tjs_int delta) = 0;

    double x;
    double y;
    double magnify;
    double magnifyVariation_guess;
    double angle;
    double angleOmega_guess;
    double opacity;
    double opacityVariation_guess;
    tjs_int lifetime;
    ParticleManager *manager_guess;
    tjs_int imageIndex;
};

struct VectorParticle : Particle {
    explicit VectorParticle(ParticleManager *manager) : Particle(manager) {}
    void update_guess(tjs_int delta) override;
    double velocityX_guess;
    double velocityY_guess;
    double accelerationX_guess;
    double accelerationY_guess;
};

struct RotateParticle : Particle {
    explicit RotateParticle(ParticleManager *manager) : Particle(manager) {}
    void update_guess(tjs_int delta) override;
    double phase_guess;
    double omega_guess;
    double radius_guess;
    double radiusVariation_guess;
};

struct AccelRotateParticle : RotateParticle {
    explicit AccelRotateParticle(ParticleManager *manager) :
        RotateParticle(manager) {}
    void update_guess(tjs_int delta) override;
    double omegaMax_guess;
    double angularAcceleration_guess;
};

struct BlinkParticle : Particle {
    explicit BlinkParticle(ParticleManager *manager) : Particle(manager) {}
    void update_guess(tjs_int delta) override;
    tjs_int maxOpacity_guess;
    tjs_int cycleCount_guess;
};

void seedRandom_guess(tjs_uint32 seed);

class ParticleManager {
public:
    ParticleManager(tjs_int initialCount, tjs_int maximumCount,
                    double generationRate);
    // The native manager has no owning destructor. Assignment alone clears its
    // former particle slots; ordinary wrapper disposal leaves them allocated.
    ~ParticleManager() = default;

    virtual void assign_guess(const ParticleManager &source);
    virtual Particle **allocateSlots_guess(tjs_int count);
    virtual Particle *createParticle_guess();
    virtual void generate_guess(Particle *particle);
    virtual bool expired_guess(const Particle *particle);

    void setMaxCount(tjs_int count);
    void update(tjs_int delta);
    void spawn_guess();
    void remove_guess(tjs_int index = -1);

    tjs_int startCount;
    tjs_int maxCount;
    double generateRate;
    double generationRemainder_guess;
    Range_guess angleRange_guess;
    Range_guess angleOmegaRange_guess;
    tjs_int rotateReverse;
    VariationRange_guess magnify;
    VariationRange_guess opacity;
    Particle **particles_guess;
    tjs_int liveCount_guess;
    tjs_int capacity_guess;
    Particle **(ParticleManager::*allocator_guess)(tjs_int count);
    iTJSDispatch2 *ownerLayer;
    iTJSDispatch2 *particleInitializer;
    // Set by the wrapper's image division, deliberately absent from the ctor.
    tjs_int imageCount;

protected:
    void resetBase_guess(tjs_int initialCount, tjs_int maximumCount,
                         double generationRate);
    void clearSlots_guess();
    double *initializerSamples_guess(tjs_int count);
};

class VectorParticleManager : public ParticleManager {
public:
    VectorParticleManager(tjs_int initialCount = 0, tjs_int maximumCount = 50,
                          double generationRate = 0.01, double x = 0,
                          double y = 0, double width = 1024,
                          double height = 576);
    Particle **allocateSlots_guess(tjs_int count) override;
    Particle *createParticle_guess() override;
    void generate_guess(Particle *particle) override;

    double x_guess;
    double y_guess;
    double dx_guess;
    double dy_guess;
    VariationRange_guess velocity;
    VariationRange_guess direction;
};

class RotateParticleManager : public ParticleManager {
public:
    RotateParticleManager(tjs_int initialCount = 0, tjs_int maximumCount = 50,
                          double generationRate = 0.01, double x = 0,
                          double y = 0);
    Particle **allocateSlots_guess(tjs_int count) override;
    Particle *createParticle_guess() override;
    void generate_guess(Particle *particle) override;

    double x_guess;
    double y_guess;
    VariationRange_guess radius;
    Range_guess omega;
    tjs_int particleRotateReverse;
};

class AccelRotateParticleManager : public RotateParticleManager {
public:
    AccelRotateParticleManager(tjs_int initialCount = 0,
                               tjs_int maximumCount = 50,
                               double generationRate = 0.01, double x = 0,
                               double y = 0);
    Particle *createParticle_guess() override;
    void generate_guess(Particle *particle) override;

    Range_guess accelerationTime;
};

class BlinkParticleManager : public ParticleManager {
public:
    BlinkParticleManager(tjs_int initialCount = 0, tjs_int maximumCount = 50,
                         double generationRate = 0.01, double x = 0,
                         double y = 0, double width = 1024,
                         double height = 576);
    Particle **allocateSlots_guess(tjs_int count) override;
    Particle *createParticle_guess() override;
    void generate_guess(Particle *particle) override;

    double x_guess;
    double y_guess;
    double dx_guess;
    double dy_guess;
    Range_guess time;
    Range_guess count;
};

} // namespace layerexparticle

#endif

#include "tjsCommHead.h"
#include "layerExParticleManager.hpp"

#include <cmath>
#include <cstring>
#include <new>

namespace layerexparticle {
namespace {
    tjs_uint32 randomState_guess = 0;
    constexpr double fullCircle_guess = 6.283185307179586;

    double randomUnit_guess() {
        randomState_guess = 2100005341u * randomState_guess + 1u;
        return static_cast<double>(randomState_guess) * 2.328306436538696e-10;
    }

    double sample_guess(const Range_guess &range) {
        return randomUnit_guess() * range.span + range.min;
    }

    tjs_int oldest_guess(const ParticleManager &manager) {
        tjs_int index = -1;
        tjs_int longest = 0;
        for(tjs_int i = 0; i < manager.capacity_guess; ++i) {
            Particle *particle = manager.particles_guess[i];
            if(particle && longest < particle->lifetime) {
                longest = particle->lifetime;
                index = i;
            }
        }
        return index;
    }
}

void seedRandom_guess(tjs_uint32 seed) { randomState_guess = seed; }

void Particle::assign_guess(const Particle &source) {
    x = source.x;
    y = source.y;
    magnify = source.magnify;
    magnifyVariation_guess = source.magnifyVariation_guess;
    angle = source.angle;
    angleOmega_guess = source.angleOmega_guess;
    opacity = source.opacity;
    opacityVariation_guess = source.opacityVariation_guess;
    lifetime = source.lifetime;
    imageIndex = source.imageIndex;
}

void Particle::initialize_guess(double initialX, double initialY,
                                double initialOpacity) {
    lifetime = 0;
    x = initialX;
    y = initialY;
    magnify = 1.0;
    magnifyVariation_guess = 0.0;
    angle = 0.0;
    angleOmega_guess = 0.0;
    opacity = initialOpacity;
    opacityVariation_guess = 0.0;
}

void VectorParticle::update_guess(tjs_int delta) {
    const double elapsed = static_cast<double>(delta);
    lifetime += delta;
    magnify = magnifyVariation_guess * elapsed + magnify;
    angle = angleOmega_guess * elapsed + angle;
    opacity = opacityVariation_guess * elapsed + opacity;
    const double velocityXChange_guess = accelerationX_guess * elapsed;
    const double velocityYChange_guess = accelerationY_guess * elapsed;
    x = elapsed * velocityXChange_guess * 0.5 +
        velocityX_guess * elapsed + x;
    y = 0.5 * (elapsed * velocityYChange_guess) +
        elapsed * velocityY_guess + y;
    velocityX_guess = velocityXChange_guess + velocityX_guess;
    velocityY_guess = velocityYChange_guess + velocityY_guess;
}

void RotateParticle::update_guess(tjs_int delta) {
    const double elapsed = static_cast<double>(delta);
    lifetime += delta;
    magnify = magnifyVariation_guess * elapsed + magnify;
    angle = angleOmega_guess * elapsed + angle;
    opacity = opacityVariation_guess * elapsed + opacity;
    const double phase = omega_guess * elapsed + phase_guess;
    phase_guess = phase;
    const double distance = elapsed * radiusVariation_guess + radius_guess;
    radius_guess = distance;
    const auto *manager = static_cast<const RotateParticleManager *>(manager_guess);
    x = std::cos(phase) * distance + manager->x_guess;
    y = manager->y_guess - std::sin(phase) * distance;
}

void AccelRotateParticle::update_guess(tjs_int delta) {
    RotateParticle::update_guess(delta);
    const double velocity = static_cast<double>(delta) *
        angularAcceleration_guess + omega_guess;
    omega_guess = velocity;
    if(velocity > omegaMax_guess)
        omega_guess = omegaMax_guess;
}

void BlinkParticle::update_guess(tjs_int delta) {
    const double elapsed = static_cast<double>(delta);
    lifetime += delta;
    magnify = magnifyVariation_guess * elapsed + magnify;
    angle = angleOmega_guess * elapsed + angle;
    const double value = elapsed * opacityVariation_guess + opacity;
    opacity = value;
    if(value < 0.0) {
        --cycleCount_guess;
        opacityVariation_guess = opacityVariation_guess * -1.0;
        opacity = 0.0;
        return;
    }
    const double maximum = static_cast<double>(maxOpacity_guess);
    if(maximum <= value) {
        opacityVariation_guess = opacityVariation_guess * -1.0;
        opacity = maximum;
    }
}

ParticleManager::ParticleManager(tjs_int initialCount, tjs_int maximumCount,
                                 double generationRate) {
    resetBase_guess(initialCount, maximumCount, generationRate);
}

void ParticleManager::resetBase_guess(tjs_int initialCount,
                                      tjs_int maximumCount,
                                      double generationRate) {
    generateRate = generationRate;
    generationRemainder_guess = 0.0;
    maxCount = 0;
    particles_guess = nullptr;
    liveCount_guess = 0;
    capacity_guess = 0;
    allocator_guess = nullptr;
    particleInitializer = nullptr;
    startCount = initialCount < maximumCount ? initialCount : maximumCount;
    angleRange_guess.set(0.0, 0.0);
    angleOmegaRange_guess.set(0.0, 0.0);
    rotateReverse = 0;
    magnify = VariationRange_guess(1.0, 1.0, 0.0, 0.0);
    opacity = VariationRange_guess(255.0, 0.0, 0.0, 0.0);
    // ownerLayer and imageCount are assigned by the wrapper, not this ctor.
}

void ParticleManager::clearSlots_guess() {
    for(tjs_int i = 0; i < capacity_guess; ++i) {
        if(particles_guess[i])
            ::operator delete(particles_guess[i]);
    }
    ::operator delete(particles_guess);
    particles_guess = nullptr;
    liveCount_guess = 0;
}

void ParticleManager::assign_guess(const ParticleManager &source) {
    startCount = source.startCount;
    maxCount = source.maxCount;
    generateRate = source.generateRate;
    generationRemainder_guess = source.generationRemainder_guess;
    angleRange_guess = source.angleRange_guess;
    angleOmegaRange_guess = source.angleOmegaRange_guess;
    rotateReverse = source.rotateReverse;
    magnify = source.magnify;
    opacity = source.opacity;
    if(particles_guess)
        clearSlots_guess();
    liveCount_guess = source.liveCount_guess;
    capacity_guess = source.capacity_guess;
    particles_guess = allocateSlots_guess(capacity_guess);
    for(tjs_int i = 0; i < capacity_guess; ++i) {
        if(source.particles_guess[i]) {
            particles_guess[i] = createParticle_guess();
            particles_guess[i]->assign_guess(*source.particles_guess[i]);
        } else {
            particles_guess[i] = nullptr;
        }
    }
    particleInitializer = source.particleInitializer;
    particleInitializer->AddRef();
    imageCount = source.imageCount;
}

Particle **ParticleManager::allocateSlots_guess(tjs_int) { return nullptr; }
Particle *ParticleManager::createParticle_guess() { return nullptr; }

void ParticleManager::setMaxCount(tjs_int requested) {
    if(requested > capacity_guess) {
        Particle **previous = particles_guess;
        particles_guess = (this->*allocator_guess)(requested);
        if(previous)
            std::memcpy(particles_guess, previous,
                        sizeof(Particle *) * capacity_guess);
        std::memset(particles_guess + capacity_guess, 0,
                    sizeof(Particle *) * (requested - capacity_guess));
        capacity_guess = requested;
    }
    if(requested < liveCount_guess && liveCount_guess - requested > 0) {
        // The native shrinking branch repeatedly removes the oldest particle
        // without a terminating count or jump out of this branch.
        for(;;) {
            const tjs_int index = oldest_guess(*this);
            if(index >= 0)
                remove_guess(index);
        }
    }
    maxCount = requested;
}

void ParticleManager::update(tjs_int delta) {
    while(liveCount_guess < startCount)
        spawn_guess();
    for(tjs_int i = 0; i < capacity_guess; ++i) {
        Particle *particle = particles_guess[i];
        if(particle && particle->lifetime >= 0) {
            particle->update_guess(delta);
            if(expired_guess(particle))
                remove_guess(i);
        }
    }
    const double amount = static_cast<double>(delta) * generateRate +
        generationRemainder_guess;
    generationRemainder_guess = amount;
    if(amount >= 1.0) {
        tjs_int count = static_cast<tjs_int>(amount);
        generationRemainder_guess = amount - static_cast<double>(count);
        const tjs_int available = maxCount - liveCount_guess;
        if(count > available)
            count = available;
        for(; count > 0; --count)
            spawn_guess();
    } else if(amount <= -1.0) {
        tjs_int count = static_cast<tjs_int>(amount);
        generationRemainder_guess = amount - static_cast<double>(count);
        if(liveCount_guess + count < 0)
            count = -liveCount_guess;
        for(tjs_int remaining = -count; remaining > 0; --remaining) {
            const tjs_int index = oldest_guess(*this);
            if(index >= 0)
                remove_guess(index);
        }
    }
}

void ParticleManager::spawn_guess() {
    if(capacity_guess <= 0)
        return;
    tjs_int index = 0;
    for(; particles_guess[index]; ++index) {
        if(particles_guess[index]->lifetime < 0)
            break;
        if(index + 1 >= capacity_guess)
            return;
    }
    if(!particles_guess[index])
        particles_guess[index] = createParticle_guess();
    generate_guess(particles_guess[index]);
    particles_guess[index]->imageIndex =
        static_cast<tjs_int>(randomUnit_guess() * static_cast<double>(imageCount));
    ++liveCount_guess;
}

void ParticleManager::remove_guess(tjs_int index) {
    if(index < 0)
        index = oldest_guess(*this);
    if(index >= 0 && index < capacity_guess && particles_guess[index]) {
        particles_guess[index]->lifetime = -1;
        --liveCount_guess;
    }
}

bool ParticleManager::expired_guess(const Particle *particle) {
    return particle->magnify <= 0.0 || particle->opacity <= 0.0;
}

void ParticleManager::generate_guess(Particle *particle) {
    particle->magnify = sample_guess(magnify.value);
    particle->magnifyVariation_guess = sample_guess(magnify.variation);
    particle->angle = sample_guess(angleRange_guess);
    particle->angleOmega_guess = sample_guess(angleOmegaRange_guess);
    if(rotateReverse) {
        const double sign = randomUnit_guess() <= 0.5 ? 1.0 : -1.0;
        particle->angleOmega_guess = sign * particle->angleOmega_guess;
    }
    particle->opacity = sample_guess(opacity.value);
    particle->opacityVariation_guess = sample_guess(opacity.variation);
}

double *ParticleManager::initializerSamples_guess(tjs_int count) {
    auto *values = static_cast<double *>(::operator new(sizeof(double) * count));
    if(count > 0)
        std::memset(values, 0, sizeof(double) * count);
    tTJSVariant value;
    if(TJS_SUCCEEDED(particleInitializer->FuncCall(0, nullptr, nullptr,
                                                  &value, 0, nullptr,
                                                  ownerLayer))) {
        iTJSDispatch2 *array = value.AsObjectNoAddRef();
        if(TJS_SUCCEEDED(array->PropGet(0, TJS_W("count"), nullptr,
                                       &value, array))) {
            tjs_int length = static_cast<tjs_int>(value.AsReal());
            if(length > count)
                length = count;
            for(tjs_int i = 0; i < length; ++i) {
                if(TJS_SUCCEEDED(array->PropGetByNum(0, i, &value, array)))
                    values[i] = value.AsReal();
            }
        }
    }
    return values;
}

VectorParticleManager::VectorParticleManager(tjs_int initialCount,
                                             tjs_int maximumCount,
                                             double generationRate,
                                             double x, double y,
                                             double width, double height) :
    ParticleManager(initialCount, maximumCount, generationRate),
    x_guess(x), y_guess(y), dx_guess(width), dy_guess(height),
    velocity(0.1, 0.1, 0.0, 0.0),
    direction(fullCircle_guess, 0.0, 0.0, 0.0) {
    allocator_guess = &ParticleManager::allocateSlots_guess;
    setMaxCount(maximumCount);
}

Particle **VectorParticleManager::allocateSlots_guess(tjs_int count) {
    return static_cast<Particle **>(::operator new(sizeof(Particle *) * count));
}

Particle *VectorParticleManager::createParticle_guess() {
    return new VectorParticle(this);
}

void VectorParticleManager::generate_guess(Particle *base) {
    auto *particle = static_cast<VectorParticle *>(base);
    if(particleInitializer) {
        double *values = initializerSamples_guess(7);
        particle->initialize_guess(values[0], values[1], values[2]);
        particle->velocityX_guess = values[3];
        particle->velocityY_guess = values[4];
        particle->accelerationX_guess = values[5];
        particle->accelerationY_guess = values[6];
        ::operator delete(values);
    } else {
        const double vectorAngle = sample_guess(direction.value);
        const double speed = sample_guess(velocity.value);
        const double velocityX = std::cos(vectorAngle) * speed;
        const double velocityY = -(std::sin(vectorAngle) * speed);
        const double accelAngle = sample_guess(direction.variation);
        const double acceleration = sample_guess(velocity.variation);
        const double accelerationX = std::cos(accelAngle) * acceleration;
        const double accelerationY = -(std::sin(accelAngle) * acceleration);
        randomUnit_guess();
        randomUnit_guess();
        const double random = randomUnit_guess();
        const double x = dx_guess * random + x_guess;
        const double y = dy_guess * random + y_guess;
        const double initialOpacity = random * opacity.value.span + opacity.value.min;
        particle->initialize_guess(x, y, initialOpacity);
        particle->velocityX_guess = velocityX;
        particle->velocityY_guess = velocityY;
        particle->accelerationX_guess = accelerationX;
        particle->accelerationY_guess = accelerationY;
    }
    ParticleManager::generate_guess(particle);
}

RotateParticleManager::RotateParticleManager(tjs_int initialCount,
                                             tjs_int maximumCount,
                                             double generationRate,
                                             double x, double y) :
    ParticleManager(initialCount, maximumCount, generationRate),
    x_guess(x), y_guess(y), radius(1024.0, 0.0, 0.0, 0.0),
    omega(0.006283185307179587, 0.0), particleRotateReverse(0) {
    allocator_guess = &ParticleManager::allocateSlots_guess;
    setMaxCount(maximumCount);
}

Particle **RotateParticleManager::allocateSlots_guess(tjs_int count) {
    return static_cast<Particle **>(::operator new(sizeof(Particle *) * count));
}

Particle *RotateParticleManager::createParticle_guess() {
    return new RotateParticle(this);
}

void RotateParticleManager::generate_guess(Particle *base) {
    auto *particle = static_cast<RotateParticle *>(base);
    if(particleInitializer) {
        double *values = initializerSamples_guess(7);
        particle->initialize_guess(0.0, 0.0, values[2]);
        particle->phase_guess = values[3];
        particle->omega_guess = values[4];
        particle->radius_guess = values[5];
        particle->radiusVariation_guess = values[6];
        ::operator delete(values);
    } else {
        const double phase = randomUnit_guess() * fullCircle_guess;
        double velocity = sample_guess(omega);
        if(particleRotateReverse)
            velocity = velocity * (randomUnit_guess() <= 0.5 ? 1.0 : -1.0);
        const double radiusVariation = sample_guess(radius.variation);
        const double initialRadius = sample_guess(radius.value);
        const double initialOpacity = sample_guess(opacity.value);
        particle->initialize_guess(0.0, 0.0, initialOpacity);
        particle->phase_guess = phase;
        particle->omega_guess = velocity;
        particle->radius_guess = initialRadius;
        particle->radiusVariation_guess = radiusVariation;
    }
    ParticleManager::generate_guess(particle);
}

AccelRotateParticleManager::AccelRotateParticleManager(tjs_int initialCount,
                                                       tjs_int maximumCount,
                                                       double generationRate,
                                                       double x, double y) :
    RotateParticleManager(), accelerationTime(2000.0, 1000.0) {
    resetBase_guess(initialCount, maximumCount, generationRate);
    x_guess = x;
    y_guess = y;
    radius = VariationRange_guess(1024.0, 0.0, 0.0, 0.0);
    omega.set(0.006283185307179587, 0.0);
    particleRotateReverse = 0;
    allocator_guess = &ParticleManager::allocateSlots_guess;
    setMaxCount(maximumCount);
}

Particle *AccelRotateParticleManager::createParticle_guess() {
    return new AccelRotateParticle(this);
}

void AccelRotateParticleManager::generate_guess(Particle *base) {
    auto *particle = static_cast<AccelRotateParticle *>(base);
    if(particleInitializer) {
        double *values = initializerSamples_guess(8);
        particle->initialize_guess(0.0, 0.0, values[2]);
        particle->phase_guess = values[3];
        particle->omega_guess = 0.0;
        particle->radius_guess = values[5];
        particle->radiusVariation_guess = values[6];
        particle->omegaMax_guess = values[4];
        particle->angularAcceleration_guess = values[7];
        ::operator delete(values);
    } else {
        const double phase = randomUnit_guess() * fullCircle_guess;
        double maximumVelocity = sample_guess(omega);
        if(particleRotateReverse)
            maximumVelocity = maximumVelocity *
                (randomUnit_guess() <= 0.5 ? 1.0 : -1.0);
        const double acceleration = maximumVelocity / sample_guess(accelerationTime);
        const double radiusVariation = sample_guess(radius.variation);
        const double initialRadius = sample_guess(radius.value);
        const double initialOpacity = sample_guess(opacity.value);
        particle->initialize_guess(0.0, 0.0, initialOpacity);
        particle->phase_guess = phase;
        particle->omega_guess = 0.0;
        particle->radius_guess = initialRadius;
        particle->radiusVariation_guess = radiusVariation;
        particle->omegaMax_guess = maximumVelocity;
        particle->angularAcceleration_guess = acceleration;
    }
    ParticleManager::generate_guess(particle);
}

BlinkParticleManager::BlinkParticleManager(tjs_int initialCount,
                                           tjs_int maximumCount,
                                           double generationRate,
                                           double x, double y,
                                           double width, double height) :
    ParticleManager(initialCount, maximumCount, generationRate),
    x_guess(x), y_guess(y), dx_guess(width), dy_guess(height),
    time(1000.0, 500.0), count(1.0, 1.0) {
    opacity.value.set(255.0, 255.0);
    allocator_guess = &ParticleManager::allocateSlots_guess;
    setMaxCount(maximumCount);
}

Particle **BlinkParticleManager::allocateSlots_guess(tjs_int count) {
    return static_cast<Particle **>(::operator new(sizeof(Particle *) * count));
}

Particle *BlinkParticleManager::createParticle_guess() {
    return new BlinkParticle(this);
}

void BlinkParticleManager::generate_guess(Particle *base) {
    auto *particle = static_cast<BlinkParticle *>(base);
    ParticleManager::generate_guess(particle);
    if(particleInitializer) {
        double *values = initializerSamples_guess(5);
        const tjs_int cycles = static_cast<tjs_int>(values[4]);
        particle->initialize_guess(values[0], values[1], 0.0);
        particle->opacityVariation_guess = values[3];
        particle->maxOpacity_guess = static_cast<tjs_int>(values[2]);
        particle->cycleCount_guess = cycles;
        ::operator delete(values);
    } else {
        const double duration = sample_guess(time);
        const double maximum = sample_guess(opacity.value);
        const double randomX = randomUnit_guess();
        randomUnit_guess();
        const double random = randomUnit_guess();
        const double cycles = count.span * random + count.min;
        const double x = randomX * dx_guess + x_guess;
        const double y = random * dy_guess + y_guess;
        particle->initialize_guess(x, y, 0.0);
        particle->maxOpacity_guess = static_cast<tjs_int>(maximum);
        particle->cycleCount_guess = static_cast<tjs_int>(cycles);
        particle->opacityVariation_guess = maximum / duration + maximum / duration;
    }
}


} // namespace layerexparticle

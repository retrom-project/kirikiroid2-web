// Cubism 3 physics boundary reconstructed from the native plugin's parameter
// normalization, particle update and output callbacks. No model names or
// resources are embedded here. Parameter pools can be Core parameters or the
// plugin's independent ExternalParamter pool.
var krkrCubismPhysics = (() => {
    const f = Math.fround;
    const pi = f(3.14159);
    const tau = f(6.28318);
    const add = (a, b) => f(a + b);
    const sub = (a, b) => f(a - b);
    const mul = (a, b) => f(a * b);
    const div = (a, b) => f(a / b);
    const wrap = value => {
        while (value < -pi) value = add(value, tau);
        while (value > pi) value = sub(value, tau);
        return value;
    };

    function finite(value, label) {
        if (typeof value !== 'number' || !Number.isFinite(value))
            throw new Error('Invalid Cubism physics ' + label);
        return f(value);
    }

    function type(value) {
        if (value === 'X' || value === 'Y' || value === 'Angle') return value;
        throw new Error('Unsupported Cubism physics type: ' + value);
    }

    function normalization(value) {
        if (!value) throw new Error('Missing Cubism physics normalization');
        return {
            min: finite(value.Minimum, 'normalization minimum'),
            max: finite(value.Maximum, 'normalization maximum'),
            default: finite(value.Default, 'normalization default'),
        };
    }

    function normalize(value, min, max, range, reflect) {
        const low = Math.min(min, max), high = Math.max(min, max);
        // The native callbacks ignore out-of-range inputs, and use the middle
        // of the declared range rather than the parameter's default value.
        if (value < low || value > high) return 0;
        const center = add(mul(Math.abs(sub(high, low)), 0.5), low);
        const offset = sub(value, center);
        let result = 0;
        if (offset > 0) {
            const width = sub(high, center);
            if (width !== 0)
                result = add(mul(div(sub(Math.max(range.min, range.max), range.default), width), offset), range.default);
        } else if (offset < 0) {
            const width = sub(low, center);
            if (width !== 0)
                result = add(mul(div(sub(Math.min(range.min, range.max), range.default), width), offset), range.default);
        } else result = range.default;
        return reflect ? result : -result;
    }

    function particles(vertices) {
        let initialY = 0;
        return vertices.map((vertex, index) => {
            const radius = finite(vertex.Radius, 'particle radius');
            if (index) initialY = add(initialY, radius);
            return {
                mobility: finite(vertex.Mobility, 'particle mobility'),
                delay: finite(vertex.Delay, 'particle delay'),
                acceleration: finite(vertex.Acceleration, 'particle acceleration'),
                radius,
                x: 0, y: initialY, lastX: 0, lastY: initialY,
                gravityX: 0, gravityY: 1, velocityX: 0, velocityY: 0,
            };
        });
    }

    function updateParticles(list, angle, threshold, delta, tx, ty, windX, windY) {
        list[0].x = tx;
        list[0].y = ty;
        const radians = mul(div(angle, 180), pi);
        let gravityX = f(Math.sin(radians)), gravityY = f(Math.cos(radians));
        const length = f(Math.sqrt(add(mul(gravityX, gravityX), mul(gravityY, gravityY))));
        gravityX = div(gravityX, length);
        gravityY = div(gravityY, length);
        const gravityAngle = f(Math.atan2(gravityY, gravityX));
        for (let i = 1; i < list.length; ++i) {
            const previous = list[i - 1], current = list[i];
            const forceX = add(mul(gravityX, current.acceleration), windX);
            const forceY = add(mul(gravityY, current.acceleration), windY);
            const delay = mul(mul(current.delay, delta), 30);
            current.lastX = current.x;
            current.lastY = current.y;
            let dx = sub(current.x, previous.x), dy = sub(current.y, previous.y);
            const rotation = div(wrap(sub(gravityAngle, f(Math.atan2(current.gravityY, current.gravityX)))), 5);
            const cosine = f(Math.cos(rotation)), sine = f(Math.sin(rotation));
            // Both native rotation sites compute Y after writing X. Preserve
            // that observable order rather than substituting a matrix helper.
            dx = sub(mul(cosine, dx), mul(sine, dy));
            dy = add(mul(sine, dx), mul(cosine, dy));
            dx = sub(add(mul(mul(forceX, delay), delay), add(mul(current.velocityX, delay), add(previous.x, dx))), previous.x);
            dy = sub(add(mul(mul(forceY, delay), delay), add(add(previous.y, dy), mul(current.velocityY, delay))), previous.y);
            const distance = f(Math.sqrt(add(mul(dx, dx), mul(dy, dy))));
            current.x = add(previous.x, mul(current.radius, div(dx, distance)));
            current.y = add(previous.y, mul(current.radius, div(dy, distance)));
            if (Math.abs(current.x) < threshold) current.x = 0;
            if (delay !== 0) {
                current.velocityX = mul(current.mobility, div(sub(current.x, current.lastX), delay));
                current.velocityY = mul(current.mobility, div(sub(current.y, current.lastY), delay));
            }
            current.gravityX = gravityX;
            current.gravityY = gravityY;
        }
    }

    class Physics {
        constructor(document, pool) {
            if (!document || document.Version !== 3 || !Array.isArray(document.PhysicsSettings))
                throw new Error('Unsupported Cubism physics document');
            this.pool = pool;
            const lookup = new Map(pool.ids.map((name, index) => [name, index]));
            const resolve = name => {
                if (!lookup.has(name)) throw new Error('Cubism physics parameter does not exist: ' + name);
                return lookup.get(name);
            };
            this.settings = document.PhysicsSettings.map(setting => {
                if (!Array.isArray(setting.Input) || !Array.isArray(setting.Output) ||
                    !Array.isArray(setting.Vertices) || !setting.Vertices.length)
                    throw new Error('Invalid Cubism physics setting');
                const position = normalization(setting.Normalization?.Position);
                return {
                    position, angle: normalization(setting.Normalization?.Angle),
                    particles: particles(setting.Vertices),
                    inputs: setting.Input.map(input => ({
                        index: resolve(input.Source?.Id), type: type(input.Type),
                        weight: div(finite(input.Weight, 'input weight'), 100), reflect: !!input.Reflect,
                    })),
                    outputs: setting.Output.map(output => ({
                        index: resolve(output.Destination?.Id), type: type(output.Type),
                        vertex: output.VertexIndex,
                        scale: finite(output.Scale, 'output scale'),
                        weight: div(finite(output.Weight, 'output weight'), 100), reflect: !!output.Reflect,
                    })),
                };
            });
        }

        evaluate(deltaTime, options) {
            const delta = finite(deltaTime, 'time delta'), pool = this.pool;
            const gravityX = f(options.gravityX), gravityY = f(options.gravityY);
            const windX = f(options.windX), windY = f(options.windY);
            for (const setting of this.settings) {
                let x = 0, y = 0, angle = 0;
                for (const input of setting.inputs) {
                    const i = input.index;
                    const value = mul(normalize(pool.values[i], pool.minimumValues[i], pool.maximumValues[i],
                        input.type === 'Angle' ? setting.angle : setting.position, input.reflect), input.weight);
                    if (input.type === 'X') x = add(x, value);
                    else if (input.type === 'Y') y = add(y, value);
                    else angle = add(angle, value);
                }
                const rotation = mul(div(angle, -180), pi);
                const cosine = f(Math.cos(rotation)), sine = f(Math.sin(rotation));
                x = sub(mul(cosine, x), mul(sine, y));
                y = add(mul(sine, x), mul(cosine, y));
                updateParticles(setting.particles, angle, mul(setting.position.max, f(0.001)), delta, x, y, windX, windY);
                for (const output of setting.outputs) {
                    const n = output.vertex, list = setting.particles;
                    // The original evaluator stops this setting at an invalid
                    // output vertex, leaving its later outputs untouched.
                    if (!Number.isInteger(n) || n < 1 || n >= list.length) break;
                    const dx = sub(list[n].x, list[n - 1].x), dy = sub(list[n].y, list[n - 1].y);
                    let value;
                    if (output.type === 'X') value = dx;
                    else if (output.type === 'Y') value = dy;
                    else {
                        const oldX = n < 2 ? -gravityX : sub(list[n - 1].x, list[n - 2].x);
                        const oldY = n < 2 ? -gravityY : sub(list[n - 1].y, list[n - 2].y);
                        value = wrap(sub(f(Math.atan2(dy, dx)), f(Math.atan2(oldY, oldX))));
                    }
                    if (output.reflect) value = -value;
                    const i = output.index;
                    value = Math.min(pool.maximumValues[i], Math.max(pool.minimumValues[i], mul(value, output.scale)));
                    if (output.weight < 1)
                        value = add(mul(value, output.weight), mul(sub(1, output.weight), pool.values[i]));
                    pool.values[i] = value;
                }
            }
        }
    }

    return {Physics};
})();

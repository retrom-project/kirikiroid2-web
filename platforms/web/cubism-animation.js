// The native Live2DAnimation plugin accepts Cubism motion3 JSON. Its four
// segment callbacks use linear time, restricted cubic Beziers, stepped and
// inverse-stepped values. Game-authored TJS timeline dictionaries belong to
// their script animation system and are not coerced into motion3 JSON here.
var krkrCubismAnimation = (() => {
    const f = Math.fround;
    const lerp = (a, b, t) => f(f(f(b - a) * t) + a);
    const number = value => {
        if (typeof value !== 'number' || !Number.isFinite(value))
            throw new Error('Invalid Cubism motion number');
        return f(value);
    };

    function curve(source) {
        if (!['Model', 'Parameter', 'PartOpacity'].includes(source.Target) ||
            typeof source.Id !== 'string' || !Array.isArray(source.Segments) || source.Segments.length < 2)
            throw new Error('Invalid Cubism motion curve');
        const data = source.Segments;
        let start = [number(data[0]), number(data[1])];
        const segments = [];
        for (let i = 2; i < data.length;) {
            const type = data[i++];
            const points = [start];
            const count = type === 1 ? 3 : 1;
            if (![0, 1, 2, 3].includes(type) || i + count * 2 > data.length)
                throw new Error('Invalid Cubism motion segment');
            for (let p = 0; p < count; ++p) points.push([number(data[i++]), number(data[i++])]);
            if (points.at(-1)[0] <= start[0]) throw new Error('Cubism motion segment has no time range');
            segments.push({type, points});
            start = points.at(-1);
        }
        return {target: source.Target, id: source.Id, segments};
    }

    function evaluate(curve, time) {
        const segments = curve.segments;
        if (!segments.length) throw new Error('Cubism motion curve has no segments');
        let segment = segments.at(-1);
        for (let i = 0; i < segments.length - 1; ++i) {
            if (segments[i].points.at(-1)[0] > time) { segment = segments[i]; break; }
        }
        const p = segment.points;
        if (segment.type === 2) return p[0][1];
        if (segment.type === 3) return p[1][1];
        const t = f(f(time - p[0][0]) / f(p.at(-1)[0] - p[0][0]));
        if (segment.type === 0) return lerp(p[0][1], p[1][1], t);
        const a = lerp(p[0][1], p[1][1], t), b = lerp(p[1][1], p[2][1], t), c = lerp(p[2][1], p[3][1], t);
        return lerp(lerp(a, b, t), lerp(b, c, t), t);
    }

    class Animation {
        constructor(document) {
            if (!document || document.Version !== 3 || !document.Meta || !Array.isArray(document.Curves))
                throw new Error('Unsupported Cubism motion document');
            this.duration = number(document.Meta.Duration);
            this.loop = !!document.Meta.Loop;
            if (this.loop && this.duration <= 0) throw new Error('Looping Cubism motion requires a positive duration');
            this.curves = document.Curves.map(curve);
            this.time = 0;
        }

        apply(model, delta, weight) {
            this.time = f(this.time + number(delta));
            weight = number(weight);
            let time = this.time;
            if (this.loop) while (time > this.duration) time = f(time - this.duration);
            for (const curve of this.curves) {
                if (curve.target === 'Model') continue;
                const pool = curve.target === 'Parameter' ? model.parameters : model.parts;
                const index = pool.ids.indexOf(curve.id);
                if (index < 0) continue;
                const values = curve.target === 'Parameter' ? pool.values : pool.opacities;
                const initial = curve.target === 'Parameter' ? pool.defaultValues[index] : 0;
                // Native applyAnimation resets each affected parameter to its
                // declared default before blending this animation's value.
                values[index] = f(f(f(1 - weight) * initial) + f(evaluate(curve, time) * weight));
            }
        }
    }

    return {Animation};
})();

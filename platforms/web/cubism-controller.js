// Script-facing state of the native Cubism plugin. Core and physics retain
// their own parameter pools; external outputs are added to Core values only
// after both rigs have been evaluated.
var krkrCubismController = (() => {
    const f = Math.fround;

    class Controller {
        constructor(model, settings) {
            this.model = model;
            this.parameters = new Map(model.parameters.ids.map((name, index) => [name, index]));
            this.system = new Map();
            this.rates = new Map();
            this.lipSync = [];
            for (const group of settings.Groups || []) {
                if (!Array.isArray(group.Ids)) continue;
                for (const id of group.Ids) {
                    const index = this.parameters.get(id);
                    if (index === undefined) continue;
                    if (group.Name === 'LipSync') this.lipSync.push(index);
                    if (group.Name === 'EyeBlink') this.system.set(index, model.parameters.defaultValues[index]);
                }
            }
            this.physics = null;
            this.externalPhysics = null;
            this.external = null;
            this.externalIndices = new Map();
            this.outputs = [];
            this.enabled = [];
        }

        get(name) {
            const index = this.parameters.get(name);
            if (index !== undefined)
                return this.system.has(index) ? this.system.get(index) : this.model.parameters.values[index];
            const external = this.externalIndices.get(name);
            return external === undefined ? undefined : this.external.values[external];
        }

        set(name, value) {
            value = f(value);
            const index = this.parameters.get(name);
            if (index !== undefined) {
                if (this.system.has(index)) this.system.set(index, value);
                else this.model.parameters.values[index] = value;
                return true;
            }
            const external = this.externalIndices.get(name);
            if (external === undefined) return false;
            this.external.values[external] = value;
            return true;
        }

        setRate(name, value) {
            const index = this.parameters.get(name);
            if (index === undefined) return false;
            this.rates.set(index, f(Math.min(1, Math.max(0, value))));
            return true;
        }

        resetDefaults() {
            this.model.parameters.values.set(this.model.parameters.defaultValues);
            for (const index of this.system.keys()) this.system.set(index, this.model.parameters.defaultValues[index]);
        }

        resetSystem() {
            for (const index of this.lipSync) this.model.parameters.values[index] = this.model.parameters.defaultValues[index];
        }

        loadPhysics(document, external) {
            if (external) {
                if (!this.external) throw new Error('Cubism external parameters must be loaded before external physics');
                this.externalPhysics = new krkrCubismPhysics.Physics(document, this.external);
            } else this.physics = new krkrCubismPhysics.Physics(document, this.model.parameters);
        }

        loadExternalParameters(document) {
            if (!Array.isArray(document)) throw new Error('Invalid Cubism external parameter document');
            const pool = {
                ids: [], values: new Float32Array(document.length),
                minimumValues: new Float32Array(document.length), maximumValues: new Float32Array(document.length),
                defaultValues: new Float32Array(document.length),
            };
            const indices = new Map(), outputs = [];
            let maximumEnableIndex = -1;
            for (let i = 0; i < document.length; ++i) {
                const item = document[i];
                if (typeof item.Name !== 'string' || ![item.Min, item.Max, item.Default].every(Number.isFinite))
                    throw new Error('Invalid Cubism external parameter');
                pool.ids.push(item.Name);
                pool.minimumValues[i] = item.Min;
                pool.maximumValues[i] = item.Max;
                pool.values[i] = pool.defaultValues[i] = item.Default;
                indices.set(item.Name, i);
                const enabled = item.EnableIndex === undefined ? -1 : item.EnableIndex;
                if (!Number.isInteger(enabled) || enabled < -1) throw new Error('Invalid Cubism external enable index');
                maximumEnableIndex = Math.max(maximumEnableIndex, enabled);
                if (item.Output) {
                    const target = this.parameters.get(item.Destination === undefined ? item.Name : item.Destination);
                    outputs.push({source: i, target, enabled});
                }
            }
            this.external = pool;
            this.externalIndices = indices;
            this.outputs = outputs;
            this.enabled = new Array(maximumEnableIndex + 1).fill(false);
            this.externalPhysics = null;
        }

        setExternalEnabled(index, enabled) {
            if (Number.isInteger(index) && index >= 0 && index < this.enabled.length)
                this.enabled[index] = !!enabled;
        }

        update(delta, options, voicePeak) {
            const parameters = this.model.parameters;
            if (voicePeak >= 0) {
                let peak = voicePeak;
                if (options.lipSyncRate >= 0) peak = Math.min(32767, Math.trunc(f(peak * options.lipSyncRate)));
                for (const index of this.lipSync)
                    parameters.values[index] = f(f(f(parameters.maximumValues[index] - parameters.defaultValues[index]) * peak) / 32767);
            }
            if (options.enablePhysics) {
                if (this.physics) this.physics.evaluate(delta, options);
                if (this.external) {
                    const x = this.externalIndices.get('x'), y = this.externalIndices.get('y');
                    if (x !== undefined) this.external.values[x] = options.positionX;
                    if (y !== undefined) this.external.values[y] = options.positionY;
                    if (this.externalPhysics) this.externalPhysics.evaluate(delta, options);
                    for (const output of this.outputs) {
                        if (output.target === undefined || (output.enabled >= 0 && !this.enabled[output.enabled])) continue;
                        const index = output.target;
                        parameters.values[index] = Math.min(parameters.maximumValues[index], Math.max(parameters.minimumValues[index],
                            f(parameters.values[index] + this.external.values[output.source])));
                    }
                }
            }
            const blink = options.eyeBlinkRate < 0 ? 1 : options.eyeBlinkRate;
            for (const [index, value] of this.system) parameters.values[index] = f(value * blink);
            for (const [index, rate] of this.rates) parameters.values[index] = f(parameters.values[index] * rate);
        }
    }

    return {Controller};
})();

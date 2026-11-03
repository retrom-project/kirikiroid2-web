// Additional browser plugin boundary for Cubism models. The proprietary Core
// is an explicit build input; this file neither downloads nor embeds it.
var krkrCubismWeb = (() => {
    const vertexSource = `#version 300 es
precision mediump float;
in vec2 VertexPosition;
in vec2 VertexUv;
out vec2 DiffuseUv;
out vec2 MaskUv;
uniform mat4 Mvp;
void main() {
    vec4 position = Mvp * vec4(VertexPosition, 0.0, 1.0);
    gl_Position = position;
    MaskUv = (position.xy / position.w) * 0.5 + 0.5;
    DiffuseUv = VertexUv;
}`;
    const fragmentSource = `#version 300 es
precision mediump float;
in vec2 DiffuseUv;
uniform sampler2D DiffuseTexture;
uniform float Opacity;
out vec4 FragColor;
void main() {
    vec4 color = texture(DiffuseTexture, DiffuseUv);
    color.a *= Opacity;
    color.rgb *= color.a;
    FragColor = color;
}`;
    const maskedFragmentSource = `#version 300 es
precision mediump float;
in vec2 DiffuseUv;
in vec2 MaskUv;
uniform sampler2D DiffuseTexture;
uniform sampler2D MaskTexture;
uniform float Opacity;
uniform bool InvertMask;
out vec4 FragColor;
void main() {
    vec4 color = texture(DiffuseTexture, DiffuseUv);
    float mask = texture(MaskTexture, MaskUv).a;
    color.a *= Opacity * (InvertMask ? 1.0 - mask : mask);
    color.rgb *= color.a;
    FragColor = color;
}`;

    function shader(gl, type, source) {
        const result = gl.createShader(type);
        gl.shaderSource(result, source);
        gl.compileShader(result);
        if (!gl.getShaderParameter(result, gl.COMPILE_STATUS)) {
            const message = gl.getShaderInfoLog(result);
            gl.deleteShader(result);
            throw new Error('Cubism shader compilation failed: ' + message);
        }
        return result;
    }

    function program(gl, fragment) {
        const vertex = shader(gl, gl.VERTEX_SHADER, vertexSource);
        let pixel = null;
        let result = null;
        try {
            pixel = shader(gl, gl.FRAGMENT_SHADER, fragment);
            result = gl.createProgram();
            gl.attachShader(result, vertex);
            gl.attachShader(result, pixel);
            gl.bindAttribLocation(result, 0, 'VertexPosition');
            gl.bindAttribLocation(result, 1, 'VertexUv');
            gl.linkProgram(result);
            if (!gl.getProgramParameter(result, gl.LINK_STATUS))
                throw new Error('Cubism program link failed: ' + gl.getProgramInfoLog(result));
            return {
                program: result,
                mvp: gl.getUniformLocation(result, 'Mvp'),
                opacity: gl.getUniformLocation(result, 'Opacity'),
                diffuse: gl.getUniformLocation(result, 'DiffuseTexture'),
                mask: gl.getUniformLocation(result, 'MaskTexture'),
                inverse: gl.getUniformLocation(result, 'InvertMask'),
            };
        } catch (error) {
            if (result) gl.deleteProgram(result);
            throw error;
        } finally {
            gl.deleteShader(vertex);
            if (pixel) gl.deleteShader(pixel);
        }
    }

    // The engine's Canvas shares a GL context with other drawing code. A
    // private VAO plus restoration of every mutated binding avoids leaking
    // Cubism state into Canvas or invalidating Cocos/Emscripten state caches.
    function saveState(gl) {
        const activeTexture = gl.getParameter(gl.ACTIVE_TEXTURE);
        const textures = [];
        for (let unit = 0; unit < 2; ++unit) {
            gl.activeTexture(gl.TEXTURE0 + unit);
            textures.push(gl.getParameter(gl.TEXTURE_BINDING_2D));
        }
        gl.activeTexture(activeTexture);
        return {
            activeTexture, textures,
            program: gl.getParameter(gl.CURRENT_PROGRAM),
            vao: gl.getParameter(gl.VERTEX_ARRAY_BINDING),
            array: gl.getParameter(gl.ARRAY_BUFFER_BINDING),
            readFramebuffer: gl.getParameter(gl.READ_FRAMEBUFFER_BINDING),
            drawFramebuffer: gl.getParameter(gl.DRAW_FRAMEBUFFER_BINDING),
            renderbuffer: gl.getParameter(gl.RENDERBUFFER_BINDING),
            viewport: gl.getParameter(gl.VIEWPORT),
            clearColor: gl.getParameter(gl.COLOR_CLEAR_VALUE),
            blend: gl.isEnabled(gl.BLEND),
            cull: gl.isEnabled(gl.CULL_FACE),
            cullMode: gl.getParameter(gl.CULL_FACE_MODE),
            sourceRGB: gl.getParameter(gl.BLEND_SRC_RGB),
            destinationRGB: gl.getParameter(gl.BLEND_DST_RGB),
            sourceAlpha: gl.getParameter(gl.BLEND_SRC_ALPHA),
            destinationAlpha: gl.getParameter(gl.BLEND_DST_ALPHA),
        };
    }

    function restoreState(gl, state) {
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, state.readFramebuffer);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, state.drawFramebuffer);
        gl.bindRenderbuffer(gl.RENDERBUFFER, state.renderbuffer);
        gl.viewport(...state.viewport);
        gl.clearColor(...state.clearColor);
        gl.useProgram(state.program);
        gl.bindVertexArray(state.vao);
        gl.bindBuffer(gl.ARRAY_BUFFER, state.array);
        for (let unit = 0; unit < 2; ++unit) {
            gl.activeTexture(gl.TEXTURE0 + unit);
            gl.bindTexture(gl.TEXTURE_2D, state.textures[unit]);
        }
        gl.activeTexture(state.activeTexture);
        if (state.blend) gl.enable(gl.BLEND); else gl.disable(gl.BLEND);
        if (state.cull) gl.enable(gl.CULL_FACE); else gl.disable(gl.CULL_FACE);
        gl.cullFace(state.cullMode);
        gl.blendFuncSeparate(state.sourceRGB, state.destinationRGB,
                             state.sourceAlpha, state.destinationAlpha);
    }

    class Renderer {
        constructor(gl, drawables) {
            this.gl = gl;
            this.source = drawables;
            this.normal = null;
            this.masked = null;
            this.vao = null;
            this.positions = null;
            this.uvs = null;
            this.indices = null;
            this.maskFramebuffer = null;
            this.maskTexture = null;
            this.maskDepth = null;
            this.maskWidth = 0;
            this.maskHeight = 0;
            this.entries = [];
            this.order = [];
            const state = saveState(gl);
            try {
                this.normal = program(gl, fragmentSource);
                this.masked = program(gl, maskedFragmentSource);
                let vertexCount = 0;
                let indexCount = 0;
                for (let i = 0; i < drawables.count; ++i) {
                    this.entries.push({
                        vertexOffset: vertexCount,
                        indexOffset: indexCount,
                        vertexCount: drawables.vertexCounts[i],
                        indexCount: drawables.indexCounts[i],
                    });
                    this.order.push(i);
                    vertexCount += drawables.vertexCounts[i];
                    indexCount += drawables.indexCounts[i];
                }
                this.indexType = vertexCount > 65536 ? gl.UNSIGNED_INT : gl.UNSIGNED_SHORT;
                this.indexBytes = vertexCount > 65536 ? 4 : 2;
                const uvData = new Float32Array(vertexCount * 2);
                const indexData = vertexCount > 65536 ? new Uint32Array(indexCount) : new Uint16Array(indexCount);
                for (let i = 0; i < this.entries.length; ++i) {
                    const entry = this.entries[i];
                    uvData.set(drawables.vertexUvs[i], entry.vertexOffset * 2);
                    for (let j = 0; j < entry.indexCount; ++j)
                        indexData[entry.indexOffset + j] = entry.vertexOffset + drawables.indices[i][j];
                }
                this.vao = gl.createVertexArray();
                gl.bindVertexArray(this.vao);
                this.positions = gl.createBuffer();
                gl.bindBuffer(gl.ARRAY_BUFFER, this.positions);
                gl.bufferData(gl.ARRAY_BUFFER, vertexCount * 8, gl.DYNAMIC_DRAW);
                gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 8, 0);
                gl.enableVertexAttribArray(0);
                this.uvs = gl.createBuffer();
                gl.bindBuffer(gl.ARRAY_BUFFER, this.uvs);
                gl.bufferData(gl.ARRAY_BUFFER, uvData, gl.STATIC_DRAW);
                gl.vertexAttribPointer(1, 2, gl.FLOAT, false, 8, 0);
                gl.enableVertexAttribArray(1);
                this.indices = gl.createBuffer();
                gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.indices);
                gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, indexData, gl.STATIC_DRAW);
                this.update(drawables, true);
            } catch (error) {
                this.release();
                throw error;
            } finally {
                restoreState(gl, state);
            }
        }

        update(drawables, force = false) {
            const gl = this.gl;
            const previous = gl.getParameter(gl.ARRAY_BUFFER_BINDING);
            try {
                this.source = drawables;
                gl.bindBuffer(gl.ARRAY_BUFFER, this.positions);
                let changedOrder = force;
                for (let i = 0; i < this.entries.length; ++i) {
                    const entry = this.entries[i];
                    if (force || (drawables.dynamicFlags[i] & 0x20))
                        gl.bufferSubData(gl.ARRAY_BUFFER, entry.vertexOffset * 8, drawables.vertexPositions[i]);
                    if (drawables.dynamicFlags[i] & 0x10) changedOrder = true;
                }
                if (changedOrder)
                    this.order.sort((a, b) => drawables.renderOrders[a] - drawables.renderOrders[b]);
            } finally {
                gl.bindBuffer(gl.ARRAY_BUFFER, previous);
            }
        }

        ensureMask(width, height) {
            if (this.maskWidth === width && this.maskHeight === height) return;
            const gl = this.gl;
            this.releaseMask();
            this.maskTexture = gl.createTexture();
            gl.activeTexture(gl.TEXTURE0);
            gl.bindTexture(gl.TEXTURE_2D, this.maskTexture);
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
            this.maskDepth = gl.createRenderbuffer();
            gl.bindRenderbuffer(gl.RENDERBUFFER, this.maskDepth);
            gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT16, width, height);
            this.maskFramebuffer = gl.createFramebuffer();
            gl.bindFramebuffer(gl.FRAMEBUFFER, this.maskFramebuffer);
            gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, this.maskTexture, 0);
            gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, this.maskDepth);
            if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE)
                throw new Error('Cubism mask framebuffer is incomplete');
            this.maskWidth = width;
            this.maskHeight = height;
        }

        useProgram(program, matrix, opacity) {
            const gl = this.gl;
            gl.useProgram(program.program);
            gl.uniformMatrix4fv(program.mvp, false, matrix);
            gl.uniform1f(program.opacity, opacity);
            gl.uniform1i(program.diffuse, 1);
            if (program.mask !== null) gl.uniform1i(program.mask, 0);
        }

        bindDiffuse(index, textures) {
            const gl = this.gl;
            const slot = this.source.textureIndices[index];
            if (slot < 0 || slot >= textures.length)
                throw new Error('Live2D texture index error.');
            gl.activeTexture(gl.TEXTURE1);
            gl.bindTexture(gl.TEXTURE_2D, textures[slot]);
        }

        submit(index) {
            const entry = this.entries[index];
            this.gl.drawElements(this.gl.TRIANGLES, entry.indexCount, this.indexType,
                                 entry.indexOffset * this.indexBytes);
        }

        draw(textures, width, height, matrix) {
            const gl = this.gl;
            const state = saveState(gl);
            try {
                gl.bindVertexArray(this.vao);
                gl.enable(gl.BLEND);
                for (const index of this.order) {
                    if (!(this.source.dynamicFlags[index] & 1)) continue;
                    const masks = this.source.masks[index];
                    const masked = masks.length > 0;
                    if (masked) {
                        this.ensureMask(width, height);
                        gl.bindFramebuffer(gl.FRAMEBUFFER, this.maskFramebuffer);
                        gl.viewport(0, 0, width, height);
                        gl.clearColor(0, 0, 0, 0);
                        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
                        gl.disable(gl.CULL_FACE);
                        gl.blendFuncSeparate(gl.ONE, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
                        this.useProgram(this.normal, matrix, 1);
                        for (const mask of masks) {
                            this.bindDiffuse(mask, textures);
                            this.submit(mask);
                        }
                        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, state.readFramebuffer);
                        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, state.drawFramebuffer);
                        gl.viewport(...state.viewport);
                        this.useProgram(this.masked, matrix, this.source.opacities[index]);
                        gl.uniform1i(this.masked.inverse, (this.source.constantFlags[index] & 8) !== 0);
                        gl.activeTexture(gl.TEXTURE0);
                        gl.bindTexture(gl.TEXTURE_2D, this.maskTexture);
                    } else {
                        this.useProgram(this.normal, matrix, this.source.opacities[index]);
                    }
                    this.bindDiffuse(index, textures);
                    const flags = this.source.constantFlags[index];
                    if (flags & 4) gl.disable(gl.CULL_FACE);
                    else { gl.enable(gl.CULL_FACE); gl.cullFace(gl.BACK); }
                    if (flags & 1) gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE, gl.ZERO, gl.ONE);
                    else if (flags & 2) gl.blendFuncSeparate(gl.DST_COLOR, gl.ONE_MINUS_SRC_ALPHA, gl.ZERO, gl.ONE);
                    else gl.blendFuncSeparate(gl.ONE, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
                    this.submit(index);
                }
            } finally {
                restoreState(gl, state);
            }
        }

        releaseMask() {
            const gl = this.gl;
            if (this.maskFramebuffer) gl.deleteFramebuffer(this.maskFramebuffer);
            if (this.maskDepth) gl.deleteRenderbuffer(this.maskDepth);
            if (this.maskTexture) gl.deleteTexture(this.maskTexture);
            this.maskFramebuffer = this.maskDepth = this.maskTexture = null;
            this.maskWidth = this.maskHeight = 0;
        }

        release() {
            const gl = this.gl;
            this.releaseMask();
            if (this.vao) gl.deleteVertexArray(this.vao);
            for (const buffer of [this.positions, this.uvs, this.indices])
                if (buffer) gl.deleteBuffer(buffer);
            if (this.normal) gl.deleteProgram(this.normal.program);
            if (this.masked) gl.deleteProgram(this.masked.program);
            this.vao = this.positions = this.uvs = this.indices = null;
            this.normal = this.masked = null;
        }
    }

    function projection(width, height, x, y, scaleX, scaleY) {
        return new Float32Array([
            Math.min(1, height / width) * scaleX, 0, 0, 0,
            0, Math.min(1, width / height) * scaleY, 0, 0,
            0, 0, 1, 0,
            x / (width * 0.5), y / (height * 0.5), 0, 1,
        ]);
    }

    const models = new Map();
    const animations = new Map();
    let nextHandle = 1;
    function requireModel(handle) {
        const result = models.get(handle);
        if (!result) throw new Error('Cubism model has been closed');
        return result;
    }
    function core() {
        if (typeof Live2DCubismCore === 'undefined')
            throw new Error('Live2D Cubism Core for Web is not installed');
        return Live2DCubismCore;
    }

    return {
        Renderer, projection,
        available: () => typeof Live2DCubismCore !== 'undefined',
        create(bytes) {
            const library = core();
            const moc = library.Moc.fromArrayBuffer(bytes);
            if (!moc) throw new Error('Cubism could not load MOC3');
            const model = library.Model.fromMoc(moc);
            if (!model) { moc._release(); throw new Error('Cubism could not create model'); }
            model.update();
            const handle = nextHandle++;
            models.set(handle, { moc, model, renderer: null, context: null, controller: null });
            return handle;
        },
        model(handle) { return requireModel(handle).model; },
        configure(handle, settings) {
            const entry = requireModel(handle);
            entry.controller = new krkrCubismController.Controller(entry.model, settings);
        },
        controller(handle) {
            const result = requireModel(handle).controller;
            if (!result) throw new Error('Cubism model settings are not loaded');
            return result;
        },
        createAnimation(document) {
            const handle = nextHandle++;
            animations.set(handle, new krkrCubismAnimation.Animation(document));
            return handle;
        },
        releaseAnimation(handle) { animations.delete(handle); },
        applyAnimation(handle, animationHandle, delta, weight) {
            const animation = animations.get(animationHandle);
            if (animation) animation.apply(requireModel(handle).model, delta, weight);
        },
        update(handle, delta, options, voicePeak) {
            const entry = requireModel(handle);
            if (entry.controller && options) entry.controller.update(delta, options, voicePeak);
            entry.model.update();
            if (entry.renderer) entry.renderer.update(entry.model.drawables);
            entry.model.drawables.resetDynamicFlags();
        },
        draw(handle, gl, textures, width, height, x, y, scaleX, scaleY) {
            const entry = requireModel(handle);
            if (entry.context && entry.context !== gl)
                throw new Error('Cubism model belongs to a different WebGL context');
            if (!entry.renderer) {
                entry.renderer = new Renderer(gl, entry.model.drawables);
                entry.context = gl;
            }
            entry.renderer.draw(textures, width, height, projection(width, height, x, y, scaleX, scaleY));
        },
        hitTest(handle, name, x, y, width, height, positionX, positionY, scaleX, scaleY) {
            const model = requireModel(handle).model;
            const index = model.drawables.ids.indexOf(name);
            if (index < 0 || width <= 0 || height <= 0) return false;
            const points = model.drawables.vertexPositions[index];
            let left = model.canvasinfo.CanvasWidth, bottom = model.canvasinfo.CanvasHeight, right = 0, top = 0;
            for (let i = 0; i < points.length; i += 2) {
                left = Math.min(left, points[i]); right = Math.max(right, points[i]);
                bottom = Math.min(bottom, points[i + 1]); top = Math.max(top, points[i + 1]);
            }
            const matrix = projection(width, height, positionX, positionY, scaleX, scaleY);
            const modelX = ((x - width * 0.5) / (width * 0.5) - matrix[12]) / matrix[0];
            const modelY = ((height - 1 - y - height * 0.5) / (height * 0.5) - matrix[13]) / matrix[5];
            return modelX >= left && modelX <= right && modelY >= bottom && modelY <= top;
        },
        uninitialize() {
            for (const entry of models.values()) {
                if (entry.renderer) entry.renderer.release();
                entry.renderer = entry.context = null;
            }
        },
        release(handle) {
            const entry = models.get(handle);
            if (!entry) return;
            if (entry.renderer) entry.renderer.release();
            entry.model.release();
            entry.moc._release();
            models.delete(handle);
        },
    };
})();

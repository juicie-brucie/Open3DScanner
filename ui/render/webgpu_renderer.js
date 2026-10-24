// WebGPU point-cloud renderer. Implements the same interface as the
// WebGL2 fallback. Main cloud uses per-vertex density colours (uploaded via
// uploadDensityColors); overlay path kept for compatibility.
(function () {
  "use strict";

  async function createWebGPURenderer(canvas) {
    if (!navigator.gpu) return null;
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) return null;
    const device = await adapter.requestDevice();
    const format = navigator.gpu.getPreferredCanvasFormat();

    const shader = device.createShaderModule({
      code: `
        struct Uniforms {
          matrix: mat4x4<f32>,
          brightness: f32,
          overrideFlag: f32,
          useDensity: f32,
          // implicit 4-byte pad before vec3 (align 16)
          color: vec3<f32>,
        };
        @group(0) @binding(0) var<uniform> u: Uniforms;
        struct VSOut {
          @builtin(position) pos: vec4<f32>,
          @location(0) depth: f32,
          @location(1) color: vec3<f32>,
        };
        @vertex fn vs(
          @location(0) a_position: vec3<f32>,
          @location(1) a_color: vec3<f32>
        ) -> VSOut {
          var out: VSOut;
          out.pos = u.matrix * vec4<f32>(a_position, 1.0);
          // WebGPU clip space is z in [0,1]; the shared WebGL-style
          // perspective maps to [-1,1]. Remap so nothing is silently culled.
          out.pos.z = out.pos.z * 0.5 + out.pos.w * 0.5;
          out.depth = a_position.z;
          out.color = a_color;
          return out;
        }
        @fragment fn fs(in: VSOut) -> @location(0) vec4<f32> {
          var base: vec3<f32>;
          if (u.useDensity > 0.5) {
            base = in.color * u.brightness;
          } else {
            let t = clamp((in.depth + 1.0) * 0.5, 0.0, 1.0);
            let near = vec3<f32>(1.0, 0.85, 0.35);
            let far = vec3<f32>(0.25, 0.55, 1.0);
            base = mix(far, near, t) * u.brightness;
          }
          if (u.overrideFlag > 0.5) { base = u.color; }
          return vec4<f32>(base, 1.0);
        }
      `,
    });
    // Explicit bind group layout shared by both pipelines so the same
    // bind group can be used regardless of which pipeline is active.
    const UNIFORM_SIZE = 96;
    const uniformsBindGroupLayout = device.createBindGroupLayout({
      entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
        buffer: { type: "uniform", minBindingSize: UNIFORM_SIZE } }],
    });
    const pipelineLayout = device.createPipelineLayout({ bindGroupLayouts: [uniformsBindGroupLayout] });

    const depthStencilState = { format: "depth24plus", depthWriteEnabled: true, depthCompare: "less-equal" };
    // Density pipeline: per-vertex colour (vertex step mode for both buffers).
    const pipeline = device.createRenderPipeline({
      layout: pipelineLayout,
      vertex: { module: shader, entryPoint: "vs", buffers: [
        { arrayStride: 12, stepMode: "vertex",   attributes: [{ shaderLocation: 0, offset: 0, format: "float32x3" }] },
        { arrayStride: 12, stepMode: "vertex",   attributes: [{ shaderLocation: 1, offset: 0, format: "float32x3" }] },
      ] },
      fragment: { module: shader, entryPoint: "fs", targets: [{ format }] },
      primitive: { topology: "point-list" },
      // less-equal so the overlay drawn second at the same depth paints over
      // the cloud instead of being rejected by the equal-depth tie.
      depthStencil: depthStencilState,
    });
    // Fallback pipeline: colour buffer uses instance step mode so it only needs
    // 12 bytes (1 instance) regardless of vertex count. Used for the full mesh
    // path where density colours are not computed (avoids a ~75 MB allocation).
    const fallbackPipeline = device.createRenderPipeline({
      layout: pipelineLayout,
      vertex: { module: shader, entryPoint: "vs", buffers: [
        { arrayStride: 12, stepMode: "vertex",   attributes: [{ shaderLocation: 0, offset: 0, format: "float32x3" }] },
        { arrayStride: 12, stepMode: "instance", attributes: [{ shaderLocation: 1, offset: 0, format: "float32x3" }] },
      ] },
      fragment: { module: shader, entryPoint: "fs", targets: [{ format }] },
      primitive: { topology: "point-list" },
      depthStencil: depthStencilState,
    });
    // 12-byte colour buffer for the fallback path (value is ignored by shader
    // since useDensity=0 → depth gradient is used instead).
    const instanceColorBuf = device.createBuffer({ size: 16, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
    device.queue.writeBuffer(instanceColorBuf, 0, new Float32Array([0, 0, 0]));

    // Line-list pipeline for the world-origin XYZ triad. Same shader/layout;
    // per-vertex colours (useDensity=1) carry the axis colours.
    const linePipeline = device.createRenderPipeline({
      layout: pipelineLayout,
      vertex: { module: shader, entryPoint: "vs", buffers: [
        { arrayStride: 12, stepMode: "vertex", attributes: [{ shaderLocation: 0, offset: 0, format: "float32x3" }] },
        { arrayStride: 12, stepMode: "vertex", attributes: [{ shaderLocation: 1, offset: 0, format: "float32x3" }] },
      ] },
      fragment: { module: shader, entryPoint: "fs", targets: [{ format }] },
      primitive: { topology: "line-list" },
      depthStencil: depthStencilState,
    });
    const axisColorBuf = device.createBuffer({ size: 72, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
    device.queue.writeBuffer(axisColorBuf, 0, new Float32Array([
      0.95, 0.26, 0.21, 0.95, 0.26, 0.21,  // X — red
      0.30, 0.80, 0.40, 0.30, 0.80, 0.40,  // Y — green
      0.33, 0.55, 0.95, 0.33, 0.55, 0.95,  // Z — blue
    ]));
    const axisPosBuf = device.createBuffer({ size: 72, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });

    // 96-byte uniform block: matrix (64), brightness (4), override (4),
    // useDensity (4), pad (4), color (12). Two blocks so an overlay draw
    // can use a flat color without re-writing the main buffer mid-pass.
    function makeBindGroup() {
      const buffer = device.createBuffer({ size: UNIFORM_SIZE, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
      const bindGroup = device.createBindGroup({
        layout: uniformsBindGroupLayout,
        entries: [{ binding: 0, resource: { buffer } }],
      });
      return { buffer, bindGroup };
    }
    const mainUniforms = makeBindGroup();
    const overlayUniforms = makeBindGroup();
    const axisUniforms = makeBindGroup();

    function writeUniforms(target, matrix, brightness, override, useDensity, r, g, b) {
      const u = new Float32Array(UNIFORM_SIZE / 4);
      u.set(matrix, 0);
      u[16] = brightness;
      u[17] = override;
      u[18] = useDensity;
      // u[19] implicit pad
      u[20] = r; u[21] = g; u[22] = b;
      device.queue.writeBuffer(target.buffer, 0, u);
    }

    function makePointBuffer() {
      return { buffer: null, capacity: 0 };
    }
    function uploadTo(target, data) {
      const bytes = data.byteLength;
      if (!target.buffer || bytes > target.capacity) {
        if (target.buffer) target.buffer.destroy();
        target.capacity = Math.max(bytes, 12 * 1024);
        target.buffer = device.createBuffer({ size: target.capacity, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
      }
      device.queue.writeBuffer(target.buffer, 0, data);
    }
    const main = makePointBuffer();
    const overlay = makePointBuffer();
    const densityBuf = makePointBuffer();
    let hasDensityColors = false;

    let depthTexture = null;
    let width = 1, height = 1;

    function ensureDepth() {
      if (depthTexture && depthTexture.width === width && depthTexture.height === height) return;
      if (depthTexture) depthTexture.destroy();
      depthTexture = device.createTexture({ size: [width, height], format: "depth24plus",
        usage: GPUTextureUsage.RENDER_ATTACHMENT });
    }

    // Canvas is claimed only after every other fallible step.
    const context = canvas.getContext("webgpu");
    if (!context) return null;
    context.configure({ device, format, alphaMode: "opaque" });

    return {
      name: "webgpu",
      resize(w, h) { width = w; height = h; ensureDepth(); },
      upload(positions) { uploadTo(main, positions); },
      uploadDensityColors(colors) {
        uploadTo(densityBuf, colors);
        hasDensityColors = true;
      },
      resetDensityColors() {
        hasDensityColors = false;
        // densityBuf is left intact; fallbackPipeline uses instanceColorBuf instead.
      },
      uploadOverlay(positions) { uploadTo(overlay, positions); },
      draw(matrix, brightness, count, overlayCount = 0) {
        ensureDepth();
        writeUniforms(mainUniforms, matrix, brightness, 0.0, hasDensityColors ? 1.0 : 0.0, 0, 0, 0);
        writeUniforms(overlayUniforms, matrix, 1.0, 1.0, 0.0, 1.0, 0.15, 0.1);
        const activePipeline = hasDensityColors ? pipeline : fallbackPipeline;
        const colorBuf = hasDensityColors ? densityBuf.buffer : instanceColorBuf;
        const encoder = device.createCommandEncoder();
        const pass = encoder.beginRenderPass({
          colorAttachments: [{ view: context.getCurrentTexture().createView(),
            clearValue: { r: 0.07, g: 0.07, b: 0.08, a: 1.0 }, loadOp: "clear", storeOp: "store" }],
          depthStencilAttachment: { view: depthTexture.createView(), depthClearValue: 1.0,
            depthLoadOp: "clear", depthStoreOp: "store" },
        });
        pass.setPipeline(activePipeline);
        if (count > 0 && main.buffer) {
          pass.setBindGroup(0, mainUniforms.bindGroup);
          pass.setVertexBuffer(0, main.buffer);
          pass.setVertexBuffer(1, colorBuf);
          pass.draw(count, 1, 0, 0);
        }
        if (overlayCount > 0 && overlay.buffer) {
          pass.setBindGroup(0, overlayUniforms.bindGroup);
          pass.setVertexBuffer(0, overlay.buffer);
          pass.setVertexBuffer(1, colorBuf); // not used (overrideFlag=1) but pipeline requires it
          pass.draw(overlayCount, 1, 0, 0);
        }
        pass.end();
        device.queue.submit([encoder.finish()]);
      },
      // World-origin XYZ triad (X red, Y green, Z blue), each `length` units
      // long. A second render pass that loads the existing colour/depth so the
      // lines sit over the cloud already drawn by draw() this frame.
      drawAxes(matrix, length) {
        if (!depthTexture) return;
        const L = length > 0 ? length : 1.0;
        device.queue.writeBuffer(axisPosBuf, 0, new Float32Array([
          0, 0, 0, L, 0, 0, 0, 0, 0, 0, L, 0, 0, 0, 0, 0, 0, L]));
        writeUniforms(axisUniforms, matrix, 1.0, 0.0, 1.0, 0, 0, 0);
        const encoder = device.createCommandEncoder();
        const pass = encoder.beginRenderPass({
          colorAttachments: [{ view: context.getCurrentTexture().createView(),
            loadOp: "load", storeOp: "store" }],
          depthStencilAttachment: { view: depthTexture.createView(),
            depthLoadOp: "load", depthStoreOp: "store" },
        });
        pass.setPipeline(linePipeline);
        pass.setBindGroup(0, axisUniforms.bindGroup);
        pass.setVertexBuffer(0, axisPosBuf);
        pass.setVertexBuffer(1, axisColorBuf);
        pass.draw(6, 1, 0, 0);
        pass.end();
        device.queue.submit([encoder.finish()]);
      },
    };
  }

  window.createWebGPURenderer = createWebGPURenderer;
})();

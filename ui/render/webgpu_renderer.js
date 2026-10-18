// WebGPU point-cloud renderer. Implements the same interface as the
// WebGL2 fallback; selected when navigator.gpu is available.
(function () {
  "use strict";

  async function createWebGPURenderer(canvas) {
    if (!navigator.gpu) return null;
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) return null;
    const device = await adapter.requestDevice();
    const context = canvas.getContext("webgpu");
    if (!context) return null;
    const format = navigator.gpu.getPreferredCanvasFormat();
    context.configure({ device, format, alphaMode: "opaque" });

    const shader = device.createShaderModule({
      code: `
        struct Uniforms { matrix: mat4x4<f32>, brightness: f32, };
        @group(0) @binding(0) var<uniform> u: Uniforms;
        struct VSOut { @builtin(position) pos: vec4<f32>, @location(0) depth: f32, };
        @vertex fn vs(@location(0) a_position: vec3<f32>) -> VSOut {
          var out: VSOut;
          out.pos = u.matrix * vec4<f32>(a_position, 1.0);
          out.depth = a_position.z;
          return out;
        }
        @fragment fn fs(in: VSOut) -> @location(0) vec4<f32> {
          let t = clamp((in.depth + 1.0) * 0.5, 0.0, 1.0);
          let near = vec3<f32>(1.0, 0.85, 0.35);
          let far = vec3<f32>(0.25, 0.55, 1.0);
          return vec4<f32>(mix(far, near, t) * u.brightness, 1.0);
        }
      `,
    });
    const pipeline = device.createRenderPipeline({
      layout: "auto",
      vertex: { module: shader, entryPoint: "vs", buffers: [{
        arrayStride: 12,
        attributes: [{ shaderLocation: 0, offset: 0, format: "float32x3" }],
      }] },
      fragment: { module: shader, entryPoint: "fs", targets: [{ format }] },
      primitive: { topology: "point-list" },
      depthStencil: { format: "depth24plus", depthWriteEnabled: true, depthCompare: "less" },
    });

    const uniformBuffer = device.createBuffer({ size: 80, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    const bindGroup = device.createBindGroup({
      layout: pipeline.getBindGroupLayout(0),
      entries: [{ binding: 0, resource: { buffer: uniformBuffer } }],
    });

    let vertexBuffer = null;
    let vertexCapacity = 0;
    let depthTexture = null;
    let width = 1, height = 1;

    function ensureDepth() {
      if (depthTexture && depthTexture.width === width && depthTexture.height === height) return;
      if (depthTexture) depthTexture.destroy();
      depthTexture = device.createTexture({ size: [width, height], format: "depth24plus",
        usage: GPUTextureUsage.RENDER_ATTACHMENT });
    }

    return {
      name: "webgpu",
      resize(w, h) { width = w; height = h; ensureDepth(); },
      upload(positions) {
        const bytes = positions.byteLength;
        if (!vertexBuffer || bytes > vertexCapacity) {
          if (vertexBuffer) vertexBuffer.destroy();
          vertexCapacity = Math.max(bytes, 12 * 1024);
          vertexBuffer = device.createBuffer({ size: vertexCapacity, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
        }
        device.queue.writeBuffer(vertexBuffer, 0, positions);
      },
      draw(matrix, brightness, count) {
        ensureDepth();
        const uniforms = new Float32Array(20);
        uniforms.set(matrix, 0);
        uniforms[16] = brightness;
        device.queue.writeBuffer(uniformBuffer, 0, uniforms);
        const encoder = device.createCommandEncoder();
        const pass = encoder.beginRenderPass({
          colorAttachments: [{ view: context.getCurrentTexture().createView(),
            clearValue: { r: 0.07, g: 0.07, b: 0.08, a: 1.0 }, loadOp: "clear", storeOp: "store" }],
          depthStencilAttachment: { view: depthTexture.createView(), depthClearValue: 1.0,
            depthLoadOp: "clear", depthStoreOp: "store" },
        });
        if (count > 0 && vertexBuffer) {
          pass.setPipeline(pipeline);
          pass.setBindGroup(0, bindGroup);
          pass.setVertexBuffer(0, vertexBuffer);
          pass.draw(count, 1, 0, 0);
        }
        pass.end();
        device.queue.submit([encoder.finish()]);
      },
    };
  }

  window.createWebGPURenderer = createWebGPURenderer;
})();

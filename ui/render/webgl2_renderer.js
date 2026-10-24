// WebGL2 point-cloud renderer (the fallback path).
// Implements the same interface as webgpu_renderer.js.
// Main cloud uses per-vertex density colours (red→orange→green→blue by voxel
// hit count, uploaded via uploadDensityColors). Overlay path kept for
// compatibility but not used while density colouring is active.
(function () {
  "use strict";

  function createWebGL2Renderer(canvas) {
    const gl = canvas.getContext("webgl2");
    if (!gl) return null;

    const vertexSrc = `#version 300 es
      layout(location = 0) in vec3 a_position;
      layout(location = 1) in vec3 a_color;
      uniform mat4 u_matrix;
      out vec3 v_color;
      out float v_depth;
      void main() {
        gl_Position = u_matrix * vec4(a_position, 1.0);
        gl_PointSize = 2.5;
        v_depth = a_position.z;
        v_color = a_color;
      }
    `;
    const fragmentSrc = `#version 300 es
      precision mediump float;
      in vec3 v_color;
      in float v_depth;
      uniform float u_brightness;
      uniform float u_override;
      uniform vec3 u_override_color;
      uniform float u_use_density;
      out vec4 outColor;
      void main() {
        vec3 base;
        if (u_use_density > 0.5) {
          base = v_color * u_brightness;
        } else {
          float t = clamp((v_depth + 1.0) * 0.5, 0.0, 1.0);
          vec3 near = vec3(1.0, 0.85, 0.35);
          vec3 far = vec3(0.25, 0.55, 1.0);
          base = mix(far, near, t) * u_brightness;
        }
        if (u_override > 0.5) base = u_override_color;
        outColor = vec4(base, 1.0);
      }
    `;
    function compile(type, source) {
      const s = gl.createShader(type);
      gl.shaderSource(s, source);
      gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
      return s;
    }
    const program = gl.createProgram();
    gl.attachShader(program, compile(gl.VERTEX_SHADER, vertexSrc));
    gl.attachShader(program, compile(gl.FRAGMENT_SHADER, fragmentSrc));
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(program));
    const matrixLocation = gl.getUniformLocation(program, "u_matrix");
    const brightnessLocation = gl.getUniformLocation(program, "u_brightness");
    const overrideLocation = gl.getUniformLocation(program, "u_override");
    const overrideColorLocation = gl.getUniformLocation(program, "u_override_color");
    const useDensityLocation = gl.getUniformLocation(program, "u_use_density");

    // Main VAO: two attribute buffers — position (loc 0) and density colour (loc 1).
    function makeMainVao() {
      const posBuffer = gl.createBuffer();
      const colorBuffer = gl.createBuffer();
      const vao = gl.createVertexArray();
      gl.bindVertexArray(vao);
      gl.bindBuffer(gl.ARRAY_BUFFER, posBuffer);
      gl.enableVertexAttribArray(0);
      gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, colorBuffer);
      gl.enableVertexAttribArray(1);
      gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);
      gl.bindVertexArray(null);
      return { posBuffer, colorBuffer, vao };
    }
    // Overlay VAO: position only (flat colour via uniform, loc 1 unused).
    function makeVao() {
      const buffer = gl.createBuffer();
      const vao = gl.createVertexArray();
      gl.bindVertexArray(vao);
      gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
      gl.enableVertexAttribArray(0);
      gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
      gl.bindVertexArray(null);
      return { buffer, vao };
    }
    const main = makeMainVao();
    const overlay = makeVao();
    // Axis triad VAO (position loc 0 + colour loc 1), drawn as GL lines from
    // the world origin so the cloud always has a fixed XYZ reference. Six
    // vertices (three segments); geometry is re-uploaded per draw with the
    // caller's length, which is a trivial 36-float write.
    const axes = makeMainVao();
    const AXIS_COLORS = new Float32Array([
      0.95, 0.26, 0.21, 0.95, 0.26, 0.21,  // X — red
      0.30, 0.80, 0.40, 0.30, 0.80, 0.40,  // Y — green
      0.33, 0.55, 0.95, 0.33, 0.55, 0.95,  // Z — blue
    ]);
    gl.bindVertexArray(axes.vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, axes.colorBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, AXIS_COLORS, gl.STATIC_DRAW);
    gl.bindVertexArray(null);
    let hasDensityColors = false;
    gl.enable(gl.DEPTH_TEST);
    gl.depthFunc(gl.LEQUAL);
    gl.clearColor(0.07, 0.07, 0.08, 1.0);

    return {
      name: "webgl2",
      resize(w, h) { gl.viewport(0, 0, w, h); },
      upload(positions) {
        gl.bindBuffer(gl.ARRAY_BUFFER, main.posBuffer);
        gl.bufferData(gl.ARRAY_BUFFER, positions, gl.DYNAMIC_DRAW);
      },
      uploadDensityColors(colors) {
        gl.bindBuffer(gl.ARRAY_BUFFER, main.colorBuffer);
        gl.bufferData(gl.ARRAY_BUFFER, colors, gl.DYNAMIC_DRAW);
        hasDensityColors = true;
      },
      resetDensityColors() {
        // Disable the colour attribute so the full-mesh draw uses depth gradient
        // without needing a colour buffer sized to the full point count.
        gl.bindVertexArray(main.vao);
        gl.disableVertexAttribArray(1);
        gl.bindVertexArray(null);
        hasDensityColors = false;
      },
      uploadOverlay(positions) {
        gl.bindBuffer(gl.ARRAY_BUFFER, overlay.buffer);
        gl.bufferData(gl.ARRAY_BUFFER, positions, gl.DYNAMIC_DRAW);
      },
      draw(matrix, brightness, count, overlayCount = 0) {
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.useProgram(program);
        gl.uniformMatrix4fv(matrixLocation, false, matrix);
        if (count > 0) {
          gl.bindVertexArray(main.vao);
          gl.uniform1f(brightnessLocation, brightness);
          gl.uniform1f(overrideLocation, 0.0);
          gl.uniform1f(useDensityLocation, hasDensityColors ? 1.0 : 0.0);
          gl.drawArrays(gl.POINTS, 0, count);
        }
        if (overlayCount > 0) {
          gl.bindVertexArray(overlay.vao);
          gl.uniform1f(brightnessLocation, 1.0);
          gl.uniform1f(overrideLocation, 1.0);
          gl.uniform1f(useDensityLocation, 0.0);
          gl.uniform3f(overrideColorLocation, 1.0, 0.15, 0.1);
          gl.drawArrays(gl.POINTS, 0, overlayCount);
        }
        gl.bindVertexArray(null);
      },
      // The world-origin XYZ triad: X red, Y green, Z blue, each `length`
      // world units long. Drawn after the cloud (same matrix, depth test on)
      // so it sits in the scene rather than as a flat overlay.
      drawAxes(matrix, length) {
        const L = length > 0 ? length : 1.0;
        const pos = new Float32Array([0, 0, 0, L, 0, 0, 0, 0, 0, 0, L, 0, 0, 0, 0, 0, 0, L]);
        gl.bindBuffer(gl.ARRAY_BUFFER, axes.posBuffer);
        gl.bufferData(gl.ARRAY_BUFFER, pos, gl.DYNAMIC_DRAW);
        gl.useProgram(program);
        gl.uniformMatrix4fv(matrixLocation, false, matrix);
        gl.bindVertexArray(axes.vao);
        gl.uniform1f(brightnessLocation, 1.0);
        gl.uniform1f(overrideLocation, 0.0);
        gl.uniform1f(useDensityLocation, 1.0);  // per-vertex axis colours
        gl.drawArrays(gl.LINES, 0, 6);
        gl.bindVertexArray(null);
      },
    };
  }

  window.createWebGL2Renderer = createWebGL2Renderer;
})();

// WebGL2 point-cloud renderer (the fallback path).
// Implements the same interface as webgpu_renderer.js.
(function () {
  "use strict";

  function createWebGL2Renderer(canvas) {
    const gl = canvas.getContext("webgl2");
    if (!gl) return null;

    const vertexSrc = `#version 300 es
      layout(location = 0) in vec3 a_position;
      uniform mat4 u_matrix;
      out float v_depth;
      void main() {
        gl_Position = u_matrix * vec4(a_position, 1.0);
        gl_PointSize = 2.5;
        v_depth = a_position.z;
      }
    `;
    const fragmentSrc = `#version 300 es
      precision mediump float;
      in float v_depth;
      uniform float u_brightness;
      out vec4 outColor;
      void main() {
        float t = clamp((v_depth + 1.0) * 0.5, 0.0, 1.0);
        vec3 near = vec3(1.0, 0.85, 0.35);
        vec3 far = vec3(0.25, 0.55, 1.0);
        outColor = vec4(mix(far, near, t) * u_brightness, 1.0);
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

    const positionBuffer = gl.createBuffer();
    const vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
    gl.enable(gl.DEPTH_TEST);
    gl.clearColor(0.07, 0.07, 0.08, 1.0);

    return {
      name: "webgl2",
      resize(w, h) { gl.viewport(0, 0, w, h); },
      upload(positions) { gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer); gl.bufferData(gl.ARRAY_BUFFER, positions, gl.DYNAMIC_DRAW); },
      draw(matrix, brightness, count) {
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        if (count <= 0) return;
        gl.useProgram(program);
        gl.bindVertexArray(vao);
        gl.uniformMatrix4fv(matrixLocation, false, matrix);
        gl.uniform1f(brightnessLocation, brightness);
        gl.drawArrays(gl.POINTS, 0, count);
      },
    };
  }

  window.createWebGL2Renderer = createWebGL2Renderer;
})();

// OpenScanner web UI — minimal, dependency-free WebGL2 point-cloud viewer.
// No framework, no build step, no animation beyond the point cloud data
// itself: this exists to prove and measure the render/latency pipeline,
// not to look polished yet.

(function () {
  "use strict";

  const params = new URLSearchParams(window.location.search);
  const port = params.get("port") || "8787";
  const host = params.get("host") || window.location.hostname || "127.0.0.1";
  const wsUrl = "ws://" + host + ":" + port + "/";

  const hud = document.getElementById("hud");
  const canvas = document.getElementById("gl");
  const gl = canvas.getContext("webgl2");
  if (!gl) {
    hud.textContent = "WebGL2 is not available in this browser.";
    return;
  }

  function resize() {
    const dpr = window.devicePixelRatio || 1;
    canvas.width = Math.floor(window.innerWidth * dpr);
    canvas.height = Math.floor(window.innerHeight * dpr);
    gl.viewport(0, 0, canvas.width, canvas.height);
  }
  window.addEventListener("resize", resize);
  resize();

  // --- Shaders ------------------------------------------------------------

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

  function compileShader(type, source) {
    const shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
      throw new Error("Shader compile error: " + gl.getShaderInfoLog(shader));
    }
    return shader;
  }

  const program = gl.createProgram();
  gl.attachShader(program, compileShader(gl.VERTEX_SHADER, vertexSrc));
  gl.attachShader(program, compileShader(gl.FRAGMENT_SHADER, fragmentSrc));
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
    throw new Error("Program link error: " + gl.getProgramInfoLog(program));
  }
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

  // --- Minimal matrix math (no library needed for this) -------------------

  function perspective(fovyRad, aspect, near, far) {
    const f = 1.0 / Math.tan(fovyRad / 2);
    const nf = 1 / (near - far);
    return new Float32Array([
      f / aspect, 0, 0, 0,
      0, f, 0, 0,
      0, 0, (far + near) * nf, -1,
      0, 0, 2 * far * near * nf, 0,
    ]);
  }

  function multiply(a, b) {
    const out = new Float32Array(16);
    for (let row = 0; row < 4; row++) {
      for (let col = 0; col < 4; col++) {
        let sum = 0;
        for (let k = 0; k < 4; k++) sum += a[k * 4 + row] * b[col * 4 + k];
        out[col * 4 + row] = sum;
      }
    }
    return out;
  }

  function translation(x, y, z) {
    return new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1]);
  }

  // --- WebSocket data stream -----------------------------------------------

  let pointCount = 0;
  let lastFrameId = -1;
  let framesReceived = 0;
  let bytesReceived = 0;
  // Accumulator for the on-demand full-detail final mesh: while a full-mesh
  // transfer is in progress, incoming binary frames are appended here
  // instead of replacing the live view, and rendered once it completes.
  let fullMeshAccum = null;
  let fullMeshReceived = 0;
  let fullMeshTotal = 0;

  // Real scan data (mm-scale, arbitrary origin) has a totally different
  // scale/offset than the synthetic demo sphere (unit-radius, centered at
  // the origin) the fixed camera below was tuned for. Auto-fit to each
  // incoming frame's bounding sphere so both sources render visibly
  // instead of real data showing as a tiny off-center speck.
  let viewCenter = [0, 0, 0];
  let viewDistance = 4.2;
  let viewRadius = 1.0;

  // Driven by the server's status messages (see handleStatus below), not
  // read locally: "zoom" here means the scanner's own field of view
  // (ObScanFov in obscan_api.h — a real two-position switch on the actual
  // hardware, Normal/Wide, not a continuous control), so the camera
  // always reflects whatever the server says is actually configured
  // rather than a value chosen client-side.
  let fovDeg = 45;
  function fovRad() {
    return (fovDeg * Math.PI) / 180;
  }

  // Also server-driven, from the exposure/brightness control (see
  // handleStatus): maps the scanner's combined exposure/laser-current
  // value (or the synthetic demo's stand-in for it) onto a multiplier
  // applied to rendered point color, so the control has a visible effect
  // even though this viewer isn't literally re-rendering a camera image.
  let brightness = 1.0;

  function updateViewFit(positions, count) {
    if (count === 0) return;
    // Mean + RMS spread rather than a min/max bounding box: a real
    // reconstructed mesh can have a handful of far stray vertices, and a
    // pure min/max box lets a few outliers blow up the fit distance,
    // leaving the dense, visually-relevant cluster a tiny speck in the
    // middle of an otherwise-empty frame. RMS distance from the mean is
    // dominated by where most of the points actually are.
    let sx = 0, sy = 0, sz = 0;
    for (let i = 0; i < count; i++) {
      sx += positions[i * 3];
      sy += positions[i * 3 + 1];
      sz += positions[i * 3 + 2];
    }
    const cx = sx / count, cy = sy / count, cz = sz / count;
    let sumSq = 0;
    for (let i = 0; i < count; i++) {
      const dx = positions[i * 3] - cx, dy = positions[i * 3 + 1] - cy, dz = positions[i * 3 + 2] - cz;
      sumSq += dx * dx + dy * dy + dz * dz;
    }
    const rms = Math.sqrt(sumSq / count);
    const radius = Math.max(rms * 2.0, 0.001);  // ~2 sigma covers the dense cluster.
    viewCenter = [cx, cy, cz];
    viewRadius = radius;
    // Distance needed to fit `radius` within the vertical half-FOV, plus
    // headroom so the cloud isn't touching the viewport edges.
    viewDistance = (radius / Math.tan(fovRad() / 2)) * 1.6;
  }
  const latencySamples = [];
  const kLatencyWindow = 30;

  let connectionState = "connecting";
  let lastError = "";
  let ws = null;
  let latestStatus = null;

  function sendCommand(cmd) {
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(cmd));
    }
  }

  function connect() {
    ws = new WebSocket(wsUrl);
    ws.binaryType = "arraybuffer";

    ws.onopen = () => {
      connectionState = "connected";
    };
    ws.onclose = () => {
      connectionState = "disconnected";
      latestStatus = null;
      updateControlsFromStatus();
      setTimeout(connect, 1000); // simple retry; this is a dev tool, not a product flow yet
    };
    ws.onerror = () => {
      lastError = "connection error";
    };
    ws.onmessage = (event) => {
      if (typeof event.data === "string") {
        let msg = null;
        try { msg = JSON.parse(event.data); } catch (e) { msg = null; }
        if (msg && msg.type === "full_mesh_start") {
          fullMeshAccum = new Float32Array(msg.total * 3);
          fullMeshReceived = 0;
          fullMeshTotal = msg.total;
          return;
        }
        if (msg && msg.type === "full_mesh_done") {
          if (fullMeshAccum) {
            gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
            gl.bufferData(gl.ARRAY_BUFFER, fullMeshAccum, gl.DYNAMIC_DRAW);
            updateViewFit(fullMeshAccum, fullMeshReceived);
            pointCount = fullMeshReceived;
            lastFrameId = -1;
            framesReceived++;
            fullMeshAccum = null;
          }
          return;
        }
        handleStatusMessage(event.data);
        return;
      }
      const buf = event.data;
      bytesReceived += buf.byteLength;
      const frame = FrameProtocol.decodeCloudSnapshot(buf);
      if (!frame) return;
      const frameId = frame.seq;
      const captureTimestampMs = frame.tCapture;
      const count = frame.count;

      if (fullMeshAccum) {
        fullMeshAccum.set(frame.positions, fullMeshReceived * 3);
        fullMeshReceived += count;
        framesReceived++;
        return;
      }

      gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
      gl.bufferData(gl.ARRAY_BUFFER, frame.positions, gl.DYNAMIC_DRAW);
      updateViewFit(frame.positions, count);
      pointCount = count;
      lastFrameId = frameId;
      framesReceived++;

      const latency = Date.now() - captureTimestampMs;
      latencySamples.push(latency);
      if (latencySamples.length > kLatencyWindow) latencySamples.shift();
    };
  }
  connect();

  // --- Scan controls (start/pause/finish, exposure, FOV) -------------------
  // Purely reactive to the server's status broadcasts (see
  // tools/scan_server/main.cpp's BuildStatusJson) rather than tracking
  // local optimistic state, so the UI always shows what the server
  // actually applied — important in live mode, where e.g. a FOV change
  // can't take effect mid-scan and the server reports that back via
  // fovControllable rather than the UI just guessing.

  const btnStartPause = document.getElementById("btn-start-pause");
  const btnFinish = document.getElementById("btn-finish");
  const autoExposureCheckbox = document.getElementById("auto-exposure");
  const exposureSlider = document.getElementById("exposure");
  const exposureValueLabel = document.getElementById("exposure-value");
  const fovNormalBtn = document.getElementById("fov-normal");
  const fovWideBtn = document.getElementById("fov-wide");
  const fovNote = document.getElementById("fov-note");

  function handleStatusMessage(raw) {
    let status;
    try {
      status = JSON.parse(raw);
    } catch (e) {
      return;
    }
    if (!status || status.type !== "status") return;
    latestStatus = status;

    // FOV is a real two-position switch on the actual scanner hardware
    // (see obscan_api.h's ObScanFov) — map it to a wider/narrower camera
    // angle here rather than pretending it's a continuous zoom.
    fovDeg = status.fov === "wide" ? 75 : 45;

    const lo = status.exposureMin || 1;
    const hi = status.exposureMax || 1000;
    const t = (status.exposure - lo) / Math.max(1, hi - lo);
    brightness = 0.4 + t * 1.2; // Visible but never fully black or blown out.

    updateControlsFromStatus();
  }

  function updateControlsFromStatus() {
    const connected = connectionState === "connected" && latestStatus !== null;
    btnStartPause.disabled = !connected;
    btnFinish.disabled = !connected || latestStatus.state === "idle";
    autoExposureCheckbox.disabled = !connected;
    fovNormalBtn.disabled = !connected || !latestStatus.fovControllable;
    fovWideBtn.disabled = !connected || !latestStatus.fovControllable;

    if (!connected) {
      btnStartPause.textContent = "Start";
      exposureSlider.disabled = true;
      fovNote.textContent = "";
      return;
    }

    btnStartPause.textContent = latestStatus.state === "scanning" ? "Pause" : "Start";
    autoExposureCheckbox.checked = latestStatus.autoExposure;
    exposureSlider.min = latestStatus.exposureMin;
    exposureSlider.max = latestStatus.exposureMax;
    exposureSlider.value = latestStatus.exposure;
    exposureSlider.disabled = latestStatus.autoExposure;
    exposureValueLabel.textContent = latestStatus.exposure;

    fovNormalBtn.classList.toggle("active", latestStatus.fov === "normal");
    fovWideBtn.classList.toggle("active", latestStatus.fov === "wide");
    fovNote.textContent = latestStatus.fovControllable
      ? ""
      : "Fixed for this live session (the real scanner can't change FOV mid-scan).";
  }

  btnStartPause.addEventListener("click", () => {
    if (!latestStatus) return;
    sendCommand({ cmd: latestStatus.state === "scanning" ? "pause" : "start" });
  });
  btnFinish.addEventListener("click", () => sendCommand({ cmd: "finish" }));
  autoExposureCheckbox.addEventListener("change", () => {
    if (autoExposureCheckbox.checked) {
      sendCommand({ cmd: "set_exposure", auto: true });
    } else {
      sendCommand({ cmd: "set_exposure", auto: false, value: Number(exposureSlider.value) });
    }
  });
  exposureSlider.addEventListener("input", () => {
    exposureValueLabel.textContent = exposureSlider.value;
  });
  exposureSlider.addEventListener("change", () => {
    sendCommand({ cmd: "set_exposure", auto: false, value: Number(exposureSlider.value) });
  });
  fovNormalBtn.addEventListener("click", () => sendCommand({ cmd: "set_fov", value: "normal" }));
  fovWideBtn.addEventListener("click", () => sendCommand({ cmd: "set_fov", value: "wide" }));

  // --- Render loop (decoupled from the data rate) --------------------------

  let renderFrames = 0;
  let lastFpsSampleTime = performance.now();
  let renderFps = 0;
  let dataFps = 0;
  let lastDataFpsSampleFrames = 0;

  function draw() {
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    if (pointCount > 0) {
      const aspect = canvas.width / canvas.height;
      // Fixed 0.1/100.0 near/far planes (fine for the synthetic unit
      // sphere) clip almost all of a real scan's mm-scale geometry, since
      // viewDistance there is easily in the hundreds — scale the clip
      // planes to the fitted distance/radius instead so content at any
      // scale stays inside the frustum.
      const near = Math.max(viewDistance - viewRadius * 4, viewDistance * 0.01, 0.001);
      const far = viewDistance + viewRadius * 4;
      const proj = perspective(fovRad(), aspect, near, far);
      const view = translation(-viewCenter[0], -viewCenter[1], -viewCenter[2] - viewDistance);
      const matrix = multiply(proj, view);
      gl.useProgram(program);
      gl.bindVertexArray(vao);
      gl.uniformMatrix4fv(matrixLocation, false, matrix);
      gl.uniform1f(brightnessLocation, brightness);
      gl.drawArrays(gl.POINTS, 0, pointCount);
    }
    renderFrames++;

    const now = performance.now();
    if (now - lastFpsSampleTime >= 500) {
      renderFps = (renderFrames * 1000) / (now - lastFpsSampleTime);
      dataFps = ((framesReceived - lastDataFpsSampleFrames) * 1000) / (now - lastFpsSampleTime);
      lastDataFpsSampleFrames = framesReceived;
      renderFrames = 0;
      lastFpsSampleTime = now;
      updateHud();
    }

    requestAnimationFrame(draw);
  }

  function avgLatency() {
    if (latencySamples.length === 0) return null;
    return latencySamples.reduce((a, b) => a + b, 0) / latencySamples.length;
  }

  function updateHud() {
    const avg = avgLatency();
    const latencyText = avg === null ? "--" : avg.toFixed(1) + " ms";
    const latencyClass = avg !== null && avg > 80 ? "warn" : "ok";
    const scanState = latestStatus ? latestStatus.state : "--";
    const scanMode = latestStatus ? latestStatus.mode : "--";
    hud.innerHTML =
      '<span class="label">status</span>  <span class="value">' + connectionState + "</span>\n" +
      '<span class="label">scan</span>    <span class="value">' + scanState + " (" + scanMode + ")</span>\n" +
      '<span class="label">latency</span>  <span class="' + latencyClass + '">' + latencyText + "</span>\n" +
      '<span class="label">data</span>     <span class="value">' + dataFps.toFixed(1) + " fps</span>\n" +
      '<span class="label">render</span>   <span class="value">' + renderFps.toFixed(1) + " fps</span>\n" +
      '<span class="label">points</span>   <span class="value">' + pointCount + "</span>\n" +
      '<span class="label">frame</span>    <span class="value">#' + lastFrameId + "</span>";
  }

  requestAnimationFrame(draw);
})();

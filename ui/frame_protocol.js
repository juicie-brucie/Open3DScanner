// V1 frame protocol codec (see docs/architecture/scan-viewer.md §4).
// Shared shape with engine/include/openscanner/frame_protocol.h.
const FrameProtocol = (function () {
  const MAGIC = 0x4F534346;
  const VERSION = 1;
  const TYPE_CLOUD_SNAPSHOT = 3;
  const TYPE_POSE = 2;

  function decodeCloudSnapshot(buf) {
    const view = new DataView(buf);
    if (buf.byteLength < 32 + 4) return null;
    const magic = view.getUint32(0, true);
    const version = view.getUint16(4, true);
    const type = view.getUint16(6, true);
    if (magic !== MAGIC || version !== VERSION || type !== TYPE_CLOUD_SNAPSHOT) return null;
    const seq = view.getUint32(8, true);
    const tCapture = view.getFloat64(12, true);
    const tServer = view.getFloat64(20, true);
    const payloadLen = view.getUint32(28, true);
    if (payloadLen < 4) return null;
    if (32 + payloadLen > buf.byteLength) return null;
    const count = view.getUint32(32, true);
    if (4 + count * 3 * 4 !== payloadLen) return null;
    const positions = new Float32Array(buf, 36, count * 3);
    return { seq, tCapture, tServer, count, positions };
  }

  function decodePose(buf) {
    const view = new DataView(buf);
    if (buf.byteLength < 32 + 16 * 8) return null;
    const magic = view.getUint32(0, true);
    const version = view.getUint16(4, true);
    const type = view.getUint16(6, true);
    if (magic !== MAGIC || version !== VERSION || type !== TYPE_POSE) return null;
    const seq = view.getUint32(8, true);
    const tCapture = view.getFloat64(12, true);
    const tServer = view.getFloat64(20, true);
    const payloadLen = view.getUint32(28, true);
    if (payloadLen !== 16 * 8) return null;
    if (32 + payloadLen > buf.byteLength) return null;
    const m = new Float64Array(buf, 32, 16);
    const tx = m[3], ty = m[7], tz = m[11];
    return { seq, tCapture, tServer, matrix: m, translation: [tx, ty, tz] };
  }

  return { MAGIC, VERSION, TYPE_CLOUD_SNAPSHOT, TYPE_POSE, decodeCloudSnapshot, decodePose };
})();

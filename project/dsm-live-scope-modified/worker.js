// Runs off the main thread.
// Owns the WebSocket, binary parsing, ring buffers, trigger alignment,
// statistics calculation, and frame generation.

const BATCH_HDR_BYTES = 6;
const PKT_BYTES = 20;

const CHANNELS = [
  "adc_in",
  "fgen_out",
  "filt_x",
  "filt_y",
  "dac0",
  "dac1"
];

const TRIGGER_CH = "filt_x";

let ws = null;

let sampleCount = 500;
let rawLen = sampleCount * 2;

let raw = makeBuffers(rawLen);
let writeIdx = 0;
let totalWritten = 0;

let lastBatchId = -1;
let droppedBatches = 0;
let receivedPackets = 0;

function makeBuffers(n) {
  const b = {};

  for (const ch of CHANNELS) {
    b[ch] = new Int16Array(n);
  }

  return b;
}

function resizeBuffers(n) {
  sampleCount = n;
  rawLen = sampleCount * 2;

  raw = makeBuffers(rawLen);

  writeIdx = 0;
  totalWritten = 0;
}

function pushSample(pktView, offset) {
  raw.adc_in[writeIdx] =
    pktView.getInt16(offset + 4, true);

  raw.fgen_out[writeIdx] =
    pktView.getInt16(offset + 6, true);

  raw.filt_x[writeIdx] =
    pktView.getInt16(offset + 8, true);

  raw.filt_y[writeIdx] =
    pktView.getInt16(offset + 10, true);

  raw.dac0[writeIdx] =
    pktView.getInt16(offset + 12, true);

  raw.dac1[writeIdx] =
    pktView.getInt16(offset + 14, true);

  writeIdx = (writeIdx + 1) % rawLen;
  totalWritten++;
}

function unwrap(ch) {
  const src = raw[ch];
  const out = new Int16Array(rawLen);

  out.set(src.subarray(writeIdx));
  out.set(
    src.subarray(0, writeIdx),
    rawLen - writeIdx
  );

  return out;
}

function findTrigger(triggerArr) {
  let min = Infinity;
  let max = -Infinity;

  for (let i = 0; i < rawLen; i++) {
    const v = triggerArr[i];

    if (v < min) min = v;
    if (v > max) max = v;
  }

  // No meaningful signal variation.
  if (max - min < 4) {
    return 0;
  }

  const level = (min + max) / 2;

  const searchEnd = rawLen - sampleCount;

  for (let i = 1; i < searchEnd; i++) {
    if (
      triggerArr[i - 1] < level &&
      triggerArr[i] >= level
    ) {
      return i;
    }
  }

  return 0;
}

function calculateStats(arr) {
  let min = Infinity;
  let max = -Infinity;
  let sum = 0;

  for (let i = 0; i < arr.length; i++) {
    const v = arr[i];

    if (v < min) min = v;
    if (v > max) max = v;

    sum += v;
  }

  const current =
    arr.length > 0
      ? arr[arr.length - 1]
      : NaN;

  const average =
    arr.length > 0
      ? sum / arr.length
      : NaN;

  return {
    current,
    min,
    max,
    average,
    delta: max - min
  };
}

function postSnapshot() {
  const triggerArr = unwrap(TRIGGER_CH);
  const triggerIndex = findTrigger(triggerArr);

  const out = {};
  const stats = {};
  const transferList = [];

  for (const ch of CHANNELS) {
    const full =
      ch === TRIGGER_CH
        ? triggerArr
        : unwrap(ch);

    const dst = new Int16Array(sampleCount);

    dst.set(
      full.subarray(
        triggerIndex,
        triggerIndex + sampleCount
      )
    );

    out[ch] = dst;
    stats[ch] = calculateStats(dst);

    transferList.push(dst.buffer);
  }

  self.postMessage(
    {
      type: "frame",
      buffers: out,
      stats,
      sampleCount,
      droppedBatches,
      receivedPackets
    },
    transferList
  );
}

function handleBatch(buf) {
  if (!(buf instanceof ArrayBuffer)) {
    return;
  }

  if (buf.byteLength < BATCH_HDR_BYTES) {
    return;
  }

  const view = new DataView(buf);

  const batchId = view.getUint32(0, true);
  const count = view.getUint16(4, true);

  if (lastBatchId >= 0) {
    const expected =
      (lastBatchId + 1) >>> 0;

    if (batchId !== expected) {
      droppedBatches++;
    }
  }

  lastBatchId = batchId;

  for (let i = 0; i < count; i++) {
    const offset =
      BATCH_HDR_BYTES + i * PKT_BYTES;

    if (offset + PKT_BYTES > buf.byteLength) {
      break;
    }

    pushSample(view, offset);
    receivedPackets++;
  }

  if (totalWritten >= rawLen) {
    postSnapshot();
  }
}

function connect(url) {
  if (ws) {
    try {
      ws.close();
    } catch (_) {}
  }

  ws = new WebSocket(url);
  ws.binaryType = "arraybuffer";

  ws.onopen = () => {
    self.postMessage({
      type: "status",
      connected: true
    });
  };

  ws.onclose = () => {
    self.postMessage({
      type: "status",
      connected: false
    });
  };

  ws.onerror = e => {
    self.postMessage({
      type: "status",
      connected: false,
      error: String(e.message || e)
    });
  };

  ws.onmessage = ev => {
    if (ev.data instanceof ArrayBuffer) {
      handleBatch(ev.data);
    }
  };
}

function disconnect() {
  if (ws) {
    ws.close();
    ws = null;
  }

  lastBatchId = -1;
}

self.onmessage = ev => {
  const msg = ev.data;

  if (msg.type === "connect") {
    connect(msg.url);
  }

  else if (msg.type === "setSampleCount") {
    const n = Math.max(
      20,
      Math.min(
        5000,
        Number.parseInt(msg.sampleCount, 10) || 500
      )
    );

    resizeBuffers(n);
  }

  else if (msg.type === "disconnect") {
    disconnect();
  }
};

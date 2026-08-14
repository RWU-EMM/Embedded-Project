// Runs off the main thread. Owns the WebSocket, binary parsing, ring buffers, trigger.

const BATCH_HDR_BYTES = 6;   // batch_id(u32) + count(u16)
const PKT_BYTES = 20;        // sig, n, seq, adc_in, fgen_out, filt_x, filt_y, dac0, dac1, dt, crc16

const CHANNELS = ["adc_in", "fgen_out", "filt_x", "filt_y", "dac0", "dac1"];
const TRIGGER_CH = "filt_x"; // edge-trigger reference channel

let ws = null;
let sampleCount = 500;
let rawLen = sampleCount * 2;   // extra headroom so a trigger point can always be found
let raw = makeBuffers(rawLen);
let writeIdx = 0;
let totalWritten = 0;

let lastBatchId = -1;
let droppedBatches = 0;
let receivedPackets = 0;

function makeBuffers(n) {
    const b = {};
    for (const ch of CHANNELS) b[ch] = new Int16Array(n);
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
    raw.adc_in[writeIdx]   = pktView.getInt16(offset + 4, true);
    raw.fgen_out[writeIdx] = pktView.getInt16(offset + 6, true);
    raw.filt_x[writeIdx]   = pktView.getInt16(offset + 8, true);
    raw.filt_y[writeIdx]   = pktView.getInt16(offset + 10, true);
    raw.dac0[writeIdx]     = pktView.getInt16(offset + 12, true);
    raw.dac1[writeIdx]     = pktView.getInt16(offset + 14, true);

    writeIdx = (writeIdx + 1) % rawLen;
    totalWritten++;
}

// Unwrap circular buffer into chronological order (oldest..newest), length rawLen.
function unwrap(ch) {
    const src = raw[ch];
    const out = new Int16Array(rawLen);
    out.set(src.subarray(writeIdx));
    out.set(src.subarray(0, writeIdx), rawLen - writeIdx);
    return out;
}

// Find first rising-edge crossing of mid-level within the searchable region
// (leave sampleCount samples after it so the slice fits). Falls back to 0
// (raw, unaligned tail) if no edge found — e.g. flat/noise input.
function findTrigger(triggerArr) {
    let min = Infinity, max = -Infinity;
    for (let i = 0; i < rawLen; i++) {
        if (triggerArr[i] < min) min = triggerArr[i];
        if (triggerArr[i] > max) max = triggerArr[i];
    }
    if (max - min < 4) return 0; // no meaningful edge, avoid noise-triggering
    const level = (min + max) / 2;

    const searchEnd = rawLen - sampleCount;
    for (let i = 1; i < searchEnd; i++) {
        if (triggerArr[i - 1] < level && triggerArr[i] >= level) {
            return i;
        }
    }
    return 0;
}

function postSnapshot() {
    const triggerArr = unwrap(TRIGGER_CH);
    const t = findTrigger(triggerArr);

    const out = {};
    const transferList = [];
    for (const ch of CHANNELS) {
        const full = ch === TRIGGER_CH ? triggerArr : unwrap(ch);
        const dst = new Int16Array(sampleCount);
        dst.set(full.subarray(t, t + sampleCount));
        out[ch] = dst;
        transferList.push(dst.buffer);
    }

    self.postMessage(
        { type: "frame", buffers: out, sampleCount, droppedBatches, receivedPackets },
        transferList
    );
}

function handleBatch(buf) {
    const view = new DataView(buf);
    const batchId = view.getUint32(0, true);
    const count = view.getUint16(4, true);

    if (lastBatchId >= 0) {
        const expected = (lastBatchId + 1) >>> 0;
        if (batchId !== expected) {
            droppedBatches += 1;
        }
    }
    lastBatchId = batchId;

    for (let i = 0; i < count; i++) {
        const offset = BATCH_HDR_BYTES + i * PKT_BYTES;
        if (offset + PKT_BYTES > buf.byteLength) break;
        pushSample(view, offset);
        receivedPackets++;
    }

    if (totalWritten >= rawLen) {
        postSnapshot();
    }
}

function connect(url) {
    ws = new WebSocket(url);
    ws.binaryType = "arraybuffer";

    ws.onopen = () => self.postMessage({ type: "status", connected: true });
    ws.onclose = () => self.postMessage({ type: "status", connected: false });
    ws.onerror = (e) => self.postMessage({ type: "status", connected: false, error: String(e.message || e) });

    ws.onmessage = (ev) => {
        if (ev.data instanceof ArrayBuffer) {
            handleBatch(ev.data);
        }
    };
}

self.onmessage = (ev) => {
    const msg = ev.data;
    if (msg.type === "connect") {
        connect(msg.url);
    } else if (msg.type === "setSampleCount") {
        resizeBuffers(msg.sampleCount);
    } else if (msg.type === "disconnect") {
        if (ws) ws.close();
    }
};
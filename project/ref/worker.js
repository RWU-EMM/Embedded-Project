// Runs off the main thread. Owns the WebSocket, binary parsing, and ring buffers.

const BATCH_HDR_BYTES = 6;   // batch_id(u32) + count(u16)
const PKT_BYTES = 20;        // sig, n, seq, adc_in, fgen_out, filt_x, filt_y, dac0, dac1, dt, crc16

const CHANNELS = ["adc_in", "fgen_out", "filt_x", "filt_y", "dac0", "dac1"];

let ws = null;
let sampleCount = 500;       // default, overwritten by main thread on init/change
let buffers = makeBuffers(sampleCount);
let writeIdx = 0;
let filled = false;

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
    buffers = makeBuffers(n);
    writeIdx = 0;
    filled = false;
}

function pushSample(pktView, offset) {
    const ch = buffers;
    ch.adc_in[writeIdx]   = pktView.getInt16(offset + 4, true);
    ch.fgen_out[writeIdx] = pktView.getInt16(offset + 6, true);
    ch.filt_x[writeIdx]   = pktView.getInt16(offset + 8, true);
    ch.filt_y[writeIdx]   = pktView.getInt16(offset + 10, true);
    ch.dac0[writeIdx]     = pktView.getInt16(offset + 12, true);
    ch.dac1[writeIdx]     = pktView.getInt16(offset + 14, true);

    writeIdx++;
    if (writeIdx >= sampleCount) {
        writeIdx = 0;
        filled = true;
    }
}

function postSnapshot() {
    // Copy out (ring buffer order starting at writeIdx if filled, else 0..writeIdx)
    const out = {};
    for (const ch of CHANNELS) {
        const src = buffers[ch];
        const dst = new Int16Array(sampleCount);
        if (!filled) {
            dst.set(src.subarray(0, writeIdx));
        } else {
            dst.set(src.subarray(writeIdx));
            dst.set(src.subarray(0, writeIdx), sampleCount - writeIdx);
        }
        out[ch] = dst;
    }
    self.postMessage(
        { type: "frame", buffers: out, sampleCount, droppedBatches, receivedPackets },
        Object.values(out).map((a) => a.buffer)
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

    postSnapshot();
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

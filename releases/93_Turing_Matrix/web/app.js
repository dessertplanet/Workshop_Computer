const SYSEX_START = 0xf0;
const SYSEX_END = 0xf7;
const MANUFACTURER = 0x7d;
const DEVICE = 93;
const CMD_GET = 0x01;
const CMD_GET_RESPONSE = 0x02;
const CMD_SET = 0x03;
const CMD_LIVE_STATUS = 0x10;
const CONFIG_SIZE = 32;

const DEFAULT_CONFIG = {
  magic: 0x434f4e46,
  bpm: 1605,
  divide: 5,
  cvRange: 0,
  preset0: {
    scale: 3,
    range: 2,
    length: 5,
    looplen: 1,
    pulseMode1: 0,
    pulseMode2: 0,
    cvRange: 0,
  },
  preset1: {
    scale: 3,
    range: 1,
    length: 5,
    looplen: 1,
    pulseMode1: 0,
    pulseMode2: 1,
    cvRange: 3,
  },
  vactrol: {
    law: 0,
    relation: 0,
    rise: 48,
    fall: 56,
    min1: 0,
    max1: 255,
    min2: 0,
    max2: 255,
  },
};

const state = {
  midiAccess: null,
  input: null,
  output: null,
  config: structuredClone(DEFAULT_CONFIG),
  sysexBuffer: [],
  lastTxAt: null,
  lastRxAt: null,
  transportLog: ["No SysEx traffic yet."],
  ignoredShortCount: 0,
};

function byId(id) {
  return document.getElementById(id);
}

function setStatus(text) {
  byId("status").textContent = text;
}

function setTransport(text) {
  const stamp = new Date().toLocaleTimeString();
  if (state.transportLog.length === 1 && state.transportLog[0] === "No SysEx traffic yet.") {
    state.transportLog = [];
  }
  state.transportLog.push(`[${stamp}] ${text}`);
  state.transportLog = state.transportLog.slice(-16);
  const logEl = byId("transportLog");
  logEl.textContent = state.transportLog.join("\n");
  logEl.scrollTop = logEl.scrollHeight;
}

function clearTransportLog() {
  state.transportLog = ["No SysEx traffic yet."];
  state.ignoredShortCount = 0;
  byId("transportLog").textContent = state.transportLog[0];
}

function setPortsText() {
  const inputName = state.input?.name || "none";
  const outputName = state.output?.name || "none";
  byId("ports").textContent = `Input: ${inputName} | Output: ${outputName}`;
}

function formatBytes(data) {
  return [...data].map((value) => value.toString(16).padStart(2, "0")).join(" ");
}

function scorePort(port) {
  const text = `${port?.name || ""} ${port?.manufacturer || ""}`.toLowerCase();
  let score = 0;
  if (text.includes("mtmcomputer")) score += 100;
  if (text.includes("music thing")) score += 50;
  if (text.includes("workshop")) score += 25;
  return score;
}

function selectPreferredPort(ports, previousId) {
  if (previousId) {
    const exact = ports.find((port) => port.id === previousId);
    if (exact) {
      return exact;
    }
  }
  return [...ports].sort((a, b) => scorePort(b) - scorePort(a))[0] || null;
}

function bindRangePair(rangeId, numberId) {
  const range = byId(rangeId);
  const number = byId(numberId);
  range.addEventListener("input", () => {
    number.value = range.value;
  });
  number.addEventListener("input", () => {
    range.value = number.value;
  });
}

function syncFormFromConfig(cfg) {
  const p = cfg.preset0;
  byId("scale").value = p.scale;
  byId("range").value = p.range;
  byId("length").value = p.length;
  byId("looplen").value = p.looplen;
  byId("pulseMode1").value = p.pulseMode1;
  byId("pulseMode2").value = p.pulseMode2;
  byId("preset_cvRange").value = p.cvRange;

  byId("vactrol_law").value = cfg.vactrol.law;
  byId("vactrol_relation").value = cfg.vactrol.relation;
  byId("vactrol_rise").value = cfg.vactrol.rise;
  byId("vactrol_rise_num").value = cfg.vactrol.rise;
  byId("vactrol_fall").value = cfg.vactrol.fall;
  byId("vactrol_fall_num").value = cfg.vactrol.fall;
  byId("vactrol_min1").value = cfg.vactrol.min1;
  byId("vactrol_min1_num").value = cfg.vactrol.min1;
  byId("vactrol_max1").value = cfg.vactrol.max1;
  byId("vactrol_max1_num").value = cfg.vactrol.max1;
  byId("vactrol_min2").value = cfg.vactrol.min2;
  byId("vactrol_min2_num").value = cfg.vactrol.min2;
  byId("vactrol_max2").value = cfg.vactrol.max2;
  byId("vactrol_max2_num").value = cfg.vactrol.max2;
}

function readFormIntoConfig() {
  const cfg = structuredClone(state.config);
  cfg.divide = 5;
  cfg.cvRange = cfg.preset0.cvRange;
  cfg.preset0.scale = Number(byId("scale").value);
  cfg.preset0.range = Number(byId("range").value);
  cfg.preset0.length = Number(byId("length").value);
  cfg.preset0.looplen = Number(byId("looplen").value);
  cfg.preset0.pulseMode1 = Number(byId("pulseMode1").value);
  cfg.preset0.pulseMode2 = Number(byId("pulseMode2").value);
  cfg.preset0.cvRange = Number(byId("preset_cvRange").value);
  cfg.cvRange = cfg.preset0.cvRange;
  cfg.vactrol.law = Number(byId("vactrol_law").value);
  cfg.vactrol.relation = Number(byId("vactrol_relation").value);
  cfg.vactrol.rise = Number(byId("vactrol_rise_num").value);
  cfg.vactrol.fall = Number(byId("vactrol_fall_num").value);
  cfg.vactrol.min1 = Number(byId("vactrol_min1_num").value);
  cfg.vactrol.max1 = Number(byId("vactrol_max1_num").value);
  cfg.vactrol.min2 = Number(byId("vactrol_min2_num").value);
  cfg.vactrol.max2 = Number(byId("vactrol_max2_num").value);
  return cfg;
}

function encodeConfig(cfg) {
  const bytes = [];
  const push8 = (value) => bytes.push(value & 0xff);
  const push16 = (value) => {
    push8(value & 0xff);
    push8((value >> 8) & 0xff);
  };
  const push32 = (value) => {
    push8(value & 0xff);
    push8((value >> 8) & 0xff);
    push8((value >> 16) & 0xff);
    push8((value >> 24) & 0xff);
  };
  const pushPreset = (preset) => {
    push8(preset.scale);
    push8(preset.range);
    push8(preset.length);
    push8(preset.looplen);
    push8(preset.pulseMode1);
    push8(preset.pulseMode2);
    push8(preset.cvRange);
  };
  const pushVactrol = (vactrol) => {
    push8(vactrol.law);
    push8(vactrol.relation);
    push8(vactrol.rise);
    push8(vactrol.fall);
    push8(vactrol.min1);
    push8(vactrol.max1);
    push8(vactrol.min2);
    push8(vactrol.max2);
  };

  push32(cfg.magic >>> 0);
  push16(cfg.bpm);
  push8(cfg.divide);
  push8(cfg.cvRange);
  pushPreset(cfg.preset0);
  pushPreset(cfg.preset1);
  pushVactrol(cfg.vactrol);
  while (bytes.length < CONFIG_SIZE) {
    push8(0);
  }
  return bytes;
}

function decodeConfig(bytes) {
  let index = 0;
  const read8 = () => bytes[index++] ?? 0;
  const read16 = () => read8() | (read8() << 8);
  const read32 = () => (read8() | (read8() << 8) | (read8() << 16) | (read8() << 24)) >>> 0;
  const readPreset = () => ({
    scale: read8(),
    range: read8(),
    length: read8(),
    looplen: read8(),
    pulseMode1: read8(),
    pulseMode2: read8(),
    cvRange: read8(),
  });
  const readVactrol = () => ({
    law: read8(),
    relation: read8(),
    rise: read8(),
    fall: read8(),
    min1: read8(),
    max1: read8(),
    min2: read8(),
    max2: read8(),
  });

  return {
    magic: read32(),
    bpm: read16(),
    divide: read8(),
    cvRange: read8(),
    preset0: readPreset(),
    preset1: readPreset(),
    vactrol: readVactrol(),
  };
}

function encode7Bit(raw) {
  const out = [];
  for (let i = 0; i < raw.length; i += 7) {
    const block = raw.slice(i, i + 7);
    let msb = 0;
    for (let j = 0; j < block.length; j += 1) {
      if (block[j] & 0x80) {
        msb |= 1 << j;
      }
    }
    out.push(msb);
    for (const value of block) {
      out.push(value & 0x7f);
    }
  }
  return out;
}

function decode7Bit(payload) {
  const out = [];
  let index = 0;
  while (index < payload.length) {
    const msb = payload[index++];
    for (let bit = 0; bit < 7 && index < payload.length; bit += 1) {
      let value = payload[index++];
      if (msb & (1 << bit)) {
        value |= 0x80;
      }
      out.push(value);
    }
  }
  return out;
}

function sendSysEx(command, payload = []) {
  if (!state.output) {
    setStatus("No MIDI output selected.");
    return;
  }
  const message = new Uint8Array(5 + payload.length);
  message[0] = SYSEX_START;
  message[1] = MANUFACTURER;
  message[2] = DEVICE;
  message[3] = command;
  message.set(payload, 4);
  message[message.length - 1] = SYSEX_END;
  state.output.send(message);
  state.lastTxAt = Date.now();
  setTransport(`TX cmd 0x${command.toString(16)} (${message.length} bytes): ${formatBytes(message)}`);
}

function handleLiveStatus(payload) {
  if (payload.length < 11) {
    setTransport(`RX live status too short (${payload.length} bytes).`);
    return;
  }
  const lane1 = ((payload[0] & 0x01) << 7) | payload[1];
  const lane2 = ((payload[2] & 0x01) << 7) | payload[3];
  const pwm1 = ((payload[4] & 0x01) << 7) | payload[5];
  const pwm2 = ((payload[6] & 0x01) << 7) | payload[7];
  const randomness = payload[8];
  const layer = payload[9] ? "Vactrol" : "Turing";
  const length = payload[10];
  setTransport(
    `RX live status: layer=${layer}, len=${length}, rand=${randomness}, dac1=${lane1}, dac2=${lane2}, pwm1=${pwm1}, pwm2=${pwm2}`
  );
}

function handleMIDIMessage(event) {
  for (const byte of event.data) {
    if (byte === SYSEX_START) {
      state.sysexBuffer = [byte];
      continue;
    }

    if (state.sysexBuffer.length === 0) {
      continue;
    }

    state.sysexBuffer.push(byte);

    if (byte !== SYSEX_END) {
      continue;
    }

    const data = state.sysexBuffer;
    state.sysexBuffer = [];
    state.lastRxAt = Date.now();

    if (data.length === 3 && data[0] === SYSEX_START && data[1] === MANUFACTURER && data[2] === SYSEX_END) {
      state.ignoredShortCount += 1;
      if (state.ignoredShortCount === 1 || state.ignoredShortCount % 25 === 0) {
        setTransport(`Ignored ${state.ignoredShortCount} short MIDI noise packets (f0 7d f7).`);
      }
      continue;
    }

    setTransport(`RX event (${data.length} bytes): ${formatBytes(data)}`);

    if (data.length < 5) {
      setTransport(`RX short message (${data.length} bytes): ${formatBytes(data)}`);
      continue;
    }
    if (data[1] !== MANUFACTURER || data[2] !== DEVICE) {
      setTransport(`RX ignored for other device: ${formatBytes(data)}`);
      continue;
    }
    if (data[3] === CMD_LIVE_STATUS) {
      handleLiveStatus(data.slice(4, -1));
      continue;
    }
    if (data[3] !== CMD_GET_RESPONSE) {
      setTransport(`RX unknown cmd 0x${data[3].toString(16)}: ${formatBytes(data)}`);
      continue;
    }

    const payload = data.slice(7, -1);
    const raw = decode7Bit(payload);
    if (raw.length < CONFIG_SIZE) {
      setStatus(`Config reply was too short (${raw.length} bytes).`);
      setTransport(`RX config too short (${raw.length} bytes raw): ${formatBytes(data)}`);
      continue;
    }
    state.config = decodeConfig(raw);
    syncFormFromConfig(state.config);
    setStatus(`Config received from card (${raw.length} bytes).`);
    setTransport(`RX config ok (${raw.length} raw bytes).`);
  }
}

function updatePorts() {
  const midiIn = byId("midiIn");
  const midiOut = byId("midiOut");
  const previousInputId = state.input?.id || "";
  const previousOutputId = state.output?.id || "";
  midiIn.innerHTML = "";
  midiOut.innerHTML = "";

  const inputs = [...state.midiAccess.inputs.values()];
  const outputs = [...state.midiAccess.outputs.values()];

  for (const input of inputs) {
    const option = document.createElement("option");
    option.value = input.id;
    option.textContent = input.name || input.id;
    midiIn.appendChild(option);
  }

  for (const output of outputs) {
    const option = document.createElement("option");
    option.value = output.id;
    option.textContent = output.name || output.id;
    midiOut.appendChild(option);
  }

  state.input = selectPreferredPort(inputs, previousInputId);
  state.output = selectPreferredPort(outputs, previousOutputId);
  midiIn.value = state.input?.id || "";
  midiOut.value = state.output?.id || "";

  if (state.input) {
    state.input.onmidimessage = handleMIDIMessage;
  }
  setPortsText();
}

async function connectMIDI() {
  if (!navigator.requestMIDIAccess) {
    setStatus("Web MIDI is not available in this browser.");
    return;
  }
  try {
    state.midiAccess = await navigator.requestMIDIAccess({ sysex: true });
    state.midiAccess.onstatechange = updatePorts;
    updatePorts();
    setStatus("Web MIDI connected.");
    setTransport("Connected. Press Read From Card to test SysEx.");
  } catch (error) {
    setStatus(`Web MIDI connection failed: ${error.message}`);
  }
}

function init() {
  bindRangePair("vactrol_rise", "vactrol_rise_num");
  bindRangePair("vactrol_fall", "vactrol_fall_num");
  bindRangePair("vactrol_min1", "vactrol_min1_num");
  bindRangePair("vactrol_max1", "vactrol_max1_num");
  bindRangePair("vactrol_min2", "vactrol_min2_num");
  bindRangePair("vactrol_max2", "vactrol_max2_num");
  syncFormFromConfig(state.config);

  byId("connectBtn").addEventListener("click", connectMIDI);
  byId("midiIn").addEventListener("change", (event) => {
    const input = state.midiAccess?.inputs.get(event.target.value) || null;
    if (state.input) {
      state.input.onmidimessage = null;
    }
    state.input = input;
    if (state.input) {
      state.input.onmidimessage = handleMIDIMessage;
    }
    setPortsText();
  });
  byId("midiOut").addEventListener("change", (event) => {
    state.output = state.midiAccess?.outputs.get(event.target.value) || null;
    setPortsText();
  });

  byId("readBtn").addEventListener("click", () => {
    sendSysEx(CMD_GET);
    setStatus("Requested config from card.");
  });

  byId("sendBtn").addEventListener("click", () => {
    state.config = readFormIntoConfig();
    const raw = encodeConfig(state.config);
    const packed = encode7Bit(raw);
    sendSysEx(CMD_SET, packed);
    setStatus(`Sent shared Turing settings to card (${raw.length} bytes raw).`);
  });

  byId("defaultsBtn").addEventListener("click", () => {
    state.config = structuredClone(DEFAULT_CONFIG);
    syncFormFromConfig(state.config);
    setStatus("Loaded editor defaults locally.");
  });
  byId("clearLogBtn").addEventListener("click", clearTransportLog);

  setPortsText();
}

init();

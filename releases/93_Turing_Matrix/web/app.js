const SYSEX_START = 0xf0;
const SYSEX_END = 0xf7;
const MANUFACTURER = 0x7d;
const DEVICE = 0x93;
const CMD_GET = 0x01;
const CMD_GET_RESPONSE = 0x02;
const CMD_SET = 0x03;
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
};

function byId(id) {
  return document.getElementById(id);
}

function setStatus(text) {
  byId("status").textContent = text;
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
  byId("bpm").value = cfg.bpm;
  byId("bpm_num").value = cfg.bpm;
  byId("divide").value = cfg.divide;
  byId("divide_num").value = cfg.divide;
  byId("global_cv_range").value = cfg.cvRange;

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
  cfg.bpm = Number(byId("bpm_num").value);
  cfg.divide = Number(byId("divide_num").value);
  cfg.cvRange = Number(byId("global_cv_range").value);
  cfg.preset0.scale = Number(byId("scale").value);
  cfg.preset0.range = Number(byId("range").value);
  cfg.preset0.length = Number(byId("length").value);
  cfg.preset0.looplen = Number(byId("looplen").value);
  cfg.preset0.pulseMode1 = Number(byId("pulseMode1").value);
  cfg.preset0.pulseMode2 = Number(byId("pulseMode2").value);
  cfg.preset0.cvRange = Number(byId("preset_cvRange").value);
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
}

function handleMIDIMessage(event) {
  const data = [...event.data];
  if (data.length < 5 || data[0] !== SYSEX_START || data.at(-1) !== SYSEX_END) {
    return;
  }
  if (data[1] !== MANUFACTURER || data[2] !== DEVICE) {
    return;
  }
  if (data[3] !== CMD_GET_RESPONSE) {
    return;
  }

  const payload = data.slice(7, -1);
  const raw = decode7Bit(payload);
  if (raw.length < CONFIG_SIZE) {
    setStatus(`Config reply was too short (${raw.length} bytes).`);
    return;
  }
  state.config = decodeConfig(raw);
  syncFormFromConfig(state.config);
  setStatus(`Config received from card (${raw.length} bytes).`);
}

function updatePorts() {
  const midiIn = byId("midiIn");
  const midiOut = byId("midiOut");
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

  state.input = inputs[0] || null;
  state.output = outputs[0] || null;
  midiIn.value = state.input?.id || "";
  midiOut.value = state.output?.id || "";

  if (state.input) {
    state.input.onmidimessage = handleMIDIMessage;
  }
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
  } catch (error) {
    setStatus(`Web MIDI connection failed: ${error.message}`);
  }
}

function init() {
  bindRangePair("bpm", "bpm_num");
  bindRangePair("divide", "divide_num");
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
  });
  byId("midiOut").addEventListener("change", (event) => {
    state.output = state.midiAccess?.outputs.get(event.target.value) || null;
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
}

init();

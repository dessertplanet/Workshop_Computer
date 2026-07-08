// Canonical program-card model.
//
// This is a JavaScript port of the MTM program-card importer
// (TomWhitwell/MTM_Newsite_2022 scripts/import_program_cards.rb). It normalizes
// a single release's raw info.yaml (plus discovered docs/downloads/web assets)
// into one canonical card object that mirrors the importer's generated
// _data/program_cards/cards.yml entry shape.
//
// Field extraction is deliberately case/space/underscore-insensitive to match
// the importer, so `Name`, `name`, and `NAME` all resolve to the same value.

import { parseYoutubeId } from '../utils/youtube.js';

// API jack id -> physical panel slot key (ported from the importer constants).
const API_INPUT_KEYS = {
  AudioIn1: 'audio_l',
  AudioIn2: 'audio_r',
  CVIn1: 'cv_1',
  CVIn2: 'cv_2',
  CV1: 'cv_1',
  CV2: 'cv_2',
  PulseIn1: 'pulse_1',
  PulseIn2: 'pulse_2',
  Pulse1: 'pulse_1',
  Pulse2: 'pulse_2',
};

const API_OUTPUT_KEYS = {
  AudioOut1: 'audio_out_l',
  AudioOut2: 'audio_out_r',
  CVOut1: 'cv_out_1',
  CVOut2: 'cv_out_2',
  PulseOut1: 'pulse_out_1',
  PulseOut2: 'pulse_out_2',
};

const SLOT_KEY_ALIASES = {
  audio_out_1: 'audio_out_l',
  audio_out_2: 'audio_out_r',
  audio_in_1: 'audio_l',
  audio_in_2: 'audio_r',
};

const KNOB_KEYS = ['main', 'x', 'y'];
const Z_MODES = ['up', 'middle', 'down'];

// ---------- generic helpers (ported from the importer) ----------

function isPlainObject(value) {
  return Boolean(value) && typeof value === 'object' && !Array.isArray(value) && !(value instanceof Date);
}

function normalizeKey(key) {
  return String(key ?? '').toLowerCase().replace(/[-_\s]/g, '');
}

/** Case/space/underscore-insensitive key lookup over a plain object. */
function field(hash, ...names) {
  if (!isPlainObject(hash)) return undefined;
  for (const name of names) {
    if (Object.prototype.hasOwnProperty.call(hash, name)) return hash[name];
    const normalized = normalizeKey(name);
    for (const [key, value] of Object.entries(hash)) {
      if (normalizeKey(key) === normalized) return value;
    }
  }
  return undefined;
}

function textValue(value, fallback = '') {
  if (value == null) return fallback;
  if (value instanceof Date) return value.toISOString().slice(0, 10);
  if (typeof value === 'object') return fallback;
  return String(value).trim();
}

function requiredText(value, warnings, fieldName, fallback = 'n/a') {
  if (value == null) {
    warnings.push(`${fieldName} missing; using ${JSON.stringify(fallback)}.`);
    return fallback;
  }
  if (typeof value === 'object' && !(value instanceof Date)) {
    warnings.push(`${fieldName} should be text; using ${JSON.stringify(fallback)}.`);
    return fallback;
  }
  const text = textValue(value);
  if (!text) {
    warnings.push(`${fieldName} blank; using ${JSON.stringify(fallback)}.`);
    return fallback;
  }
  return text;
}

function optionalText(value, warnings, fieldName) {
  if (value == null) return '';
  if (typeof value === 'object' && !(value instanceof Date)) {
    if (warnings && fieldName) warnings.push(`${fieldName} should be text; using n/a.`);
    return 'n/a';
  }
  return textValue(value);
}

function listValue(value, warnings, fieldName) {
  if (value == null) return [];
  if (Array.isArray(value)) return value;
  if (warnings && fieldName) warnings.push(`${fieldName} should be a list; coerced single value to a list.`);
  return [value];
}

function hashValue(value, warnings, fieldName) {
  if (isPlainObject(value)) return value;
  if (warnings && fieldName && value != null) {
    warnings.push(`${fieldName} should be a map/object; ignored ${typeof value}.`);
  }
  return {};
}

function sanitizeValue(value) {
  if (value instanceof Date) return value.toISOString().slice(0, 10);
  if (Array.isArray(value)) return value.map(sanitizeValue);
  if (isPlainObject(value)) {
    return Object.fromEntries(
      Object.entries(value).map(([key, child]) => [textValue(key, 'unknown'), sanitizeValue(child)])
    );
  }
  if (value == null || typeof value === 'string' || typeof value === 'number' || typeof value === 'boolean') {
    return value;
  }
  return String(value);
}

function mappedSlot(mapping, value) {
  const key = textValue(value);
  if (mapping[key]) return mapping[key];
  if (SLOT_KEY_ALIASES[key]) return SLOT_KEY_ALIASES[key];
  const normalized = normalizeKey(key);
  for (const [mapKey, slot] of Object.entries(mapping)) {
    if (normalizeKey(mapKey) === normalized) return slot;
  }
  for (const [aliasKey, slot] of Object.entries(SLOT_KEY_ALIASES)) {
    if (normalizeKey(aliasKey) === normalized) return slot;
  }
  return null;
}

function truthy(value) {
  if (value === true) return true;
  if (value === false || value == null) return false;
  return ['true', 'yes', 'y', '1'].includes(String(value).trim().toLowerCase());
}

function normalizedDate(value, warnings, fieldName) {
  const text = optionalText(value, warnings, fieldName);
  if (!text) return '';
  if (/^\d{4}-\d{2}-\d{2}$/.test(text)) return text;
  if (warnings && fieldName) warnings.push(`${fieldName} should be YYYY-MM-DD; using n/a.`);
  return 'n/a';
}

function cardNumber(id) {
  return String(id).split('_')[0];
}

function titleizeId(id) {
  return String(id)
    .replace(/^\d+_?/, '')
    .replace(/[_-]/g, ' ')
    .split(/\s+/)
    .filter(Boolean)
    .map(word => word.charAt(0).toUpperCase() + word.slice(1))
    .join(' ');
}

function compactPanelLabel(name) {
  let text = textValue(name).trim().replace(/\s*\/\s*/g, ' / ');
  if (!text) return '';
  const replacements = {
    External: 'Ext',
    Channel: 'Chan',
    Quantized: 'Quant',
    Modulation: 'Mod',
    Divide: 'Div',
    Multiply: 'Mult',
    Randomness: 'Random',
    Trigger: 'Trig',
    Output: 'Out',
    Input: 'In',
    'Preset Select': 'Preset',
    Pattern: 'Patt',
  };
  for (const [from, to] of Object.entries(replacements)) {
    text = text.replace(new RegExp(`\\b${from.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\b`, 'gi'), to);
  }
  const words = text.split(/\s+/);
  const lines = [];
  let line = '';
  for (const word of words) {
    const candidate = line ? `${line} ${word}` : word;
    if (candidate.length <= 12) {
      line = candidate;
    } else {
      if (line) lines.push(line);
      line = word;
    }
  }
  if (line) lines.push(line);
  const label = lines.slice(0, 2).join('\n');
  return label.length > 24 ? label.slice(0, 23).replace(/\s+$/, '') : label;
}

// ---------- panel sockets ----------

function normalizeSocket(item, source) {
  const obj = hashValue(item);
  const name = field(obj, 'label', 'name', 'Name') ?? field(obj, 'id');
  const out = {
    label: compactPanelLabel(name),
    description: textValue(field(obj, 'description', 'Description')),
    source,
  };
  const type = textValue(field(obj, 'type'));
  if (type) out.type = type;
  if (!out.description) delete out.description;
  return out;
}

function normalizeSocketList(items, mapping, warnings, fieldName = 'panel sockets') {
  const result = {};
  const slotKeys = Object.values(mapping);

  if (isPlainObject(items)) {
    for (const [rawKey, value] of Object.entries(items)) {
      const key = textValue(rawKey);
      const item = hashValue(value, warnings, `${fieldName}.${key}`);
      const aliasedKey = mappedSlot(mapping, key) || key;
      if (slotKeys.includes(aliasedKey)) {
        result[aliasedKey] = normalizeSocket({ ...item, id: key }, 'info.yaml');
      } else {
        const apiId = textValue(field(item, 'id'));
        const slot = mappedSlot(mapping, apiId);
        if (slot) {
          result[slot] = normalizeSocket(item, 'info.yaml');
        } else {
          warnings.push(`Unknown ${fieldName} socket key ${JSON.stringify(key)}; ignored.`);
        }
      }
    }
    return result;
  }

  for (const item of listValue(items, warnings, fieldName)) {
    if (!isPlainObject(item)) {
      warnings.push(`${fieldName} entry should be a map/object; ignored ${typeof item}.`);
      continue;
    }
    const apiId = textValue(field(item, 'id'));
    const slot = mappedSlot(mapping, apiId);
    if (!slot) {
      if (apiId) warnings.push(`Unknown ${fieldName} socket id ${JSON.stringify(apiId)}; ignored.`);
      continue;
    }
    result[slot] = normalizeSocket(item, 'info.yaml');
  }
  return result;
}

// ---------- controls / switch / leds ----------

function rowWhen(row) {
  const value = field(row, 'when');
  return isPlainObject(value) ? value : {};
}

function rowContextLabel(row) {
  const context = rowWhen(row);
  const parts = [];
  const z = textValue(field(context, 'z'));
  const layer = textValue(field(context, 'layer'));
  const gesture = textValue(field(context, 'gesture'));
  if (z) parts.push(`Z ${z}`);
  if (layer) parts.push(layer);
  if (gesture) parts.push(gesture);
  return parts.length ? parts.join(', ') : 'Default';
}

function summarizeKnobRow(row) {
  return KNOB_KEYS.map(key => {
    const knob = field(row, key);
    if (!isPlainObject(knob)) return null;
    const name = textValue(field(knob, 'name'));
    if (!name) return null;
    return `${key.toUpperCase()}: ${name}`;
  }).filter(Boolean).join('; ');
}

function normalizeControls(info, warnings) {
  const directControls = field(field(info, 'panel'), 'controls');
  if (isPlainObject(directControls)) {
    const controls = {};
    for (const key of ['main', 'x', 'y', 'z']) {
      const control = directControls[key];
      if (!isPlainObject(control)) continue;
      const label = textValue(field(control, 'label', 'name')) || key;
      controls[key] = {
        label: compactPanelLabel(label),
        description: optionalText(field(control, 'description'), warnings, `panel.controls.${key}.description`),
        source: textValue(field(control, 'source')) || 'info.yaml',
      };
      if (!controls[key].description) delete controls[key].description;
    }
    if (Object.keys(controls).length) return controls;
  }

  const rows = listValue(field(field(info, 'controls'), 'knobs'), warnings, 'controls.knobs');
  const primary = rows.find(row => {
    const context = rowWhen(row);
    return textValue(field(context, 'z')) === 'middle' && !field(context, 'layer') && !field(context, 'gesture');
  }) || rows.find(row => KNOB_KEYS.some(key => isPlainObject(field(row, key))));

  const controls = {};
  if (primary) {
    for (const key of KNOB_KEYS) {
      const knob = field(primary, key);
      if (!isPlainObject(knob)) continue;
      const name = textValue(field(knob, 'name'));
      if (!name) continue;
      controls[key] = {
        label: compactPanelLabel(name),
        description: optionalText(field(knob, 'description'), warnings, `controls.knobs.${key}.description`),
        source: 'info.yaml',
      };
      if (!controls[key].description) delete controls[key].description;
    }
  }

  const zRows = rows.filter(row => field(rowWhen(row), 'z'));
  const downAction = zRows.find(row => textValue(field(rowWhen(row), 'z')) === 'down' && isPlainObject(field(row, 'main')));
  if (downAction) {
    const knob = field(downAction, 'main');
    const name = textValue(field(knob, 'name'));
    controls.z = {
      label: compactPanelLabel(name),
      description: optionalText(field(knob, 'description'), warnings, 'controls.knobs.z.description'),
      source: 'info.yaml',
    };
    if (!controls.z.description) delete controls.z.description;
  } else if (zRows.length) {
    controls.z = { label: 'Mode', description: 'Selects alternate control modes.', source: 'info.yaml' };
  }

  return controls;
}

function normalizeSwitchModes(info, warnings) {
  const modes = { up: '', middle: '', down: '' };

  const directModes = field(info, 'switch_modes');
  if (isPlainObject(directModes)) {
    for (const mode of Z_MODES) modes[mode] = optionalText(field(directModes, mode), warnings, `switch_modes.${mode}`);
    return modes;
  }

  const controlsSwitch = field(field(info, 'controls'), 'switch');
  if (isPlainObject(controlsSwitch)) {
    for (const mode of Z_MODES) {
      const item = field(controlsSwitch, mode);
      if (isPlainObject(item)) {
        const name = optionalText(field(item, 'name'), warnings, `controls.switch.${mode}.name`);
        const description = optionalText(field(item, 'description'), warnings, `controls.switch.${mode}.description`);
        modes[mode] = [name, description].filter(Boolean).join(': ');
      } else {
        modes[mode] = optionalText(item, warnings, `controls.switch.${mode}`);
      }
    }
    return modes;
  }

  const rows = listValue(field(field(info, 'controls'), 'knobs'), warnings, 'controls.knobs');
  for (const mode of Z_MODES) {
    const summaries = rows
      .filter(row => textValue(field(rowWhen(row), 'z')) === mode)
      .map(row => {
        const summary = summarizeKnobRow(row);
        return summary ? `${rowContextLabel(row)}: ${summary}.` : null;
      })
      .filter(Boolean);
    modes[mode] = summaries.join(' ');
  }
  return modes;
}

function normalizeLeds(info, warnings) {
  const directLeds = field(info, 'leds');
  if (directLeds != null) {
    if (typeof directLeds === 'string') {
      return directLeds.split('\n').map(s => s.trim()).filter(Boolean);
    }
    return listValue(directLeds, warnings, 'leds').map(led => textValue(led)).filter(Boolean);
  }

  const rows = listValue(field(field(info, 'controls'), 'leds'), warnings, 'controls.leds');
  return rows.map(row => {
    const items = listValue(field(row, 'items'), warnings, 'controls.leds.items');
    if (!items.length) return null;
    const names = items.map(item => textValue(field(item, 'name'))).filter(Boolean).join('; ');
    if (!names) return null;
    return `${rowContextLabel(row)}: ${names}.`;
  }).filter(Boolean);
}

function isEmptyContainer(value) {
  if (Array.isArray(value)) return value.length === 0;
  if (isPlainObject(value)) return Object.keys(value).length === 0;
  return false;
}

// ---------- assembly ----------

export function buildCanonicalCardModel({
  folderName,
  slug,
  info: normalizedInfo,
  rawYaml,
  docs,
  downloads,
  latestUf2,
  web,
  readmePath,
  sourceFile,
  sourceUrl,
  readmeUrl,
  gitFirstDate = '',
  gitLastDate = '',
}) {
  const warnings = [];
  const info = isPlainObject(rawYaml) ? rawYaml : {};
  const id = folderName;
  const number = cardNumber(id);

  const title = requiredText(field(info, 'Name', 'Title'), warnings, 'Name', titleizeId(id));
  const releaseText = optionalText(field(info, 'release'), warnings, 'release');
  let version = optionalText(field(info, 'Version'), warnings, 'Version');
  if (!version && releaseText.includes('/')) version = releaseText.split('/')[1].trim();
  const description = requiredText(field(info, 'Description'), warnings, 'Description', 'n/a');
  const release = releaseText || (version ? `${number} / ${version}` : number);

  let created = normalizedDate(field(info, 'created', 'created_at'), warnings, 'created');
  if (!created) created = gitFirstDate;
  if (!created) created = 'n/a';
  let updated = normalizedDate(field(info, 'date', 'updated', 'updated_at'), warnings, 'date');
  if (!updated) updated = gitLastDate;
  if (!updated) updated = 'n/a';

  const demoLink = optionalText(field(info, 'demo-link'), warnings, 'demo-link');
  const videoId = demoLink ? parseYoutubeId(demoLink) : null;
  const editor = (web && web.editorUrl) || (normalizedInfo && normalizedInfo.editor) || '';
  const downloadUrl = latestUf2?.url || sourceUrl;

  const panelInfo = hashValue(field(info, 'panel'), warnings, 'panel');
  const panel = {
    controls: normalizeControls(info, warnings),
    inputs: normalizeSocketList(field(panelInfo, 'inputs'), API_INPUT_KEYS, warnings, 'panel.inputs'),
    outputs: normalizeSocketList(field(panelInfo, 'outputs'), API_OUTPUT_KEYS, warnings, 'panel.outputs'),
  };
  if (!Object.keys(panel.controls).length) delete panel.controls;
  if (!Object.keys(panel.inputs).length) delete panel.inputs;
  if (!Object.keys(panel.outputs).length) delete panel.outputs;

  const tags = listValue(field(info, 'tags'), warnings, 'tags')
    .flatMap(tag => textValue(tag).split(','))
    .map(t => t.trim())
    .filter(Boolean);

  const metadata = {
    creator: optionalText(field(info, 'Creator'), warnings, 'Creator'),
    language: optionalText(field(info, 'Language'), warnings, 'Language'),
    version,
    status: optionalText(field(info, 'Status'), warnings, 'Status'),
    license: optionalText(field(info, 'License'), warnings, 'License'),
    created,
    updated,
    repository: optionalText(field(info, 'repository'), warnings, 'repository'),
    contact: sanitizeValue(field(info, 'contact')),
  };
  if (editor) {
    metadata.editor_url = editor;
    metadata.editor_note = 'Configure this card in your browser';
  }
  for (const key of Object.keys(metadata)) {
    const value = metadata[key];
    if (key === 'created' || key === 'updated') continue;
    if (value == null || value === '' || value === 'n/a' || isEmptyContainer(value)) {
      delete metadata[key];
    }
  }

  const notes = [];
  const host = field(info, 'host');
  if (isPlainObject(host)) {
    for (const rawEntry of listValue(field(host, 'usb'), warnings, 'host.usb')) {
      const entry = hashValue(rawEntry, warnings, 'host.usb entry');
      const name = optionalText(field(entry, 'name'), warnings, 'host.usb.name');
      const desc = optionalText(field(entry, 'description'), warnings, 'host.usb.description');
      const note = [name, desc].filter(Boolean).join(': ');
      if (note) notes.push(note);
    }
    const hostNotes = optionalText(field(host, 'notes'), warnings, 'host.notes');
    if (hostNotes) notes.push(hostNotes);
  }

  const card = {
    id,
    slug,
    title,
    draft: truthy(field(info, 'draft', 'Draft')),
    release,
    summary: description,
    description,
    panel,
    switch_modes: normalizeSwitchModes(info, warnings),
    leds: normalizeLeds(info, warnings),
    tags,
    source: [`releases/${id}/info.yaml`, `releases/${id}/README.md`],
    url: `programs/${slug}/`,
    source_file: sourceFile,
    source_url: sourceUrl,
    readme_url: readmeUrl,
    readme_path: readmePath,
    download_url: downloadUrl,
    metadata,
  };

  if (notes.length) card.notes = notes;

  const documentation = {};
  const manual = optionalText(field(info, 'manual'), warnings, 'manual');
  if (manual) documentation.intro = manual;
  const structuredDocs = field(info, 'documentation');
  if (isPlainObject(structuredDocs)) {
    for (const [key, value] of Object.entries(structuredDocs)) {
      const text = textValue(value);
      if (!text) continue;
      if (key === 'intro') {
        if (!documentation.intro) documentation.intro = text;
      } else {
        documentation.sections = documentation.sections || [];
        documentation.sections.push({ title: key.replace(/[_-]/g, ' '), body: text });
      }
    }
  }
  if (Object.keys(documentation).length) card.documentation = documentation;

  const structuredVideos = field(info, 'videos');
  if (Array.isArray(structuredVideos) && structuredVideos.length) {
    card.videos = sanitizeValue(structuredVideos);
  } else if (demoLink && videoId) {
    card.videos = [{ title: 'Demo video', url: demoLink, id: videoId }];
  }

  const quickStart = listValue(field(info, 'quick_start'), warnings, 'quick_start')
    .map(step => textValue(step))
    .filter(Boolean);
  if (quickStart.length) card.quick_start = quickStart;

  const memory = field(info, 'memory');
  if (isPlainObject(memory)) card.memory = sanitizeValue(memory);

  // Presentation extras the site build needs but the importer did not carry.
  if (Array.isArray(docs) && docs.length) card.docs = sanitizeValue(docs);
  if (Array.isArray(downloads) && downloads.length) card.downloads = sanitizeValue(downloads);
  if (web && (web.editorUrl || web.mode)) card.web = sanitizeValue(web);
  if (normalizedInfo && normalizedInfo.audiosampleurl) card.audio_sample_url = normalizedInfo.audiosampleurl;

  card.warnings = warnings;

  return sanitizeValue(card);
}
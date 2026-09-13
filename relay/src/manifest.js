// Manifest validation.
//
// The firmware would survive a bad manifest — it caps everything and shows a
// message on the app's own screen — but "it appeared in the drawer and said it
// was broken" is a terrible way to learn you mistyped a source name. The relay
// is where a person is present to read an error, so this is strict here and
// forgiving on the device.
//
// The two lists below mirror the firmware. SOURCES tracks resolveSource() in
// src/engage/DataSource.cpp and ICONS tracks iconByName() in
// AppDrawerActivity.cpp. A name the firmware does not know renders as nothing
// (sources) or as the generic icon (icons), so drift here degrades quietly
// rather than breaking — but it should not drift.

export const SOURCES = [
  'device.version', 'device.model', 'device.screen', 'device.battery',
  'device.freeHeap', 'device.clock',
  'reading.title', 'reading.author', 'reading.percent',
  'review.word',
  'i18n.about',
];

export const ICONS = [
  'book', 'library', 'bookmark', 'inbox', 'words',
  'settings', 'info', 'clock', 'sun', 'wifi', 'folder', 'file', 'apps',
];

export const ROW_KINDS = ['text', 'kv', 'rule', 'logo'];

// These mirror the firmware's hard caps. A manifest that exceeds them is not
// truncated silently: the author is told, because a screen that quietly loses
// its last three rows is worse than one that refuses to save.
export const LIMITS = { bytes: 4096, rows: 16, field: 64, name: 24 };

function checkField(value, where, errors) {
  if (value === undefined || value === null) return;
  if (typeof value === 'string') {
    if (Buffer.byteLength(value, 'utf8') > LIMITS.field) {
      errors.push(`${where} is longer than ${LIMITS.field} bytes and would be cut on the device`);
    }
    return;
  }
  if (typeof value === 'object' && typeof value.src === 'string') {
    if (!SOURCES.includes(value.src)) {
      errors.push(`${where} binds to "${value.src}", which the reader does not provide`);
    }
    return;
  }
  errors.push(`${where} must be text or {"src": "..."}`);
}

// Returns { ok, errors, meta }. Never throws: the caller is rendering a form.
export function validateManifest(text) {
  const errors = [];
  if (Buffer.byteLength(text ?? '', 'utf8') > LIMITS.bytes) {
    return { ok: false, errors: [`Manifest is over ${LIMITS.bytes} bytes; the reader ignores anything larger`] };
  }

  let doc;
  try {
    doc = JSON.parse(text);
  } catch (e) {
    return { ok: false, errors: [`Not valid JSON: ${e.message}`] };
  }
  if (!doc || typeof doc !== 'object' || Array.isArray(doc)) {
    return { ok: false, errors: ['A manifest is a JSON object'] };
  }

  if (typeof doc.name !== 'string' || !doc.name.trim()) {
    errors.push('name is required: a tile with no label cannot be chosen deliberately');
  } else if (doc.name.length > LIMITS.name) {
    errors.push(`name is cut to ${LIMITS.name} characters on the device`);
  }

  if (doc.icon !== undefined && !ICONS.includes(doc.icon)) {
    errors.push(`icon "${doc.icon}" is unknown; the device falls back to the generic app icon`);
  }

  checkField(doc.title, 'title', errors);

  if (!Array.isArray(doc.rows)) {
    errors.push('rows must be an array');
  } else {
    if (doc.rows.length > LIMITS.rows) errors.push(`rows beyond the first ${LIMITS.rows} are dropped on the device`);
    doc.rows.forEach((row, i) => {
      const at = `row ${i + 1}`;
      if (!row || typeof row !== 'object') { errors.push(`${at} is not an object`); return; }
      const kind = row.kind ?? 'text';
      if (!ROW_KINDS.includes(kind)) { errors.push(`${at} has kind "${kind}"; known kinds are ${ROW_KINDS.join(', ')}`); return; }
      if (kind === 'text') {
        checkField(row.text, `${at} text`, errors);
        checkField(row.prefix, `${at} prefix`, errors);
      } else if (kind === 'kv') {
        checkField(row.label, `${at} label`, errors);
        checkField(row.value, `${at} value`, errors);
      }
      if (row.gapAfter !== undefined && (!Number.isInteger(row.gapAfter) || row.gapAfter < 0 || row.gapAfter > 4)) {
        errors.push(`${at} gapAfter must be a whole number from 0 to 4`);
      }
      // Catching a conditional here is the whole reason this validator exists:
      // the format has no control flow, and someone will try.
      for (const key of ['if', 'when', 'unless', 'repeat', 'each']) {
        if (key in row) errors.push(`${at} uses "${key}"; manifests describe a screen, they do not branch or loop`);
      }
    });
  }

  return {
    ok: errors.length === 0,
    errors,
    meta: { name: String(doc.name ?? '').slice(0, LIMITS.name), icon: ICONS.includes(doc.icon) ? doc.icon : 'apps' },
  };
}

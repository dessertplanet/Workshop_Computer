import Ajv from 'ajv';
import { infoYamlJsonSchema } from '../schema/infoYamlJsonSchema.js';
import { normalizeYamlKey } from '../utils/strings.js';

const ajv = new Ajv({ allErrors: true, strict: false, allowUnionTypes: true });
const validate = ajv.compile(infoYamlJsonSchema);
const REQUIRED_CORE = new Set(['name', 'shortdescription', 'summary', 'language', 'creator', 'version', 'status']);

function decodePointerPart(value) {
  return String(value || '').replace(/~1/g, '/').replace(/~0/g, '~');
}

function pathFromError(error) {
  const parts = String(error.instancePath || '').split('/').slice(1).map(decodePointerPart);
  if (error.keyword === 'required' && error.params?.missingProperty) parts.push(error.params.missingProperty);
  return parts.join('.');
}

function messageFor(error, path) {
  if (error.keyword === 'required') return `Missing required field "${error.params.missingProperty}".`;
  if (error.keyword === 'anyOf' && !path) return 'One of "Name" or legacy "Title" is required.';
  return `${path ? `Field "${path}" ` : 'Value '}${error.message || 'does not match the schema'}.`;
}

/** Validate parsed info.yaml data with AJV and return shared diagnostic shapes. */
export function validateWithAjv(data) {
  if (validate(data)) return [];
  const diagnostics = [];
  const seen = new Set();
  for (const error of validate.errors || []) {
    const path = pathFromError(error);
    // `anyOf` also emits its nested `required` failures. Keep the actionable
    // Name/Title summary rather than reporting all three messages.
    if (error.keyword === 'required' && ['Name', 'Title'].includes(error.params?.missingProperty)) continue;
    const normalized = normalizeYamlKey(path.split('.')[0] || '');
    const severity = error.keyword === 'required' || REQUIRED_CORE.has(normalized) ? 'error' : 'warning';
    const message = messageFor(error, path);
    const identity = `${severity}|${path}|${message}`;
    if (seen.has(identity)) continue;
    seen.add(identity);
    diagnostics.push({
      severity,
      ruleId: 'ajv-schema',
      path,
      key: path.split('.')[0] || '',
      message,
    });
  }
  return diagnostics;
}

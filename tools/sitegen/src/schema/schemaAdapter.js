import { infoYamlSchema } from './schemaDefinition.js';

function normalizePath(path) {
  return String(path || '').trim();
}

export function createSchemaAdapter(schema = infoYamlSchema) {
  const fieldMap = new Map(schema.fields.map(field => [normalizePath(field.path), field]));

  return {
    id: schema.id,
    version: schema.version,
    source: schema.source,
    listFields() {
      return schema.fields.slice();
    },
    getField(path) {
      return fieldMap.get(normalizePath(path)) || null;
    },
    requiredFields() {
      return schema.fields.filter(field => field.required);
    },
  };
}

export function getInfoYamlSchemaAdapter() {
  return createSchemaAdapter(infoYamlSchema);
}
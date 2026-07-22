// Machine-readable structural schema for info.yaml.
//
// This deliberately allows additional properties: historical cards and future
// metadata must remain editable without AJV rejecting fields that are handled
// by semantic rules or preserved verbatim by Basic mode.

const scalarText = {
  anyOf: [
    { type: 'string', minLength: 1 },
    { type: 'number' },
    { type: 'boolean' },
  ],
};

const urlText = {
  type: 'string',
  pattern: '^https?://\\S+$',
};

export const infoYamlJsonSchema = {
  $schema: 'http://json-schema.org/draft-07/schema#',
  $id: 'https://musicthing.co.uk/schemas/workshop-computer-info-yaml-v2.json',
  title: 'Workshop Computer info.yaml',
  type: 'object',
  required: ['short-description', 'summary', 'Language', 'Creator', 'Version', 'Status'],
  anyOf: [
    { required: ['Name'] },
    { required: ['Title'] },
    { required: ['title'] },
  ],
  properties: {
    draft: { type: 'boolean' },
    Name: scalarText,
    Title: scalarText,
    title: scalarText,
    'short-description': scalarText,
    summary: scalarText,
    Language: scalarText,
    Creator: scalarText,
    Version: scalarText,
    Status: scalarText,
    License: scalarText,
    date: scalarText,
    releasedate: scalarText,
    Editor: scalarText,
    'web-entry': scalarText,
    repository: urlText,
    Repository: urlText,
    discussion: urlText,
    'demo-link': urlText,
    tags: {
      oneOf: [
        { type: 'string' },
        { type: 'array', items: {} },
      ],
    },
    'audio-sample': {
      oneOf: [
        { type: 'string' },
        { type: 'array', items: { type: ['string', 'object'] } },
      ],
    },
    readme: { type: 'string' },
    contact: {
      type: 'object',
      properties: {
        email: { type: 'string' },
        website: urlText,
        social: {
          type: 'object',
          additionalProperties: urlText,
        },
      },
      additionalProperties: true,
    },
    panel: { type: 'object' },
    controls: { type: 'object' },
    host: { type: 'object' },
    uf2: { type: 'array' },
  },
  additionalProperties: true,
};

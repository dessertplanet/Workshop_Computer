import { marked } from 'marked';
import sanitizeHtml from 'sanitize-html';

const BLOCK_TAGS = [
  'p', 'br', 'hr', 'blockquote', 'pre', 'code',
  'h1', 'h2', 'h3', 'h4', 'h5', 'h6',
  'ul', 'ol', 'li', 'dl', 'dt', 'dd',
  'strong', 'em', 'del', 's', 'a', 'img',
  'table', 'thead', 'tbody', 'tfoot', 'tr', 'th', 'td',
  'details', 'summary', 'kbd', 'sup', 'sub',
];

const INLINE_TAGS = ['br', 'code', 'strong', 'em', 'del', 's', 'a', 'kbd', 'sup', 'sub'];

function sanitizeOptions(inline = false) {
  return {
    allowedTags: inline ? INLINE_TAGS : BLOCK_TAGS,
    allowedSchemes: ['http', 'https', 'mailto'],
    allowedSchemesByTag: { img: ['http', 'https'] },
    allowProtocolRelative: false,
    disallowedTagsMode: 'discard',
    transformTags: {
      a: (tagName, attribs) => {
        const href = String(attribs.href || '').trim();
        const external = /^https?:\/\//i.test(href);
        return {
          tagName,
          attribs: {
            ...attribs,
            ...(external ? { target: '_blank', rel: 'noopener noreferrer' } : {}),
          },
        };
      },
    },
    allowedAttributes: {
      a: ['href', 'title', 'target', 'rel'],
      img: ['src', 'alt', 'title', 'width', 'height'],
      th: ['align'],
      td: ['align'],
    },
  };
}

export function sanitizeAuthoredHtml(html, { inline = false } = {}) {
  return sanitizeHtml(String(html || ''), sanitizeOptions(inline));
}

export function renderMarkdownBlock(markdown) {
  const source = String(markdown ?? '').trim();
  return source ? sanitizeAuthoredHtml(marked.parse(source)) : '';
}

export function renderMarkdownInline(markdown) {
  const source = String(markdown ?? '').trim();
  return source ? sanitizeAuthoredHtml(marked.parseInline(source), { inline: true }) : '';
}

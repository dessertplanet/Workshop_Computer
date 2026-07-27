# Metadata site development

The metadata site is generated from card metadata, repository content, and the curation files under `tools/sitegen/src/curation/`. Generated output is written to `site/`.

## Install dependencies

From the repository root:

```sh
npm install --prefix tools/sitegen
```

## Build once

```sh
npm run build
```

This performs a clean production build of the complete site.

## Run the development server

```sh
npm run dev
```

The command first performs a complete build, then serves the generated site at <http://localhost:5173/>.

While it is running, the development server watches:

- card metadata at `releases/*/info.yaml`
- discovery and tag curation under `tools/sitegen/src/curation/`
- other site-generator source files and assets

Successful rebuilds automatically refresh connected browsers.

## Incremental rebuilds

After the initial complete build, common metadata and curation edits use faster delta builds:

- Changing one `info.yaml` rediscovers that release and updates its page and dependent indexes.
- Changing `discovery.yml` updates the program-card home page.
- Changing `tags.yml` updates curation-dependent indexes and card pages.

Mixed changes, deleted metadata, and structural or generator changes safely fall back to a clean complete build.
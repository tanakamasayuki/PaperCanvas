# vendor/

Third-party code, committed rather than fetched.

## lgfx-font-tool.js

[lgfx-font-tool](https://www.npmjs.com/package/lgfx-font-tool) v0.1.0, MIT. The
unmodified `dist/lgfx-font-tool.js` from the npm package.

It renders text exactly as LovyanGFX does — verified against the real thing
across all 186 built-in fonts — which is what lets the tool's preview match what
the printer produces.

**Committed rather than loaded from a CDN** so the tool works offline and the
version cannot shift underneath a saved layout.

CJK fonts are not in this file; they are fetched on first use from
`tanakamasayuki.github.io/LGFXFontToolJs/`, which is the same origin this tool
is served from.

Licence: `lgfx-font-tool.LICENSE`.

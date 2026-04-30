// PageShell: global chrome (logo, Tools / Settings dropdowns, primary
// links) wrapping every Preact-rendered page.  The DOM and visual
// design match the legacy `pageHeader` (`Web/html_header.h` +
// `html_header_css.h`) so navigating between Preact-served pages
// (`/live`, `/indexer`) and the still-server-rendered pages
// (`/aoaconfig`, `/calwiz`, etc.) feels like one site.
//
// Dropdowns rely on `:hover` + `:focus-within` selectors only; no JS
// open-state.  iOS Safari taps focus the trigger, which is what reveals
// the menu via `:focus-within`.
//
// Logo source: `tools/web/public/onspeed-logo.png`.  The bundler reads
// the PNG at build time and prepends a `globalThis.ONSPEED_LOGO_DATA_URL`
// constant to the JS bundle.  The dev server emits a
// `<meta name="onspeed-logo" content="/onspeed-logo.png">` tag and
// serves the PNG file directly from `public/`.  PageShell reads either
// source.

import { html } from '../vendor/preact-standalone.js';
import { NAV, navIdForPath } from './nav.js';

function resolveLogoSrc() {
  // Prefer the meta tag (dev server sets a real URL; bundler stub does
  // not).  Fall back to the global the bundler injects ahead of the
  // app code.
  if (typeof document !== 'undefined') {
    const m = document.querySelector('meta[name="onspeed-logo"]');
    if (m && m.content) return m.content;
  }
  if (typeof globalThis !== 'undefined' && globalThis.ONSPEED_LOGO_DATA_URL) {
    return globalThis.ONSPEED_LOGO_DATA_URL;
  }
  return null;
}

function resolveVersion(prop) {
  if (prop) return prop;
  if (typeof document !== 'undefined') {
    const m = document.querySelector('meta[name="onspeed-version"]');
    if (m && m.content) return m.content;
  }
  // TODO: fetch from /api/version once that endpoint lands (PR 2).
  return 'dev';
}

const Dropdown = ({ group, activeId }) => html`
  <li class="dropdown">
    <a href="javascript:void(0)" class="dropbtn"
       tabindex="0" aria-haspopup="true">${group.label}</a>
    <div class="dropdown-content">
      ${group.items.map(it => html`
        <a href=${it.href} class=${it.id === activeId ? 'active' : ''}>${it.label}</a>`)}
    </div>
  </li>`;

const Nav = ({ activeId }) => html`
  <ul>
    <li><a href="/" class=${activeId === 'home' ? 'active' : ''}>Home</a></li>
    ${NAV.dropdowns.map(d => html`<${Dropdown} group=${d} activeId=${activeId} />`)}
    ${NAV.primary.filter(p => p.id !== 'home').map(p => html`
      <li><a href=${p.href} class=${p.id === activeId ? 'active' : ''}>${p.label}</a></li>`)}
  </ul>`;

const Header = ({ version }) => {
  const logoSrc = resolveLogoSrc();
  return html`
    <div class="header-container">
      ${logoSrc
        ? html`<img src=${logoSrc} alt="OnSpeed" />`
        : html`<span class="logo">OnSpeed</span>`}
      <div class="firmware">OnSpeed Version: ${version}</div>
    </div>`;
};

// `active` lets a page name itself when its url-derived id doesn't
// match (e.g. an alias).  Otherwise we look up the active id from
// window.location.pathname.
export function PageShell({ active, version, children }) {
  const path = (typeof location !== 'undefined') ? location.pathname : '/';
  const activeId = active ?? navIdForPath(path);
  const resolvedVersion = resolveVersion(version);
  return html`
    <${Header} version=${resolvedVersion} />
    <${Nav} activeId=${activeId} />
    ${children}`;
}

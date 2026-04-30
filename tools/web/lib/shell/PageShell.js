// PageShell: global chrome (logo, Tools/Settings dropdowns, primary
// links, footer) wrapping every Preact-rendered page.  The server emits
// a stub with `<div id="app" data-page="...">`; entry.js looks up the
// matching page component, renders it inside `<PageShell>`, and the
// shell paints the nav.
//
// Dropdowns use `:focus-within` + `tabindex=0` so they work on iOS
// Safari (where `:hover` alone is unreliable on tap).  No useState
// open-state — the CSS toggles visibility from the focus state of the
// dropdown trigger or any descendant.

import { html } from '../vendor/preact-standalone.js';
import { NAV, navIdForPath } from './nav.js';

const Dropdown = ({ group, activeId }) => {
  const isActive = group.items.some(i => i.id === activeId);
  return html`
    <li class="dropdown">
      <a href="javascript:void(0)" class="dropbtn ${isActive ? 'active' : ''}"
         tabindex="0" aria-haspopup="true">${group.label}</a>
      <div class="dropdown-content">
        ${group.items.map(it => html`
          <a href=${it.href} class=${it.id === activeId ? 'active' : ''}>${it.label}</a>`)}
      </div>
    </li>`;
};

const Nav = ({ activeId }) => html`
  <ul class="onspeed-nav">
    <li><a href="/" class=${activeId === 'home' ? 'active' : ''}>Home</a></li>
    ${NAV.dropdowns.map(d => html`<${Dropdown} group=${d} activeId=${activeId} />`)}
    ${NAV.primary.filter(p => p.id !== 'home').map(p => html`
      <li><a href=${p.href} class=${p.id === activeId ? 'active' : ''}>${p.label}</a></li>`)}
  </ul>`;

const Header = ({ activeId, version }) => html`
  <div class="header-container">
    <a href="/" class="logo-link" aria-label="OnSpeed home">
      <span class="logo-text">OnSpeed</span>
    </a>
    <div class="firmware">${version ? `OnSpeed Version: ${version}` : ''}</div>
    <${Nav} activeId=${activeId} />
  </div>`;

const Footer = () => html`
  <footer class="onspeed-footer">
    <span>OnSpeed Gen3</span>
    <span><a href="https://flyonspeed.org" target="_blank" rel="noopener">flyonspeed.org</a></span>
  </footer>`;

// `active` lets a page name itself when its url-derived id doesn't
// match (e.g. an alias).  Otherwise we look up the active id from
// window.location.pathname.
export function PageShell({ active, version, children }) {
  const path = (typeof location !== 'undefined') ? location.pathname : '/';
  const activeId = active ?? navIdForPath(path);
  // Prefer the version baked into a `<meta name="onspeed-version">`
  // tag if present; falls back to the prop.
  let resolvedVersion = version;
  if (!resolvedVersion && typeof document !== 'undefined') {
    const m = document.querySelector('meta[name="onspeed-version"]');
    if (m && m.content) resolvedVersion = m.content;
  }
  return html`
    <div class="onspeed-shell">
      <${Header} activeId=${activeId} version=${resolvedVersion} />
      <main class="onspeed-main">${children}</main>
      <${Footer} />
    </div>`;
}

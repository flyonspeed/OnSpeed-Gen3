// PageShell: global chrome (logo, Tools / Settings dropdowns, primary
// links) wrapping every Preact-rendered page.  The DOM and visual
// design match the legacy `pageHeader` (`Web/html_header.h` +
// `html_header_css.h`) so navigating between Preact-served pages
// (`/live`, `/indexer`) and the still-server-rendered pages
// (`/aoaconfig`, `/calwiz`, etc.) feels like one site.
//
// Dropdowns: single source of truth is `openMenu` state in `<Nav>`.
// `aria-expanded` on the trigger reflects it, CSS displays the menu
// when `aria-expanded="true"`.  Open paths:
//   - pointer enters the dropdown's <li> (mouseenter) → open it
//   - click / tap on the trigger → toggle it (mobile path)
//   - keyboard Enter/Space on focused trigger → toggle (browser fires click)
// Close paths:
//   - pointer leaves the dropdown's <li> (mouseleave, with a short
//     grace timeout so the cursor can travel from trigger to menu items)
//   - click outside any dropdown
//   - Escape key
//   - clicking a menu item (full-page nav happens, this clears state)
// CSS deliberately does NOT use :hover or :focus-within to open — those
// race the JS state and cause click-to-close to lose to a still-hovering
// cursor.  All input methods funnel through the same `openMenu` state.
//
// Logo source: `tools/web/public/onspeed-logo.png`.  The bundler reads
// the PNG at build time and prepends a `globalThis.ONSPEED_LOGO_DATA_URL`
// constant to the JS bundle.  The dev server emits a
// `<meta name="onspeed-logo" content="/onspeed-logo.png">` tag and
// serves the PNG file directly from `public/`.  PageShell reads either
// source.

import { html, useState, useEffect } from '../vendor/preact-standalone.js';
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

const Dropdown = ({ group, activeId, isOpen, onPointerEnter, onClickToggle }) => html`
  <li class="dropdown" onMouseEnter=${onPointerEnter}>
    <a href="javascript:void(0)" class="dropbtn"
       tabindex="0" aria-haspopup="true"
       aria-expanded=${isOpen ? 'true' : 'false'}
       onClick=${onClickToggle}>${group.label}</a>
    <div class="dropdown-content">
      ${group.items.map(it => html`
        <a href=${it.href} class=${it.id === activeId ? 'active' : ''}>${it.label}</a>`)}
    </div>
  </li>`;

const Nav = ({ activeId }) => {
  // At most one dropdown is open at a time.  Stripe / GitHub / MDN
  // pattern: hovering a sibling tab transitions openMenu, automatically
  // closing whatever was open.
  const [openMenu, setOpenMenu] = useState(null);

  const enterMenu = (id) => () => setOpenMenu(id);
  const toggleMenu = (id) => (e) => {
    e.preventDefault();
    e.stopPropagation();
    setOpenMenu(prev => prev === id ? null : id);
  };

  useEffect(() => {
    if (typeof document === 'undefined') return undefined;
    const handleClickOutside = (e) => {
      if (!e.target || typeof e.target.closest !== 'function') return;
      if (!e.target.closest('.dropdown')) setOpenMenu(null);
    };
    const handleEsc = (e) => {
      if (e.key === 'Escape') setOpenMenu(null);
    };
    // Mouse leaves the whole nav: close.  We listen on the <ul>
    // wrapper; everything we care about is inside it.
    const ul = document.querySelector('#liveview-nav-ul');
    const handleMouseLeave = () => setOpenMenu(null);
    document.addEventListener('click', handleClickOutside);
    document.addEventListener('keydown', handleEsc);
    ul && ul.addEventListener('mouseleave', handleMouseLeave);
    return () => {
      document.removeEventListener('click', handleClickOutside);
      document.removeEventListener('keydown', handleEsc);
      ul && ul.removeEventListener('mouseleave', handleMouseLeave);
    };
  }, []);

  return html`
    <ul id="liveview-nav-ul">
      <li onMouseEnter=${enterMenu(null)}>
        <a href="/" class=${activeId === 'home' ? 'active' : ''}>Home</a></li>
      ${NAV.dropdowns.map(d => html`<${Dropdown} group=${d} activeId=${activeId}
                                                isOpen=${openMenu === d.id}
                                                onPointerEnter=${enterMenu(d.id)}
                                                onClickToggle=${toggleMenu(d.id)} />`)}
      ${NAV.primary.filter(p => p.id !== 'home').map(p => html`
        <li onMouseEnter=${enterMenu(null)}>
          <a href=${p.href} class=${p.id === activeId ? 'active' : ''}>${p.label}</a></li>`)}
    </ul>`;
};

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

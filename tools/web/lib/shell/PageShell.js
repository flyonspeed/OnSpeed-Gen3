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

import { html, useState, useEffect } from '../../../../packages/ui-core/vendor/preact-standalone.js';
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
  // Server-rendered meta tag is the canonical source.  The firmware
  // substitutes BuildInfo::version into every page stub at request
  // time; the dev-server substitutes a literal "dev".  Reading from
  // the DOM at first paint avoids any post-mount fetch flash.
  if (typeof document !== 'undefined') {
    const m = document.querySelector('meta[name="onspeed-version"]');
    if (m && m.content) return m.content;
  }
  return '…';
}

const Dropdown = ({ group, activeId, isOpen, onPointerEnter, onClickToggle }) => html`
  <li class="dropdown" onPointerEnter=${onPointerEnter}>
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
  // At most one dropdown is open at a time.  Two open modes:
  //   - "hover": opened by mouseenter; auto-closes when the cursor
  //     moves to a sibling tab or leaves the nav.
  //   - "click": opened by click/tap; sticky.  Stays open until the
  //     user clicks the trigger again (toggles), clicks outside,
  //     presses Escape, or clicks a menu item.  Hover events do NOT
  //     transition state while click-locked.  This mirrors Stripe /
  //     GitHub: hover gets you a quick peek, click pins.
  // openMenu = null | { id, mode: 'hover'|'click' }
  const [openMenu, setOpenMenu] = useState(null);

  const enterMenu = (id) => (e) => {
    // Pointer Events: only react to real mouse hover.  Touch and pen
    // get pointerType !== 'mouse' and we ignore them — they should
    // only act through `click`.  iOS synthesizes a hover event on
    // first tap of any element with hover-dependent behavior, which
    // races the click handler; ignoring touch pointers here keeps the
    // tap-to-toggle path clean.
    if (e && e.pointerType && e.pointerType !== 'mouse') return;
    // Hovering a different tab always switches (drops any click-lock).
    // Hovering the SAME tab as the current openMenu is a no-op (don't
    // demote a click-lock to hover).
    setOpenMenu(prev => {
      if (prev && prev.id === id) return prev;
      return id ? { id, mode: 'hover' } : null;
    });
  };
  const toggleMenu = (id) => (e) => {
    e.preventDefault();
    e.stopPropagation();
    setOpenMenu(prev => (prev && prev.id === id && prev.mode === 'click') ? null : { id, mode: 'click' });
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
    // Mouse leaves the whole nav: close hover-opens, but leave a
    // click-locked menu alone.
    const ul = document.querySelector('#liveview-nav-ul');
    const handleMouseLeave = () => {
      setOpenMenu(prev => prev && prev.mode === 'click' ? prev : null);
    };
    document.addEventListener('click', handleClickOutside);
    document.addEventListener('keydown', handleEsc);
    ul && ul.addEventListener('mouseleave', handleMouseLeave);
    return () => {
      document.removeEventListener('click', handleClickOutside);
      document.removeEventListener('keydown', handleEsc);
      ul && ul.removeEventListener('mouseleave', handleMouseLeave);
    };
  }, []);

  const isOpen = (id) => openMenu && openMenu.id === id;

  return html`
    <ul id="liveview-nav-ul">
      <li onPointerEnter=${enterMenu(null)}>
        <a href="/" class=${activeId === 'home' ? 'active' : ''}>Home</a></li>
      ${NAV.dropdowns.map(d => html`<${Dropdown} group=${d} activeId=${activeId}
                                                isOpen=${isOpen(d.id)}
                                                onPointerEnter=${enterMenu(d.id)}
                                                onClickToggle=${toggleMenu(d.id)} />`)}
      ${NAV.primary.filter(p => p.id !== 'home').map(p => html`
        <li onPointerEnter=${enterMenu(null)}>
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
  // Version comes from the server-rendered <meta name="onspeed-version">
  // tag (or an explicit prop).  Synchronous DOM read; no post-mount
  // fetch, no banner flash on first paint.
  const resolvedVersion = resolveVersion(version);
  return html`
    <${Header} version=${resolvedVersion} />
    <${Nav} activeId=${activeId} />
    ${children}`;
}

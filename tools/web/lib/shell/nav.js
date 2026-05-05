// OnSpeed page nav manifest.
//
// Single source of truth for the global nav structure used by every
// Preact-served page (via PageShell) and any future legacy-page server-
// side renderer that wants to stay in lockstep with the UI.  Adding a
// new page means adding one entry here.
//
// `id` matches the page key used by the bundler / entry resolver.
// `href` is the URL the firmware serves.  `label` is what the user sees.

export const NAV = {
  // Top-level links.  The legacy `/live` route is gone — its entire
  // information density is in `/indexer` Mode 0 plus the data-fields
  // panel.  The firmware redirects `/live` → `/indexer` so stale
  // bookmarks still land somewhere useful.
  primary: [
    { id: 'home',    href: '/',        label: 'Home' },
    { id: 'indexer', href: '/indexer', label: 'Indexer' },
  ],

  // Dropdowns.  Each entry has a label and a list of child links.
  // Tools / Settings keep the legacy grouping so muscle memory carries
  // over from the previous nav.
  dropdowns: [
    {
      id: 'tools',
      label: 'Tools',
      items: [
        { id: 'logs',    href: '/logs',    label: 'Log Files' },
        { id: 'format',  href: '/format',  label: 'Format SD Card' },
        { id: 'upgrade', href: '/upgrade', label: 'Firmware Upgrade' },
        { id: 'reboot',  href: '/reboot',  label: 'Reboot System' },
      ],
    },
    {
      id: 'settings',
      label: 'Settings',
      items: [
        { id: 'aoaconfig',    href: '/aoaconfig',    label: 'System Configuration' },
        { id: 'sensorconfig', href: '/sensorconfig', label: 'Sensor Calibration' },
        { id: 'calwiz',       href: '/calwiz',       label: 'AOA Calibration Wizard' },
      ],
    },
  ],
};

// Look up a page id from its href.  Used by PageShell to highlight the
// active link.
export function navIdForPath(path) {
  const all = [
    ...NAV.primary,
    ...NAV.dropdowns.flatMap(d => d.items),
  ];
  return all.find(p => p.href === path)?.id ?? null;
}

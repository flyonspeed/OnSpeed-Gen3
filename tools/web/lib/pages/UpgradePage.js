// UpgradePage (/upgrade): firmware OTA file picker + progress bar.
//
// Posts to the legacy `/upload` route as multipart/form-data.  The
// Update streaming lambda in ConfigWebServer.cpp is unchanged; the
// only thing this page replaces is the static HTML that rendered the
// file picker.  XHR is used (not fetch) so we can subscribe to the
// upload-progress events for the progress bar.
//
// On a successful upload the firmware schedules a soft restart and
// returns OK.  We report success and tell the user to wait for the
// device to come back; a separate /reboot poll could chain here
// later if desired.

import { html, useState } from '../../../../packages/ui-core/vendor/preact-standalone.js';
import { PageShell } from '../shell/PageShell.js';

export function UpgradePage() {
  const [file, setFile] = useState(null);
  const [phase, setPhase] = useState('pick');     // pick | uploading | success | failed
  const [percent, setPercent] = useState(0);
  const [error, setError] = useState(null);

  const onSubmit = (e) => {
    e.preventDefault();
    if (!file) {
      setError('Choose a firmware .bin file first.');
      return;
    }
    setError(null);
    setPhase('uploading');
    setPercent(0);

    const fd = new FormData();
    fd.append('update', file);

    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/upload');
    xhr.upload.addEventListener('progress', (ev) => {
      if (ev.lengthComputable) {
        setPercent(Math.round((ev.loaded / ev.total) * 100));
      }
    });
    xhr.addEventListener('load', () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        setPhase('success');
      } else {
        setError(`Upload failed: HTTP ${xhr.status}`);
        setPhase('failed');
      }
    });
    xhr.addEventListener('error', () => {
      setError('Network error during upload.');
      setPhase('failed');
    });
    xhr.addEventListener('abort', () => {
      setError('Upload aborted.');
      setPhase('failed');
    });
    xhr.send(fd);
  };

  return html`
    <${PageShell} active="upgrade">
      <div style=${{ maxWidth: '560px', margin: '0 auto', padding: '12px' }}>
        <h2>Firmware Upgrade</h2>
        <p>Upload an OnSpeed firmware <code>.bin</code> file.  The device will
           reboot automatically once flashing completes.</p>

        ${phase === 'pick' && html`
          <form onSubmit=${onSubmit}>
            <input type="file" accept=".bin" name="update"
                   onChange=${(e) => setFile(e.target.files[0] || null)} />
            <br /><br />
            ${error && html`<p style=${{ color: 'red' }}>${error}</p>`}
            <button type="submit" class="button">Upload</button>
            ${' '}<a href="/">Cancel</a>
          </form>`}

        ${phase === 'uploading' && html`
          <p>Uploading…</p>
          <progress max="100" value=${percent} style=${{ width: '100%' }}>
            ${percent}%
          </progress>
          <p>${percent}%</p>`}

        ${phase === 'success' && html`
          <p>Upload complete.  Wait a few seconds for OnSpeed to reboot, then
             reconnect to its access point.</p>
          <p><a href="/">Home</a></p>`}

        ${phase === 'failed' && html`
          <p style=${{ color: 'red' }}>${error || 'Upload failed.'}</p>
          <p>Power-cycle the box and try again.</p>
          <p><a href="/">Home</a> | <button type="button"
                                            onClick=${() => { setPhase('pick'); setError(null); }}>
            Try again
          </button></p>`}
      </div>
    <//>`;
}

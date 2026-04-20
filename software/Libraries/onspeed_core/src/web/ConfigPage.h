// ConfigPage.h
//
// Platform-free renderer that walks `onspeed::web::kSchema` / `kFlapSchema`
// and emits an HTML5 form whose <input name=...> and <select name=...>
// attributes match what the sketch's HandleConfig() has historically
// produced.  The rendered page is therefore POST-compatible with the
// existing HandleConfigSave() handler — this PR adds the renderer but does
// NOT yet swap the route; PR 4.1b does that once the renderer is
// bench-verified.
//
// The rendered output is deliberately a minimalist HTML5 document (no CSS
// framework, no JavaScript) — the goal for PR 4.1 is functional equivalence
// of form inputs, not visual equivalence.  PR 4.1b will add the production
// header/footer.

#ifndef ONSPEED_CORE_WEB_CONFIG_PAGE_H
#define ONSPEED_CORE_WEB_CONFIG_PAGE_H

#include <string>
#include <string_view>

#include <config/OnSpeedConfig.h>

namespace onspeed::web {

/// Render the full /aoaconfig page for the supplied config.
///
/// The returned string is a complete HTML5 document (doctype through
/// </html>).  All user-controlled string values are HTML-escaped to prevent
/// XSS when a malicious config file flows into the rendered page.
std::string RenderConfigPage(const onspeed::config::OnSpeedConfig& cfg);

/// Escape a string for HTML body/attribute context.  Exposed for the test
/// suite; production callers should use RenderConfigPage.
std::string HtmlEscape(std::string_view input);

}  // namespace onspeed::web

#endif  // ONSPEED_CORE_WEB_CONFIG_PAGE_H

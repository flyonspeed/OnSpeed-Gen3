# dev-server mocks

The dev-server (`server.mjs --mock`) maps `/api/foo/bar` to `mocks/api-foo-bar.json`. Files with a `.sample` suffix are alternate fixtures — copy one over the live filename to preview a different branch. Example:

    cp api-format-status.partial-failure.json.sample api-format-status.json

Renders the FormatPage's `done` screen with the orange "config not saved" warning. Revert with `git checkout`.

# Contributing

OnSpeed is open-source hardware and firmware. Contributions are welcome — bug reports, code review, pull requests, documentation fixes. This section covers the parts of the workflow that aren't obvious from the code alone.

## Pages

- **[Bench Testing](bench-testing.md)** — the manual protocol for stressing a firmware change on real hardware before merge. Required reading for any PR that touches SD writer code, web handlers, or anything that contends on `xWriteMutex`.

## Code and PRs

The firmware repo is at [flyonspeed/OnSpeed-Gen3](https://github.com/flyonspeed/OnSpeed-Gen3). Issues and PRs use standard GitHub workflow. The maintainers review PRs in order of impact; safety-critical changes (audio, AOA computation, logging) get the most scrutiny.

A few standing conventions worth knowing:

- **Build warnings are errors.** The project compiles with `-Werror -Wall -Wextra -Wshadow -Wformat=2 -Wunreachable-code -Wnull-dereference`. New code must compile cleanly.
- **Native unit tests run on every PR.** New algorithm code under `onspeed_core/` should have native tests.
- **The bench is the truth.** See [Bench Testing](bench-testing.md). Native tests alone do not establish that a SD/web-handler PR is safe to merge.
- **Documentation is part of the firmware.** The docs site is sourced from the firmware code; if you change a config parameter, console command, log column, or tone threshold, the docs page that documents it must change in the same PR.

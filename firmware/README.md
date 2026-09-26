# Firmware Projects

| Project | Role | Entry point |
| --- | --- | --- |
| `desk-clock/` | Original multi-page clock firmware, retained separately | [README](desk-clock/README.md) |
| `pocket-home/` | New app-launcher firmware; current device version v0.2.2 | [README](pocket-home/README.md) |

Both target the same board. Build and flash one project at a time. Their source trees and build configurations are independent; changes to one do not automatically update the other.

Create one child directory per independent firmware or experiment.

Recommended naming:

- `waveshare-idf-lvgl-v9/`
- `xiaozhi-v2.2.5/`
- `my-project-name/`

Each project must own its source, configuration, dependency manifest, build
instructions, hardware notes, and release artifacts. Generated files and
secrets must remain inside that project's ignore rules.

See [`CANDIDATE_PROJECTS.md`](CANDIDATE_PROJECTS.md) for projects that were
reviewed but have not yet been imported.

# Grafana energy screen

Replaced original website polling with authenticated Grafana POST /api/ds/query,
querying home-history -> screen_energy. Source credentials are no longer referenced
by the firmware's energy component. Dedicated Grafana Viewer token stays in an
ignored private header. HTTPS verifies the CA bundle and redirects are disabled.

NAS SQL view computes today cost and remaining cost at CNY 1.10/kWh. The view uses
energy_totals for Shanghai-date-aware daily consumption. Null daily values remain
missing, failed requests preserve cached readings, and cached daily readings are
also invalidated locally when the date changes. Source timestamps remain intact;
stale (>2h) is displayed separately from network failure.

Screen shows only today's kWh, today's cost, and remaining cost, with the retained
four-page navigation. Money uses 元 to preserve existing font coverage. Font assets
were not regenerated. Price is read from NAS, not recomputed on the screen.

Host energy-view tests passed, including monetary values, missing readings, stale
status and refresh states; existing Xiaozhi/device render tests also passed.
Production firmware built and flashed. Initial real board query returned 4.41kWh,
4.85 yuan today, 128.28 yuan remaining, stale=false; device HA refresh also succeeded.

Automatic refresh repeated successfully at uptime 74.8s (first at 13.9s), with no
manual input, request failure or unexpected reboot during observation.

-- Run against the NAS home-history database as collector.
CREATE OR REPLACE VIEW screen_energy AS
SELECT t.kwh AS today_kwh, round(t.cost::numeric,2)::double precision AS today_cost,
 round((m.remaining_kwh*1.1)::numeric,2)::double precision AS remaining_cost,
 extract(epoch FROM m.updated_at)::double precision AS updated_at,
 (now()-m.updated_at>interval '2 hours') AS stale,
 1.1::double precision AS price_per_kwh
FROM meter_summary m JOIN energy_totals t ON t.sort=1 WHERE m.id=1;
GRANT SELECT ON screen_energy TO grafana_reader;

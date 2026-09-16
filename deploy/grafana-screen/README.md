# 屏幕读取 Grafana

现有 NAS 家庭监控项目位于 `/volume2/docker/grafana`，由 collector 采集原电量网站，
PostgreSQL 持久保存数据，Grafana 使用 `home-history` 数据源只读访问。

`screen-energy.sql` 提供屏幕的稳定查询视图，已写入 NAS collector/schema.sql 并执行。
固件向 `https://grafana.hz.leaflab.cn:8888/api/ds/query` 发送
`SELECT * FROM screen_energy`，使用单独 `desk-clock-energy` Viewer 服务账号。
实际 token 在忽略的 `grafana_credentials.h` 中，不使用管理员密码。

字段：today_kwh / today_cost / remaining_cost / updated_at / stale / price_per_kwh。
金额在数据库按 1.10 元/度计算，跨日未采集新数据时今日字段为空。
固件每 10 分钟读取一次，按钮可立即重新读取；不会触发原网站请求。
collector 仍按自己的周期采集，屏幕显示真实来源更新时间而非请求时间。

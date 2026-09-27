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

## 2026-09-27 采集容器重启修复

NAS 重启后 `home-collector` 因 `PermissionError: config.json` 持续重启，
Grafana 查询认证正常，但数据停留在 12:13。绿联共享目录 ACL 下，镜像默认用户
`65534:65534` 无法读取挂载配置，即便 POSIX 权限显示为 777。

已在 NAS 的 `docker-compose.monitoring.yml` 的 `collector` 服务中添加
`user: "1002:10"`，使用配置文件所有者的 UID/GID；保留配置只读挂载。
不同 NAS 应匹配其实际配置所有者，不要照抄该 UID，也不需将容器改为 root。

该 Compose 项目名为 `home-monitoring`，更新时执行：

```sh
cd /volume2/docker/grafana
sudo docker compose -p home-monitoring -f docker-compose.monitoring.yml up -d --no-deps collector
```

原 Compose 备份为同目录 `docker-compose.monitoring.yml.before-collector-uid-20260927`。
恢复后来源时间更新至 18:44，Grafana 使用现有屏幕服务账号返回 HTTP 200。
Pocket Home 的凭据保存在已忽略的 `firmware/pocket-home/config.local.h`，不提交到仓库。

# Networking capability matrix (lista §9)

> **Última verificación:** 2026-07-29  
> **Fuente de verdad:** código kernel + ISD BusyBox; smokes QEMU user-net (`rtl8139`).

| capacidad | implementada | probada | limitación | herramienta |
|-----------|--------------|---------|------------|-------------|
| socket(AF_INET, SOCK_DGRAM) | sí | parcial (smokes UDP) | — | `nc` (ISD) |
| socket(AF_INET, SOCK_STREAM) | sí | parcial (TCP smokes) | — | `nc` / `wget` |
| socket(AF_INET, SOCK_RAW, ICMP) | sí | sí | RX = IP+ICMP (Linux ABI) | `ping` |
| bind / connect | sí | parcial | — | — |
| listen / accept | sí | parcial | — | — |
| send/recv / sendto/recvfrom | sí | sí (ICMP/UDP) | — | — |
| poll on SOCK_STREAM | sí | parcial | — | — |
| poll on SOCK_RAW ICMP | sí | smoke path | readable iff RX queued | — |
| SIOCGIF* ioctls | sí | sí | read-mostly | `ifconfig -a` |
| /proc/net/dev | sí | parcial | — | — |
| /proc/net/route | sí | sí | sintetiza connected+default si la tabla soft está vacía | `route -n`, `netstat -rn` |
| /proc/netinfo | sí | parcial | TSV raw | — |
| setitimer(ITIMER_REAL) / alarm | sí | parcial | `ping -c N` sin `-A` necesita SIGALRM mid-syscall (sysret); `ping -c 1` y `-c N -A` OK | BusyBox ping |
| IPv4 + ICMP echo | sí | sí | QEMU user-net `10.0.2.15` → `10.0.2.2` | `ping` |
| UDP / TCP | sí | parcial | — | — |
| DNS | parcial | applet presente | resolver L7 no smoke product | `nslookup` |
| virtio-net / RTL8139 | sí | smoke | — | — |
| AF_NETLINK / ip(8) | **no** | — | no fingir netlink | — |
| DHCP client userspace | bloqueado | — | — | `udhcpc` off |

## Smokes verificados (2026-07-29)

| Smoke | Resultado |
|-------|-----------|
| `ifconfig -a` | PASS — `eth0` `10.0.2.15` |
| `ping -c 2 -A 10.0.2.2` | PASS — 2 RTT, 0% loss, `ttl=255` |
| `route -n` / `netstat -rn` | PASS — lee `/proc/net/route` |
| `nc` / `wget` / `nslookup` | PASS — applets presentes en rootfs ISD |

## ISD BusyBox (`ir0_full`)

Habilitados: `IFCONFIG`, `PING`, `ROUTE`, `NETSTAT`, `NSLOOKUP`, `NC`, `WGET` (sin `UDHCPC`/`HTTPD`/`TELNETD`).

## Pendiente explícito

1. Entrega SIGALRM con redirección de retorno de syscall (sysret/rcx) → `ping -c N` / `-i` sin `-A`.
2. Smokes L7 reales para `nc`/`wget`/`nslookup` (no solo presencia del applet).
3. Sembrar `ip_route_add` desde `ip_init`/DHCP para alinear lista soft y `/proc/net/route`.

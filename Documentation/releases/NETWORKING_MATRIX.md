# Networking capability matrix (lista §9)

> **Última verificación:** 2026-07-29  
> **Fuente de verdad:** código kernel + ISD BusyBox; smokes QEMU user-net (`rtl8139`).

| capacidad | implementada | probada | limitación | herramienta |
|-----------|--------------|---------|------------|-------------|
| socket(AF_INET, SOCK_DGRAM) | sí | parcial (smokes UDP) | — | `nc` (ISD) |
| socket(AF_INET, SOCK_STREAM) | sí | parcial (TCP smokes) | connect timeout userspace incompleto | `nc` / `wget` |
| socket(AF_INET, SOCK_RAW, ICMP) | sí | sí | RX = IP+ICMP (Linux ABI) | `ping` |
| bind / connect | sí | parcial | — | — |
| listen / accept | sí | parcial | — | — |
| send/recv / sendto/recvfrom | sí | sí (ICMP/UDP) | — | — |
| poll on SOCK_STREAM | sí | parcial | — | — |
| poll on SOCK_RAW ICMP | sí | smoke path | readable iff RX queued | — |
| SIOCGIF* ioctls | sí | sí | read-mostly | `ifconfig -a` |
| /proc/net/dev | sí | parcial | — | — |
| /proc/net/route | sí | sí | sintetiza connected+default si la tabla soft está vacía | `route -n`, `netstat -rn` |
| /proc/net/{tcp,udp,raw,unix} | sí (stub) | sí | cabecera Linux vacía (sin filas) | `netstat` |
| /proc/netinfo | sí | parcial | TSV raw | — |
| setitimer(ITIMER_REAL) / alarm | sí | sí | SIGALRM: restorer + `rt_sigreturn` + frame 16-aligned; `signal_enter_pending` evita reentrada | BusyBox ping |
| IPv4 + ICMP echo | sí | sí | QEMU user-net `10.0.2.15` → `10.0.2.2` | `ping` |
| UDP / TCP | sí | parcial | TCP connect a puerto cerrado puede colgar `-w` | — |
| DNS | parcial | sí (L7) | QEMU user DNS `10.0.2.3` | `nslookup` |
| virtio-net / RTL8139 | sí | smoke | — | — |
| AF_NETLINK / ip(8) | **no** | — | no fingir netlink | — |
| DHCP client userspace | bloqueado | — | — | `udhcpc` off |

## Smokes verificados (2026-07-29)

| Smoke | Resultado |
|-------|-----------|
| `ifconfig -a` | PASS — `eth0` `10.0.2.15` |
| `ping -c 2 -A 10.0.2.2` | PASS — 2 RTT, 0% loss, `ttl=255` |
| `ping -c 2 -W 3 10.0.2.2` (sin `-A`) | PASS — 10/10; SIGALRM mid-recvfrom OK |
| `route -n` / `netstat -rn` | PASS — lee `/proc/net/route` |
| `netstat` (default) | PASS — `/proc/net/{tcp,udp,raw,unix}` stub (sin “No such file”) |
| `nslookup 10.0.2.2 10.0.2.3` | PASS — Server/Address vía DNS user-net |
| `nc -w 2 10.0.2.2 80` | FAIL / hang — connect TCP sin progreso de timeout |
| `wget http://10.0.2.2/` | FAIL — SEGV userspace (TCP/HTTP path) |

## ISD BusyBox (`ir0_full`)

Habilitados: `IFCONFIG`, `PING`, `ROUTE`, `NETSTAT`, `NSLOOKUP`, `NC`, `WGET` (sin `UDHCPC`/`HTTPD`/`TELNETD`).

## Pendiente explícito

1. Timeout de `connect` / poll TCP para `nc -w` y `wget` sin colgar ni SEGV.
2. Sembrar `ip_route_add` desde `ip_init`/DHCP para alinear lista soft y `/proc/net/route`.
3. Rellenar filas reales en `/proc/net/{tcp,udp,…}` (hoy solo cabecera).

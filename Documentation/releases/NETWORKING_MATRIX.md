# Networking capability matrix (lista §9)

> **Última verificación:** 2026-07-30  
> **Fuente de verdad:** código kernel + ISD BusyBox; smokes QEMU user-net (`rtl8139`).

| capacidad | implementada | probada | limitación | herramienta |
|-----------|--------------|---------|------------|-------------|
| socket(AF_INET, SOCK_DGRAM) | sí | parcial (smokes UDP) | — | `nc` (ISD) |
| socket(AF_INET, SOCK_STREAM) | sí | parcial | connect interrumpible por SIGALRM (`nc -w`) | `nc` / `wget` |
| socket(AF_INET, SOCK_RAW, ICMP) | sí | sí | RX = IP+ICMP (Linux ABI) | `ping` |
| bind / connect | sí | parcial | — | — |
| listen / accept | sí | parcial | — | — |
| send/recv / sendto/recvfrom | sí | sí (ICMP/UDP) | — | — |
| poll on SOCK_STREAM | sí | parcial | — | — |
| poll on SOCK_RAW ICMP | sí | smoke path | readable iff RX queued | — |
| SIOCGIF* ioctls | sí | sí | read-mostly | `ifconfig -a` |
| /proc/net/dev | sí | parcial | — | — |
| /proc/net/route | sí | sí | soft FIB sembrada desde `ip_init`/DHCP/`NET_SET_CONFIG` | `route -n`, `netstat -rn` |
| /proc/net/{tcp,udp,raw,unix} | sí | sí | filas desde inventarios sock_*; inode = puntero | `netstat` |
| /proc/netinfo | sí | parcial | TSV raw | — |
| setitimer(ITIMER_REAL) / alarm | sí | sí | SIGALRM mid-syscall + connect wait | BusyBox ping / nc |
| IPv4 + ICMP echo | sí | sí | QEMU user-net `10.0.2.15` → `10.0.2.2` | `ping` |
| UDP / TCP | sí | parcial | TCP async/`EINPROGRESS` no | — |
| DNS | parcial | sí (L7) | QEMU user DNS `10.0.2.3` | `nslookup` |
| virtio-net / RTL8139 | sí | smoke | — | — |
| AF_NETLINK / ip(8) | **no** | — | no fingir netlink | — |
| DHCP client userspace | bloqueado | — | — | `udhcpc` off |

## Smokes verificados (2026-07-30)

| Smoke | Resultado |
|-------|-----------|
| `ifconfig -a` | PASS — `eth0` `10.0.2.15` |
| `ping -c 2 -A` / `ping -c 2 -W 3` | PASS |
| `route -n` / `netstat -rn` | PASS — soft FIB |
| `netstat` | PASS — filas tcp/udp/raw/unix cuando hay sockets |
| `nslookup 10.0.2.2 10.0.2.3` | PASS |
| `nc -w 2 10.0.2.2 9` | PASS — sale por alarm/EINTR (no hang) |
| `wget http://10.0.2.2/` | parcial — connect ya no cuelga eterno; HTTP path puede fallar |

## ISD BusyBox (`ir0_full`)

Habilitados: `IFCONFIG`, `PING`, `ROUTE`, `NETSTAT`, `NSLOOKUP`, `NC`, `WGET` (sin `UDHCPC`/`HTTPD`/`TELNETD`).

## Pendiente explícito

1. TCP no bloqueante (`EINPROGRESS` + `poll` POLLOUT) y `SO_SNDTIMEO`/`SO_RCVTIMEO` si hace falta para wget HTTP completo.
2. Estados TCP finos (SYN_SENT, etc.) en `/proc/net/tcp` (hoy: idle/listen/connected aproximados).
3. SEGV al salir de pid1 tras applets BusyBox (post-smoke; no bloquea connect/route/netstat).

# Networking capability matrix (lista §9)

> **Última verificación:** 2026-07-30  
> **Fuente de verdad:** código kernel + ISD BusyBox; smokes QEMU user-net (`rtl8139`).

| capacidad | implementada | probada | limitación | herramienta |
|-----------|--------------|---------|------------|-------------|
| socket(AF_INET, SOCK_DGRAM) | sí | parcial (smokes UDP) | — | `nc` (ISD) |
| socket(AF_INET, SOCK_STREAM) | sí | sí | wire poll + HTTP | `nc` / `wget` |
| socket(AF_INET, SOCK_RAW, ICMP) | sí | sí | RX = IP+ICMP (Linux ABI) | `ping` |
| bind / connect | sí | sí | blocking + `SOCK_NONBLOCK` → `EINPROGRESS` | — |
| listen / accept | sí | parcial | — | — |
| send/recv / sendto/recvfrom | sí | sí | — | — |
| poll on SOCK_STREAM | sí | sí | wire: POLLIN=RX/FIN, POLLOUT=ESTABLISHED | wget STATUSBAR |
| poll on SOCK_RAW ICMP | sí | smoke path | readable iff RX queued | — |
| SO_ERROR / SO_RCVTIMEO / SO_SNDTIMEO | sí | sí (SO_ERROR) | TIMEO en recv stream | — |
| SIOCGIF* ioctls | sí | sí | read-mostly | `ifconfig -a` |
| /proc/net/dev | sí | parcial | — | — |
| /proc/net/route | sí | sí | soft FIB sembrada | `route -n` |
| /proc/net/{tcp,udp,raw,unix} | sí | sí | ESTABLISHED/LISTEN/SYN_SENT | `netstat` |
| /proc/netinfo | sí | parcial | TSV raw | — |
| setitimer(ITIMER_REAL) / alarm | sí | sí | SIGALRM mid-syscall + connect wait | BusyBox ping / nc |
| IPv4 + ICMP echo | sí | sí | QEMU user-net `10.0.2.15` → `10.0.2.2` | `ping` |
| UDP / TCP | sí | sí | un solo outbound wire client | — |
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
| `netstat` | PASS — filas tcp/udp/raw/unix |
| `nslookup 10.0.2.2 10.0.2.3` | PASS |
| `nc -w 2 10.0.2.2 9` | PASS — alarm/EINTR |
| `SOCK_NONBLOCK` connect | PASS — `EINPROGRESS` + `POLLOUT` + `SO_ERROR=0` |
| `wget http://10.0.2.2/` | PASS — HTML (host :80 vía SLIRP); aviso `close failed: Bad file descriptor` no bloquea body |
| pid1 `_exit(0)` post-applets | PASS — sin SEGV en argc slot (repair rechaza RIP en stack) |

## ISD BusyBox (`ir0_full`)

Habilitados: `IFCONFIG`, `PING`, `ROUTE`, `NETSTAT`, `NSLOOKUP`, `NC`, `WGET` (sin `UDHCPC`/`HTTPD`/`TELNETD`).

## Pendiente explícito

1. `wget: close failed: Bad file descriptor` al cerrar stdout/`-O -` ( BusyBox + fd; body OK ).
2. Estados TCP finos restantes (FIN_WAIT*, TIME_WAIT) si hace falta para smokes.
3. Varias asociaciones TCP wire outbound concurrentes (hoy: un `g_out`).

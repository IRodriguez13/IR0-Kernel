# Networking capability matrix (lista §9)

> **Última verificación:** 2026-07-30  
> **Fuente de verdad:** código kernel + ISD BusyBox (`ir0_full`); smoke `NET_FLAGS_PASS` (QEMU user-net `rtl8139`).

| capacidad | implementada | probada | limitación | herramienta |
|-----------|--------------|---------|------------|-------------|
| socket(AF_INET, SOCK_DGRAM) | sí | parcial (smokes UDP) | — | `nc` (ISD) |
| socket(AF_UNIX, SOCK_DGRAM) | sí | sí | ioctl control (musl `if_nametoindex`) | `ping -I eth0` |
| socket(AF_INET, SOCK_STREAM) | sí | sí | wire poll + HTTP | `nc` / `wget` |
| socket(AF_INET, SOCK_RAW, ICMP) | sí | sí | RX = IP+ICMP (Linux ABI) | `ping` |
| bind / connect | sí | sí | blocking + `SOCK_NONBLOCK` → `EINPROGRESS` | — |
| listen / accept | sí | parcial | — | — |
| send/recv / sendto/recvfrom | sí | sí | — | — |
| poll on SOCK_STREAM | sí | sí | wire: POLLIN=RX/FIN, POLLOUT=ESTABLISHED | wget STATUSBAR |
| poll on SOCK_RAW ICMP | sí | smoke path | readable iff RX queued | — |
| SO_ERROR / SO_RCVTIMEO / SO_SNDTIMEO | sí | sí (SO_ERROR) | TIMEO en recv stream | — |
| SO_BINDTODEVICE / IP_TTL / IP_MULTICAST_IF | sí | sí | single-NIC; MULTICAST_IF→bind src | `ping -I` / `-t` |
| close(1)/close(2) consola | sí | sí | Linux-like; wget `-O -` | BusyBox wget |
| SIOCGIF* / SIOCSIF* ioctls | sí | sí | addr/flags/mask/bcast/mtu/hw/metric/txqlen | `ifconfig` |
| SIOCADDRT / SIOCDELRT | sí | sí | soft FIB | `route add` / `del` |
| SIOCGIFINDEX / SIOCGIFNAME | sí | sí | — | musl `if_nametoindex` |
| /proc/net/dev | sí | parcial | — | — |
| /proc/net/route | sí | sí | soft FIB sembrada | `route -n` |
| /proc/net/{tcp,udp,raw,unix} | sí | sí | ESTABLISHED/LISTEN/SYN_SENT/CLOSE_WAIT | `netstat` |
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
| `ifconfig eth0 … up` / `mtu` / `txqueuelen` / broadcast | PASS |
| `route add default gw` / `route -n` / `route del default` | PASS |
| `ping -c 1 -I eth0` / `ping -I 10.0.2.15` | PASS — AF_UNIX dgram + MULTICAST_IF |
| `ping -t 32` / `-q -s 32` | PASS — `IP_TTL` |
| `netstat` / `netstat -rn` | PASS |
| `wget -q -O - http://10.0.2.2/` | PASS |
| `nc -w 2` / `SOCK_NONBLOCK` connect | PASS — SIGALRM → `-ETIMEDOUT` (no die-from-handler SEGV) |
| Tag `NET_FLAGS_PASS` | PASS |
| Tag `NET_STRESS_PASS` (8 rounds, `setup/pid1/net_command_stress.c`) | PASS |
| Tag `NC_ONLY_PASS` (5× `nc -w`) | PASS |

## Stress / stability notes (rc4)

| Issue found under stress | Fix |
|--------------------------|-----|
| Repeat `nc -w` → userspace SEGV @ `rsp-8` | Defer catchable signals in connect; map lone SIGALRM to `-ETIMEDOUT` |
| After parallel load, ping TX ok / RX stuck | RTL8139 read **CBR** (I/O `0x3A`), not `rx_buffer+0x10`; drain on RxOverflow |
| Connect lock stuck after SEGV mid-connect | `tcp_wire_on_process_exit` releases owner |
| Parallel `wget`∥`wget` SEGV | Single `g_out` — stress uses concurrent **ping** only |

## ISD BusyBox (`ir0_full`) — flags ON vs kernel

| applet | flags ON (config) | kernel status |
|--------|-------------------|---------------|
| `ifconfig` | mutate + HW + broadcast+ | SIOCS* OK |
| `ping` | fancy: `-c/-s/-t/-w/-W/-I/-q/-v/-A/-p/-i/-n` | OK (IPv4) |
| `route` | add/del / `-n` | SIOCADDRT/DELRT OK |
| `netstat` | `-rn` + tabla | `/proc/net/*` OK |
| `nslookup` | mini (no `NSLOOKUP_BIG`) | DNS L7 OK |
| `nc` | `-w/-l/-p` (no `-u`; `NC_110_COMPAT=n`) | OK |
| `wget` | `-q/-O/-c` (no `-T`/HTTPS; features off) | OK |

Flags **OFF** en `ir0_full` (no producto): `FEATURE_WGET_TIMEOUT` (`-T`), `NC_110_COMPAT` (`-u`), IPv6, `NSLOOKUP_BIG`, `UDHCPC`.

## Pendiente explícito

1. FIN_WAIT1/2 / TIME_WAIT completos (hoy: CLOSE_WAIT si peer FIN).
2. Varias asociaciones TCP wire outbound concurrentes (hoy: un `g_out`).
3. Multicast real (`IP_ADD_MEMBERSHIP`) — hoy `IP_MULTICAST_IF` solo fija src/bind.

#!/usr/bin/env python3
"""
Bybit WebSocket v5 public SPOT probe.

- Connects to wss://stream.bybit.com/v5/public/spot?compress=false
- Subscribes to tickers.<SYMBOL> or publicTrade.<SYMBOL>
- Handles JSON ping/pong and prints raw+parsed messages
- Reports simple msg/s per symbol for live sanity check

Usage examples:
  python scripts/bybit_ws_probe.py --mode ticker --symbols BTC,ETH,XRP
  python scripts/bybit_ws_probe.py --mode trade  --symbols BTC

Requires: websockets
  pip install websockets
"""
import asyncio
import json
import time
import argparse
from collections import defaultdict

try:
    import websockets
except ImportError as e:
    print("Missing dependency: websockets. Install with: pip install websockets")
    raise

BYBIT_WS = "wss://stream.bybit.com/v5/public/spot?compress=false"


def build_args(mode: str, symbols: list[str]) -> list[str]:
    topic = "tickers" if mode == "ticker" else "publicTrade"
    return [f"{topic}.{s.upper()}USDT" for s in symbols]


async def send_subscribe(ws: websockets.WebSocketClientProtocol, mode: str, symbols: list[str]):
    payload = {"op": "subscribe", "args": build_args(mode, symbols)}
    msg = json.dumps(payload, separators=(",", ":"))
    print("SUBSCRIBE:", msg)
    await ws.send(msg)


async def send_ping(ws: websockets.WebSocketClientProtocol):
    payload = {"op": "ping", "ts": int(time.time() * 1000)}
    await ws.send(json.dumps(payload, separators=(",", ":")))


def j2d(v):
    if isinstance(v, (int, float)):
        return float(v)
    if isinstance(v, str):
        try:
            return float(v)
        except ValueError:
            return 0.0
    return 0.0


def extract_symbol(e: dict, fallback_topic: str | None) -> str:
    s = e.get("symbol") or e.get("s") or ""
    if s:
        return s
    if fallback_topic:
        parts = fallback_topic.split('.')
        if len(parts) >= 2:
            return parts[-1]
    return ""


def extract_price_ts(mode: str, root: dict, e: dict) -> tuple[float, float]:
    if mode == "trade":
        # publicTrade: price 'p', time 'T' (ms)
        price = j2d(e.get("p"))
        ts = j2d(e.get("T")) / 1000.0
    else:
        # tickers: lastPrice or lp; ts may be in root['ts'] or e['ts']
        price = j2d(e.get("lastPrice")) or j2d(e.get("lp"))
        ts_root = j2d(root.get("ts"))
        ts_data = j2d(e.get("ts"))
        ts = (ts_root if ts_root > 0 else ts_data) / 1000.0
    return price, ts


async def recv_loop(ws: websockets.WebSocketClientProtocol, mode: str, symbols: list[str]):
    raw_dump_left = 10
    counters = defaultdict(int)
    last_report = time.time()

    while True:
        msg = await ws.recv()
        # Text expected; if binary appears, just show size
        if isinstance(msg, (bytes, bytearray)):
            print(f"[BIN] frame size={len(msg)} (unexpected with compress=false)")
            continue

        if raw_dump_left > 0:
            print("RAW:", msg[:500])
            raw_dump_left -= 1

        try:
            root = json.loads(msg)
        except json.JSONDecodeError:
            continue

        # Ping/pong & subscribe acks
        if root.get("op") == "ping":
            pong = {"op": "pong", "ts": root.get("ts", int(time.time() * 1000))}
            await ws.send(json.dumps(pong, separators=(",", ":")))
            continue
        if root.get("op") == "pong":
            continue
        if root.get("op") == "subscribe":
            print("ACK subscribe:", root)
            continue

        topic = root.get("topic", "")
        data = root.get("data")
        if data is None:
            continue

        if isinstance(data, list):
            items = data
        elif isinstance(data, dict):
            items = [data]
        else:
            continue

        for e in items:
            if not isinstance(e, dict):
                continue
            symbol = extract_symbol(e, topic)
            price, ts = extract_price_ts(mode, root, e)
            if not symbol or price <= 0 or ts <= 0:
                continue
            print(f"BYBIT {mode.upper()} {symbol} price={price} ts={ts}")
            counters[symbol] += 1

        # Periodic rate report
        now = time.time()
        if now - last_report >= 10:
            dt = now - last_report
            for s in build_args(mode, symbols):
                # s is like 'tickers.BTCUSDT' — extract BTC
                sym = s.split('.')[-1].replace("USDT", "")
                c = counters.get(f"{sym}USDT", 0) or counters.get(sym, 0)
                print(f"  {sym}: {c/dt:.2f} msg/s")
                counters[f"{sym}USDT"] = 0
                counters[sym] = 0
            last_report = now


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=["ticker", "trade"], default="ticker")
    parser.add_argument("--symbols", default="BTC,ETH,XRP", help="Comma-separated list of base symbols (no USDT suffix)")
    args = parser.parse_args()

    symbols = [s.strip().upper() for s in args.symbols.split(',') if s.strip()]
    uri = BYBIT_WS

    reconnect_attempt = 0
    while True:
        try:
            print("CONNECTING:", uri)
            async with websockets.connect(uri, ping_interval=None, ping_timeout=None, max_size=4*1024*1024) as ws:
                reconnect_attempt = 0
                await send_subscribe(ws, args.mode, symbols)
                # App-level ping loop
                async def ping_task():
                    while True:
                        await asyncio.sleep(10)
                        try:
                            await send_ping(ws)
                        except Exception:
                            break
                ptask = asyncio.create_task(ping_task())
                try:
                    await recv_loop(ws, args.mode, symbols)
                finally:
                    ptask.cancel()
        except asyncio.CancelledError:
            break
        except Exception as e:
            delay = min(30, (1 << min(reconnect_attempt, 5))) + (reconnect_attempt % 3)
            print(f"ERROR: {e} — reconnect in {delay}s")
            await asyncio.sleep(delay)
            reconnect_attempt += 1


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass

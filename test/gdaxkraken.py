#!/usr/bin/env python

import ssl
import websocket
#from websocket import create_connection
from json import dumps, loads
from pprint import pprint

ws = websocket.WebSocket(sslopt={"cert_reqs": ssl.CERT_NONE})
ws.connect("wss://ws-feed.pro.coinbase.com")

ws2 = websocket.WebSocket(sslopt={"cert_reqs": ssl.CERT_NONE})
ws2.connect("wss://ws.kraken.com")

params = {
    "type": "subscribe",
    "channels": [{"name": "level2", "product_ids": ["ETH-USD"]}]
}

ws.send(dumps(params))

pairs1 = ["BTC/USD"]

params2 = {
    "event": "subscribe",
    "pair": pairs1,
    "subscription": { "name" : "*"}
    }

ws2.send(dumps(params2))

x = range(0,100000)
for y in x:
    result =  ws.recv()
    if result:
        print("GDAX Received '%s'" % pprint(loads(result)))

    result = ws2.recv()
    if result:
        print("Kraken Received '%s'" % pprint(loads(result)))

ws.close()
ws2.close()
e

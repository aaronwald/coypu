#!/usr/bin/env python

from websocket import create_connection
import json
import coincache_pb2 as cc
import time

if __name__ == "__main__":
    ws = create_connection("ws://34.73.102.30:80/websocket")
    print(ws)
    ws.send(json.dumps({"cmd": "mark", "offset": 0}))

    count = 0
    while True:
        result =  ws.recv()
        coypu_msg = cc.CoypuMessage()
        coypu_msg.ParseFromString(result)
        count = count + 1
        if not count % 100000:
            print(time.time(), count)
    ws.close()
    
"""
        if coypu_msg.type == cc.CoypuMessage.TICK:
            print(coypu_msg.tick)
        elif coypu_msg.type == cc.CoypuMessage.TRADE:
            print(coypu_msg.trade)
"""



#!/usr/bin/env python

from websocket import create_connection
import json
import coincache_pb2 as cc

if __name__ == "__main__":
    ws = create_connection("ws://localhost:8080/websocket")
    ws.send(json.dumps({"cmd": "mark", "seqno": 0}))

    while True:
        result =  ws.recv()
        coypu_msg = cc.CoypuMessage()
        coypu_msg.ParseFromString(result)

        if coypu_msg.type == cc.CoypuMessage.TICK:
            print(coypu_msg.tick)
        elif coypu_msg.type == cc.CoypuMessage.TRADE:
            print(coypu_msg.trade)


    ws.close()

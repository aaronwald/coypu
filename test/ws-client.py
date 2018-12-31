#!/usr/bin/env python3

from queue import Queue
from websocket import create_connection
from threading import Thread

import curses
import sys
import json
import asyncio

stopped = False
ws_queue = Queue()

def do_websocket():
    ws = create_connection("ws://localhost:8080/websocket")
    try:
        #ws.send(json.dumps({"cmd": "mark", "offset": 0}))
        ws.send(json.dumps({"cmd": "mark"}))
        
        while not stopped:
            result =  ws.recv()
            # will need to rework for asnycio
            #            result = yield from asyncio.wait_for(ws.recv, 1.0)
            
            if result:
                ws_queue.put(result)

        ws.close()
    except Exception as e:
        print(e)


    
def go():
    print("foo")

if __name__ == "__main__":
    thread = Thread(target=do_websocket)
    thread.start()
 
    stdscr = curses.initscr()
    curses.start_color()
    curses.noecho()
    curses.cbreak()
    curses.curs_set(0)
    stdscr.keypad(1)
    stdscr.refresh()
    stdscr.nodelay(1)

    curses.init_pair(1, curses.COLOR_RED, curses.COLOR_BLACK)
    curses.init_pair(2, curses.COLOR_GREEN, curses.COLOR_BLACK)
    curses.init_pair(3, curses.COLOR_WHITE, curses.COLOR_BLACK)
    height,width = stdscr.getmaxyx()

    last_y = 0
    products = {}
    seq_no = 0
    while 1:
        c = stdscr.getch()
        
        if c == ord('q'):
            break
        elif c == curses.KEY_RESIZE:
            oldheight = height-1
            height,width = stdscr.getmaxyx()
            if oldheight < height:
                stdscr.addstr(oldheight,0,"")
                stdscr.clrtoeol()

            
        if not ws_queue.empty():
            doc = ws_queue.get(False)
            seq_no = seq_no + 1

            stdscr.addstr(height-1,0, "{:d}".format(seq_no))

            if doc:
                l = doc.split(' ')
                if l[0] == "Trade":
                    product = l[1]
                    if product not in products:
                        products[product] = { 'y': last_y, 'last_bid': 0.0, 'last_ask':0.0, 'vol': '-', 'last':0.0, 'prev':0.0 }
                        products[product]['last_color'] = curses.color_pair(3)
                        last_y = last_y + 1
                        
                    products[product]['vol'] = l[2]
                    products[product]['last'] = float(l[3])


                if l[0] == "Tick":
                    product = l[1]
                    if product not in products:
                        products[product] = { 'y': last_y, 'last_bid': 0.0, 'last_ask':0.0, 'vol': '-', 'last':0.0, 'prev':0.0 }
                        products[product]['last_color'] = curses.color_pair(3)
                        last_y = last_y + 1
                        
                    y = products[product]['y']
                    last_bid = products[product]['last_bid']
                    last_ask = products[product]['last_ask']
                    last = products[product]['last']
                    prev = products[product]['prev']
                    #products[product]['prev'] = last
                    stdscr.addstr(y, 0, product)
                    stdscr.clrtoeol()

                    try:
                        bid_qty = float(int(l[2]))/100000000.0
                        bid_px = float(int(l[3]))/100000000.0
                        ask_px = float(int(l[4]))/100000000.0
                        ask_qty = float(int(l[5]))/100000000.0
                    
                        f = "{:12.4f}".format(bid_qty)
                        stdscr.addstr(y, 15, f)

                        f = "{:12.6f}".format(bid_px)
                        stdscr.addstr(y, 30, f)

                        f = "x {:12.6f}".format(ask_px)
                        stdscr.addstr(y, 43, f)


                        f = "{:12.4f}".format(ask_qty)
                        stdscr.addstr(y, 60, f)

                        f = "{:14.8f}".format(last)
                        if last == prev:
                            stdscr.addstr(y, 80, f, products[product]['last_color'])
                        elif last < prev:
                            stdscr.addstr(y, 80, f, curses.color_pair(1))
                            products[product]['prev'] = last
                            products[product]['last_color'] = curses.color_pair(1)
                        else:
                            stdscr.addstr(y, 80, f, curses.color_pair(2))
                            products[product]['prev'] = last
                            products[product]['last_color'] = curses.color_pair(2)
                                                
                        products[product]['last_bid'] = bid_px
                        products[product]['last_ask'] = ask_px
                    except Exception as e:
                        print >> sys.stderr, e
                        stdscr.addstr(20, 0, "exception" + str(e))

            stdscr.refresh()

    stopped = True


    curses.nocbreak()
    stdscr.keypad(0)
    curses.echo()
    curses.endwin()
    print("Shutting down....")



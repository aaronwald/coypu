#!/usr/bin/python

from Queue import Queue
from websocket import create_connection
from threading import Thread
import curses

stopped = False
ws_queue = Queue()

def do_websocket():
    ws = create_connection("ws://localhost:8080/websocket")
    ws.send("Hello, World")

    while not stopped:
        result =  ws.recv()
        if result:
            ws_queue.put(result)

    ws.close()


if __name__ == "__main__":
    thread = Thread(target = do_websocket, args = [])
    thread.start()

    stdscr = curses.initscr()
    curses.start_color()
    curses.noecho()
    curses.cbreak()
    stdscr.keypad(1)
    stdscr.refresh()
    stdscr.nodelay(1)

    curses.init_pair(1, curses.COLOR_RED, curses.COLOR_BLACK)
    curses.init_pair(2, curses.COLOR_GREEN, curses.COLOR_BLACK)

    last_y = 0
    products = {}
    while 1:
        c = stdscr.getch()
        
        if c == ord('q'):
            break
            
        
        if not ws_queue.empty():
            doc = ws_queue.get(False)
            if doc:
                l = doc.split(' ')
                if l[0] == "Tick":
                    product = l[1]
                    if product not in products:
                        products[product] = { 'y': last_y, 'last_bid': 0.0, 'last_ask':0.0 }
                        last_y = last_y + 1
                    y = products[product]['y']
                    last_bid = products[product]['last_bid']
                    last_ask = products[product]['last_ask']
                    stdscr.addstr(y, 0, product)
                    stdscr.clrtoeol()

                    try:
                        bid_qty = float(int(l[2]))/100000000.0
                        bid_px = float(int(l[3]))/100000000.0
                        ask_px = float(int(l[4]))/100000000.0
                        ask_qty = float(int(l[5]))/100000000.0
                    
                        f = "%4.8f" % bid_qty
                        stdscr.addstr(y, 15, f)

                        f = "%6.2f" % bid_px
                        if bid_px >= last_bid:
                            stdscr.addstr(y, 30, f, curses.color_pair(1))
                        else:
                            stdscr.addstr(y, 30, f, curses.color_pair(2))

                        f = "%6.2f" % ask_px
                        if ask_px >= last_ask:
                            stdscr.addstr(y, 42, f, curses.color_pair(1))
                        else:
                            stdscr.addstr(y, 42, f, curses.color_pair(2))

                        f = "%4.8f" % ask_qty
                        stdscr.addstr(y, 55, f)

                        products[product]['last_bid'] = bid_px
                        products[product]['last_ask'] = ask_px
                    except Exception as e:
                        stdscr.addsr(20, 0, str(e))

            stdscr.refresh()

    stopped = True

    curses.nocbreak()
    stdscr.keypad(0)
    curses.echo()
    curses.endwin()
    print("Shutting down....")



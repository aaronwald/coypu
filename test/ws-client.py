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
                if l[0] == "Vol":
                    product = l[1]
                    if product not in products:
                        products[product] = { 'y': last_y, 'last_bid': 0.0, 'last_ask':0.0, 'vol': '-' }
                        last_y = last_y + 1
                    products[product]['vol'] = l[2]


                if l[0] == "Tick":
                    product = l[1]
                    if product not in products:
                        products[product] = { 'y': last_y, 'last_bid': 0.0, 'last_ask':0.0, 'vol': '-' }
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
                    
                        f = "{:12.4f}".format(bid_qty)
                        stdscr.addstr(y, 15, f)

                        f = "{:12.6f}".format(bid_px)
                        if bid_px > last_bid:
                            stdscr.addstr(y, 30, f, curses.color_pair(2))
                        elif bid_px == last_bid:
                            stdscr.addstr(y, 30, f, curses.color_pair(3))
                        else:
                            stdscr.addstr(y, 30, f, curses.color_pair(1))

                        f = "x {:12.6f}".format(ask_px)
                        if ask_px < last_ask:
                            stdscr.addstr(y, 43, f, curses.color_pair(1))
                        elif ask_px == last_ask:
                            stdscr.addstr(y, 43, f, curses.color_pair(3))
                        else:
                            stdscr.addstr(y, 43, f, curses.color_pair(2))

                        f = "{:12.4f}".format(ask_qty)
                        stdscr.addstr(y, 60, f)

                        vol = products[product]['vol']
                        stdscr.addstr(y, 80, vol)

                        products[product]['last_bid'] = bid_px
                        products[product]['last_ask'] = ask_px
                    except Exception as e:
                        stdscr.addstr(20, 0, str(e))

            stdscr.refresh()

    stopped = True

    curses.nocbreak()
    stdscr.keypad(0)
    curses.echo()
    curses.endwin()
    print("Shutting down....")



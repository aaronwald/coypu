import asyncio
import curses
from websockets import connect
import json
import sys
import logging

logger = logging.getLogger('websockets')
logger.setLevel(logging.INFO)
logger.addHandler(logging.FileHandler("client.log"))


async def coypu(display):
    async with connect("ws://localhost:8080/websocket", ping_interval=600, ping_timeout=10) as cws:
        await cws.send(json.dumps({"cmd": "mark"}))
        while True:
            msg = await cws.recv()
            try:
                display.render(msg)
                # await ?
            except Exception as e:
                print(e)
    
class Display:
    def __init__ (self, loop):
        self.loop = loop

    def __enter__(self):
        self.stdscr = curses.initscr()
        curses.start_color()
        curses.noecho()
        curses.cbreak()
        curses.curs_set(0)
        self.stdscr.keypad(1)
        self.stdscr.refresh()
        self.stdscr.nodelay(1)

        curses.init_pair(1, curses.COLOR_RED, curses.COLOR_BLACK)
        curses.init_pair(2, curses.COLOR_GREEN, curses.COLOR_BLACK)
        curses.init_pair(3, curses.COLOR_WHITE, curses.COLOR_BLACK)
        self.height, self.width = self.stdscr.getmaxyx()

        self.last_y = 0
        self.products = {}
        self.seq_no = 0

        return self

    def __exit__(self, *args):
        curses.nocbreak()
        self.stdscr.keypad(0)
        curses.echo()
        curses.endwin()

    def init_product (self, product):
        self.products[product] = { 'y': self.last_y, 'last_bid': 0.0, 'last_ask':0.0, 'vol': '-', 'last':0.0, 'prev':0.0 }
        self.products[product]['last_color'] = curses.color_pair(3)
        self.last_y = self.last_y + 1
        
        
    def render (self, doc):
        self.seq_no = self.seq_no + 1
        self.stdscr.addstr(self.height-1,0, "{:d}".format(self.seq_no))

        l = doc.split(' ')
        if l[0] == "Trade":
            product = l[1]
            if product not in self.products:
                self.init_product(product)
                        
            self.products[product]['vol'] = l[2]
            self.products[product]['last'] = float(l[3])
        elif l[0] == "Tick":
            product = l[1]
            if product not in self.products:
                self.init_product(product)
                        
            y = self.products[product]['y']
            last_bid = self.products[product]['last_bid']
            last_ask = self.products[product]['last_ask']
            last = self.products[product]['last']
            prev = self.products[product]['prev']
            self.stdscr.addstr(y, 0, product)
            self.stdscr.clrtoeol()
            try:
                bid_qty = float(int(l[2]))/100000000.0
                bid_px = float(int(l[3]))/100000000.0
                ask_px = float(int(l[4]))/100000000.0
                ask_qty = float(int(l[5]))/100000000.0
                    
                f = "{:12.4f}".format(bid_qty)
                self.stdscr.addstr(y, 15, f)

                f = "{:12.6f}".format(bid_px)
                self.stdscr.addstr(y, 30, f)

                f = "x {:12.6f}".format(ask_px)
                self.stdscr.addstr(y, 43, f)
                    
                f = "{:12.4f}".format(ask_qty)
                self.stdscr.addstr(y, 60, f)

                f = "({:12.6f})".format(ask_px-bid_px)
                self.stdscr.addstr(y, 80, f)
                
                f = "{:14.8f}".format(last)
                if last == prev:
                    self.stdscr.addstr(y, 100, f, self.products[product]['last_color'])
                elif last < prev:
                    self.stdscr.addstr(y, 100, f, curses.color_pair(1))
                    self.products[product]['prev'] = last
                    self.products[product]['last_color'] = curses.color_pair(1)
                else:
                    self.stdscr.addstr(y, 100, f, curses.color_pair(2))
                    self.products[product]['prev'] = last
                    self.products[product]['last_color'] = curses.color_pair(2)
                                                
                self.products[product]['last_bid'] = bid_px
                self.products[product]['last_ask'] = ask_px
            except Exception as e:
                self.stdscr.addstr(20, 0, "exception" + str(e))

        self.stdscr.refresh()


    async def get_ch(self):
        while True:
            char = await self.loop.run_in_executor(None, self.stdscr.getch)
            if char == ord('q'):
                loop.stop()
            elif char == curses.KEY_RESIZE:
                oldheight = height-1
                self.height,self.width = stdscr.getmaxyx()
                if oldheight < self.height:
                    self.stdscr.addstr(oldheight,0,"")
                    self.stdscr.clrtoeol()

                    
if __name__ == '__main__':
    loop = asyncio.get_event_loop()

    with Display(loop) as display:
        task1 = loop.create_task(display.get_ch())
        task2 = loop.create_task(coypu(display))
        loop.run_forever()

        # cleans up display for some reason
        task1.cancel()
        try:
            loop.run_until_complete(task1)
        except:
            pass

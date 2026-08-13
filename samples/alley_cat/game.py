"""Alley Cat: a small real-time game on the pydos.tui core.

The scenery is composed once into a backdrop Buffer; every frame blits
it, stamps the score and the colored sprites on top and presents — the
C engine then writes only the cells that changed since the last frame.
Input drains the keyboard queue without blocking and FrameClock keeps
the pace while yielding the CPU.

The game model (Actor, movement, collisions, scoring) is pure and
screen-free; tests/catmodel.py exercises it headless.
"""

from dataclasses import dataclass
from pydos.tui import Buffer, Color, Rect, Screen, Style
from pydos.tui.keys import Key
from pydos.tui.events import KeyEvent
from pydos.tui.dosinput import DosInput
from pydos.tui.clock import FrameClock, sleep_ms
from sprites import Sprite


@dataclass
class Actor:
    x: int
    y: int
    symbol: str
    color: int
    active: bool = True


class AlleyCatGame:
    def __init__(self):
        self.cat = Actor(4, 20, Sprite.CAT, Color.YELLOW)
        self.dog = Actor(50, 20, Sprite.DOG, Color.LIGHT_RED)
        self.fish = Actor(68, 3, Sprite.FISH, Color.LIGHT_CYAN)
        self.dog_step = -1
        self.score = 0
        self.lives = 3
        self.frame = 0
        self.running = True
        self.screen = None
        self.input = None
        self.backdrop = None
        self.frame_buffer = None
        self.base = Style(Color.LIGHT_GRAY, Color.BLACK)

    # -- model (kept pure: tests/catmodel.py runs it without a screen) - #

    def reset_round(self):
        self.cat.x = 4
        self.cat.y = 20
        self.dog.x = 50
        self.dog.y = 20
        self.fish.x = 68
        self.fish.y = 3
        self.fish.active = True

    def move_cat(self, dx, dy):
        next_x = self.cat.x + dx
        next_y = self.cat.y + dy
        if next_x < 2:
            next_x = 2
        if next_x > 76:
            next_x = 76
        if next_y < 2:
            next_y = 2
        if next_y > 20:
            next_y = 20
        self.cat.x = next_x
        self.cat.y = next_y

    def update(self):
        self.frame += 1
        if self.frame % 2 == 0:
            self.dog.x += self.dog_step
            if self.dog.x <= 20 or self.dog.x >= 72:
                self.dog_step = -self.dog_step

        if self.cat.y == self.dog.y and abs(self.cat.x - self.dog.x) <= 1:
            self.lives -= 1
            if self.lives <= 0:
                self.running = False
            else:
                self.reset_round()

        if (self.fish.active and self.cat.y == self.fish.y
                and abs(self.cat.x - self.fish.x) <= 2):
            self.score += 100
            self.fish.active = False
            self.reset_round()

    # -- input --------------------------------------------------------- #

    def handle_input(self):
        event = self.input.poll()
        if event is None or not isinstance(event, KeyEvent):
            return
        key = event.key
        if key == Key.ESCAPE:
            self.running = False
        elif key == Key.LEFT:
            self.move_cat(-1, 0)
        elif key == Key.RIGHT:
            self.move_cat(1, 0)
        elif key == Key.UP:
            self.move_cat(0, -1)
        elif key == Key.DOWN:
            self.move_cat(0, 1)

    def wait_key(self):
        while True:
            event = self.input.poll()
            if isinstance(event, KeyEvent):
                return event.key
            sleep_ms(20)

    # -- render -------------------------------------------------------- #

    def build_backdrop(self, width, height):
        back = Buffer(width, height)
        back.clear(self.base)
        back.box(Rect(0, 0, width, height), self.base,
                 "Alley Cat - PyDOS edition")
        floor_y = 5
        while floor_y <= 17:
            back.hline(1, floor_y + 1, 78, "-", self.base)
            window_x = 7
            while window_x < 75:
                back.text(window_x, floor_y, Sprite.WINDOW, self.base)
                window_x += 10
            floor_y += 4
        back.text(10, 20, Sprite.TRASH, self.base)
        back.text(15, 20, Sprite.TRASH, self.base)
        back.text(2, 22, "Arrows: move  Esc: quit", self.base)
        return back

    def draw(self):
        frame = self.frame_buffer
        frame.blit(self.backdrop, 0, 0)
        frame.text(52, 22, "Score: " + str(self.score), self.base)
        frame.text(68, 22, "Lives: " + str(self.lives), self.base)
        frame.text(self.cat.x, self.cat.y, self.cat.symbol,
                   Style(self.cat.color, Color.BLACK))
        frame.text(self.dog.x, self.dog.y, self.dog.symbol,
                   Style(self.dog.color, Color.BLACK))
        if self.fish.active:
            frame.text(self.fish.x, self.fish.y, self.fish.symbol,
                       Style(self.fish.color, Color.BLACK))
        self.screen.present(frame)

    def show_title(self):
        title = Buffer(self.screen.width, self.screen.height)
        accent = Style(Color.LIGHT_CYAN, Color.BLACK)
        title.clear(accent)
        title.box(Rect(0, 0, self.screen.width, self.screen.height),
                  accent, "PyDOS samples")
        lines = [
            (7, "ALLEY CAT"),
            (9, "Climb the building and catch the fish"),
            (11, "Avoid the dog in the alley"),
            (14, "Press an arrow key to begin or Esc to quit"),
        ]
        i = 0
        while i < len(lines):
            row = lines[i][0]
            text = lines[i][1]
            title.text((self.screen.width - len(text)) // 2, row,
                       text, accent)
            i += 1
        self.screen.present(title)
        if self.wait_key() == Key.ESCAPE:
            self.running = False

    def run(self):
        self.screen = Screen()
        self.input = DosInput(False)
        self.backdrop = self.build_backdrop(self.screen.width,
                                            self.screen.height)
        self.frame_buffer = Buffer(self.screen.width, self.screen.height)
        clock = FrameClock(55)
        with self.screen:
            self.show_title()
            while self.running:
                self.handle_input()
                self.update()
                self.draw()
                clock.wait()


def run_game():
    game = AlleyCatGame()
    game.run()

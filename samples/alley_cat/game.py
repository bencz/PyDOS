from dataclasses import dataclass
from pydos.io.tui import Canvas, Color, Key, Screen, delay, read_key
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
        self.screen = Screen(Color.LIGHT_GRAY, Color.BLACK)
        self.cat = Actor(4, 20, Sprite.CAT, Color.YELLOW)
        self.dog = Actor(50, 20, Sprite.DOG, Color.LIGHT_RED)
        self.fish = Actor(68, 3, Sprite.FISH, Color.LIGHT_CYAN)
        self.dog_step = -1
        self.score = 0
        self.lives = 3
        self.frame = 0
        self.running = True

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

    def handle_input(self):
        key = read_key()
        if key is None:
            return
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

    def draw_building(self, canvas):
        floor_y = 5
        while floor_y <= 17:
            canvas.draw_hline(1, floor_y + 1, 78, "-")
            window_x = 7
            while window_x < 75:
                canvas.draw_text(window_x, floor_y, Sprite.WINDOW)
                window_x += 10
            floor_y += 4
        canvas.draw_text(10, 20, Sprite.TRASH)
        canvas.draw_text(15, 20, Sprite.TRASH)

    def draw(self):
        canvas = Canvas(self.screen.width, self.screen.height)
        canvas.draw_box(0, 0, self.screen.width, self.screen.height,
                        "Alley Cat - PyDOS edition")
        self.draw_building(canvas)
        canvas.draw_text(2, 22, "Arrows: move  Esc: quit")
        canvas.draw_text(52, 22, "Score: " + str(self.score))
        canvas.draw_text(68, 22, "Lives: " + str(self.lives))
        self.screen.present(canvas, Color.LIGHT_GRAY, Color.BLACK)
        self.screen.write(self.cat.x, self.cat.y, self.cat.symbol,
                          self.cat.color, Color.BLACK)
        self.screen.write(self.dog.x, self.dog.y, self.dog.symbol,
                          self.dog.color, Color.BLACK)
        if self.fish.active:
            self.screen.write(self.fish.x, self.fish.y, self.fish.symbol,
                              self.fish.color, Color.BLACK)

    def show_title(self):
        canvas = Canvas(self.screen.width, self.screen.height)
        canvas.draw_box(0, 0, self.screen.width, self.screen.height,
                        "PyDOS samples")
        canvas.center(7, "ALLEY CAT")
        canvas.center(9, "Climb the building and catch the fish")
        canvas.center(11, "Avoid the dog in the alley")
        canvas.center(14, "Press an arrow key to begin or Esc to quit")
        self.screen.present(canvas, Color.LIGHT_CYAN, Color.BLACK)
        key = read_key()
        while key is None:
            delay(20)
            key = read_key()
        if key == Key.ESCAPE:
            self.running = False

    def run(self):
        with self.screen:
            self.show_title()
            while self.running:
                self.handle_input()
                self.update()
                self.draw()
                delay(55)


def run_game():
    game = AlleyCatGame()
    game.run()

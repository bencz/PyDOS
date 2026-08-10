from game import AlleyCatGame


game = AlleyCatGame()
print(game.cat)

game.move_cat(-100, -100)
print(game.cat.x, game.cat.y)
game.move_cat(200, 200)
print(game.cat.x, game.cat.y)

game.cat.x = game.fish.x
game.cat.y = game.fish.y
game.update()
print(game.score)
print(game.cat.x, game.cat.y)

game.cat.x = game.dog.x
game.cat.y = game.dog.y
game.update()
print(game.lives)
print(game.running)

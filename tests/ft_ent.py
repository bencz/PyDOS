# ft_ent.py - 4-level entity hierarchy

class Entity:
    def __init__(self, name: str, hp: int) -> None:
        self.name = name
        self.hp = hp
        self.alive: bool = True

    def take_damage(self, amt: int) -> None:
        self.hp -= amt
        if self.hp <= 0:
            self.hp = 0
            self.alive = False

    def status(self) -> str:
        if self.alive:
            return self.name + " hp=" + str(self.hp)
        return self.name + " DEAD"

class Character(Entity):
    def __init__(self, name: str, hp: int, level: int) -> None:
        super().__init__(name, hp)
        self.level = level
        self.xp: int = 0

    def gain_xp(self, amount: int) -> None:
        self.xp += amount
        if self.xp >= self.level * 100:
            self.level += 1
            self.xp = 0

    def status(self) -> str:
        return self.name + " L" + str(self.level) + " hp=" + str(self.hp)

class Warrior(Character):
    def __init__(self, name: str) -> None:
        super().__init__(name, 150, 1)
        self.armor: int = 10

    def take_damage(self, amt: int) -> None:
        reduced: int = amt - self.armor
        if reduced < 0:
            reduced = 0
        self.hp -= reduced
        if self.hp <= 0:
            self.hp = 0
            self.alive = False

class Mage(Character):
    def __init__(self, name: str) -> None:
        super().__init__(name, 80, 1)
        self.mana: int = 100

    def cast(self, cost: int) -> bool:
        if self.mana >= cost:
            self.mana -= cost
            return True
        return False

    def status(self) -> str:
        return self.name + " L" + str(self.level) + " hp=" + str(self.hp) + " mp=" + str(self.mana)

w: Warrior = Warrior("Conan")
m: Mage = Mage("Gandalf")
print(w.status())
print(m.status())
w.take_damage(25)
m.take_damage(30)
print(w.status())
print(m.status())
w.gain_xp(150)
print(w.status())
print(m.cast(40))
print(m.cast(40))
print(m.cast(40))
print(m.status())

class Animal:
    def __init__(self, name: str, sound: str) -> None:
        self.name = name
        self.sound = sound

    def speak(self) -> str:
        return self.name + " says " + self.sound

class Dog(Animal):
    def __init__(self, name: str) -> None:
        super().__init__(name, "Woof")

    def fetch(self, item: str) -> str:
        return self.name + " fetches " + item

dog: Dog = Dog("Rex")
print(dog.speak())
print(dog.fetch("ball"))

cat: Animal = Animal("Whiskers", "Meow")
print(cat.speak())

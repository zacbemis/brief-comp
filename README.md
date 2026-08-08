# Brief

## Comments

```
// line comment
/* block comment */
```

## Primitive Types

| Type    | Description       |
| ------- | ----------------- |
| `bool`  | boolean           |
| `int`   | integer           |
| `uint`  | unsigned integer  |
| `shrt`  | short             |
| `ushrt` | unsigned short    |
| `lng`   | long              |
| `ulng`  | unsigned long     |
| `flt`   | float             |
| `dub`   | double            |
| `chr`   | character         |
| `str`   | string            |
| `unit`  | default type      |

Arrays use `T[N]`:

```
int[5]
```

## Dynamic Types

```
Vec
String

let mut x = Vec(int, [1, 2, 3])
let mut y = String("Hi")
```

## Bindings

| Keyword | Meaning                  |
| ------- | ------------------------ |
| `let`   | binding cannot change    |
| `var`   | binding can change       |
| `mut`   | value can change         |
| `shr`   | object is shared         |

## Tuples

```
let pair = (1, 2)
```

## Structs

```
struct Animal {
  int id
  str species
}
```

## Enums

```
enum Color {
  Red
  Green
  Blue
}
```

## Operators

```
x + y
x - y
x * y
x / y

x++
y--
x += y
x -= y
x *= y
x /= y
```

## Functions

```
int add(int x, int y) {
  x + y
}
```

Lambdas:

```
add(x, y) => x + y
```

## Move Semantics

By default values are moved:

```
let user = User("Alice", 20)
let newUser = take(user)
user.name // error: user has been moved and cannot be accessed
```

`shr` makes a value shared instead of moved:

```
let shr user = User("Bob", 30)
let newUser = take(user) // user is moved to newUser variable but can still be accessed from the original user variable
```

Shared values alias, unique values are moved.

## Mutability

Both the field and the binding must be `mut` to mutate through a function:

```
struct Dog {
  mut str name
}

speak(Dog dog) => "woof " + dog.name
rename(mut Dog dog, string newName) => dog.name = newName

let Milo = Dog(Milo)
speak(Milo)
rename(Milo, "Spark") // error: Milo is not mutable

let mut Spark = Dog(Spark)
rename(Spark, "Milo") // success
```

## Pattern Matching

```
enum Shape {
  circle(flt radius)
  rectangle(flt width, flt height)
}

flt area(Shape shape) {
  match shape {
    circle(r) => 3.14 * r * r
    rectangle(w, h) => w * h
  }
}
```

## Generics

```
struct Box<T> {
    T val
}

T unbox(Box<T> box) {
    box.val
}

let intBox = Box(42)
unbox(intBox) // returns 42
```

## Control Flow

```
if y == 0 {
  err("Cannot divide by zero")
} else {
  ok(x / y)
}

for i : 0..len(arr) {
  if arr[i] == target {
    some(i);;
  }
}
```

## Result and Option

```
enum Result<T, E> {
  ok(T)
  err(E)
}

enum Option<T> {
  some(T)
  none
}
```

## Modules

```
module User

struct User {
  str name
  int age
}

pub Result<User, str> createUser(str name, int age) {
  if age < 0 {
    err("Age cannot be negative")
  } else {
    ok(User(name, age))
  }
}
```

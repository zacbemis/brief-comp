// comment
/* comment */

ung // unsinged
sng // signed
bol x
int x
sht x
lng x
flt x
dub x
str x
chr x

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

; // is an immutability modifier that is applied to declarations

let x = 5 // x = 5 and cannot change binding
let bird = bird() // type is bird
bird.id = 5// The binding for bird stays the same but the value can change
let; bird = bird() // This means that the binding for bird and the value for bird cannot change

var x = 5 // x = 5 and can change binding
var bird = bird() // type is bird
bird = bird() // The binding for bird can change
var; bird = bird() // This means the bird binding can change but you cannot mutate bird so bird.id = 5 would error

shr // means the value is shared meaning it is gc managed and 
let shr x = 5 // shared mutable int x initialized to 5 using type implication

int add(int x, int y) { // function that returns an int and takes two int parameters x and y
  x + y // implicit return
}

x = add(5, 10)

str greaterThan10(int x) {
  if (x > 10) {
    "greater than 10" // implicit return
  } else {
    "less than or equal to 10" // implicit return
  }
}

print(greaterThan10(x))

int count(int x, int num) {
  for (int i : 0..num) {
    x++
  }
  x
}



# General coding style for lgraph

## No camelCase. Use underscores to separate words:

```cpp
foo_bar = new Foo_bar(3);
```

## No tabs, indentation is 2 spaces

Make sure to configure your editor to use 2 spaces

You can configure your text editor to do this automatically

## Keep column widths short

- Less than 80 characters if at all possible (meaning not compromising
  readability)
- Less than 130 characters always

You can configure your text editor to do this automatically

## Avoid trailing spaces

You can configure your text editor to highlight them

## Classes start with upper case, but just first word.

```cpp
class Sweet_potato {
```

## Use C++11 iterators not ::iterator

```cpp
for(auto idx:g->unordered()) {
}
```

## Use "auto" and "const auto" when possible.


```cpp
for(auto idx:g->unordered()) {
  for(const auto &c:g->out_edges(idx)) {
```

## Use fmt::print to print messages for debugging

```cpp
fmt::print("This is a debug message, name = {}, id = {}\n",g->get_name(), idx);
```

## Use console log lg to report errors/warnings/info

```cpp
console->error("inou_yaml can only have a yaml_input or a graph_name, not both\n");
console->info("inou_yaml output:{} input:{} graph:{}", output, input, graph_name);
```

## Use bitarray class to have a compact bitvector marker

```cpp
bitarray visited(g->max_size());
```

## Use assert extensively / be meaningful whenever possible in assertions

This usually means use meaningful variable names and conditions that are easy to understand.
If the meaning is not clear from the assertion, use a comment in the same line.
This way, when the assertion is triggered it is easy to identify the problem.

```cpp
assert(n_edges > 0); //at least one edge needed to perform this function
```

## Develop in debug mode and benchmark in release mode


## Use compact if/else brackets

```cpp
std::vector<LGraph *> Inou_yaml::generate() {

  if (opack.graph_name!="") {
     // ...
  }else{
     // ..
  }
```


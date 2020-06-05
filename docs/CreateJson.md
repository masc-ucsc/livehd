WIP

# fromlg Translation Methodology

## class Inou_Tojson

### Public Methods

```cpp
Inou_Tojson(LGraph *toplg_, PrettySbuffWriter &writer_)
```

Creates the Inou_Tojson object. We (TODO).

### Private Fields

# LGraph API Functions Used

## lgraph

```cpp
lg->get_name()
```

Description.

```cpp
lg->each_sub_unique_fast([&](Node&, Lg_type_id lgid)
```

Description.

```cpp
LGraph *lg = LGraph::open(toplg->get_path(), lgid);
```

Description.

```cpp
lg->each_graph_input([&](const Node_pin &out_pin)
```

Description.

```cpp
lg->each_graph_output([&](const Node_pin &out_pin)
```

Description.

```cpp
for (const auto &node: lg->forward())
```

Description.


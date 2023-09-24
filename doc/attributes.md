# List of Attributes
## command line argument parsing related attributes

| **attribute** | **target**                   | **description**                                               |
|---------------|------------------------------|---------------------------------------------------------------|
| ``CLI``       | user-defined type definition | define user-defined type for declarative command line parsing |
| ``Flag``      | field declaration            | define no-arg (flag) option                                   |
| ``Option``    | field declaration            | define has-arg option                                         |
| ``Arg``       | field declaration            | define positional argument                                    |

### ``CLI`` attribute
only allowed for user-defied type definition

| **param** | **type**   | **default**  | **description**              |
|-----------|------------|--------------|------------------------------|
| name      | ``String`` | empty string | default command name         |
| verbose   | ``Bool``   | true         | default usage message format |

### ``Flag`` attribute
only allowed for field declaration that is ``Bool`` or ``Bool?`` type

| **param** | **type**   | **default**                               | **description**                                               |
|-----------|------------|-------------------------------------------|---------------------------------------------------------------|
| short     | ``String`` | empty string                              | define short option name(single ascii character)              |
| long      | ``String`` | equivalent to lower kebab case field name | define long option name (at-least 2 or more ascii characters) |
| required  | ``Bool``   | false                                     | required or not                                               |
| stop      | ``Bool``   | false                                     | stop option recognition                                       |
| store     | ``Bool``   | true                                      | set value to field                                            |
| help      | ``String`` | empty string                              | help message of this flag                                     |


### ``Option`` attribute
only allowed for field declaration that is ``String``, ``String?``, ``Int`` or ``Int?`` type

| **param**   | **type**      | **default**                               | **description**                                               |
|-------------|---------------|-------------------------------------------|---------------------------------------------------------------|
| short       | ``String``    | empty string                              | define short option name(single ascii character)              |
| long        | ``String``    | equivalent to lower kebab case field name | define long option name (at-least 2 or more ascii characters) |
| required    | ``Bool``      | false                                     | required or not                                               |
| opt         | ``Bool``      | false                                     | following argument is optional or not                         |
| stop        | ``Bool``      | false                                     | stop option recognition                                       |
| default     | ``String``    | null                                      | default argument for optional argument                        |
| placeholder | ``String``    | equivalent to upper snake case field name | placeholder for argument                                      |
| range       | ``(Int,Int)`` | null                                      | range of integer argument (inclusive, inclusive)              |
| choice      | ``[String]``  | null                                      | valid choice of string argument                               |
| help        | ``String``    | empty string                              | help message of this option                                   |


### ``Arg`` attribute
only allowed for field declaration that is ``String``, ``String?``, ``[String]``, ``[String]?``, ``Int`` or ``Int?`` type

| **param**   | **type**      | **default**                               | **description**                                               |
|-------------|---------------|-------------------------------------------|---------------------------------------------------------------|
| required    | ``Bool``      | false                                     | required or not                                               |
| placeholder | ``String``    | equivalent to upper snake case field name | placeholder for argument                                      |
| range       | ``(Int,Int)`` | null                                      | range of integer argument (inclusive, inclusive)              |
| choice      | ``[String]``  | null                                      | valid choice of string argument                               |

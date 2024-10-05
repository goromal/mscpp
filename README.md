# mscpp (MicroServices Cpp)

![example workflow](https://github.com/goromal/mscpp/actions/workflows/test.yml/badge.svg)

Useful template classes for creating multithreaded, interdependent microservices in C++.

**Design Principles**

- Eliminate side effects within a microservice
- Ensure that all developer logic is contained within highly testable pure functions in the form of state machine
definitions
- Make illegal states unrepresentable and force the microservice developer to:
  - consider every corner case from outside inputs
  - partition functionality responsibly in order to keep the FSMs tractable
- Impose time limits on every action the microservice takes
- Eliminate the need for locking the store (in fact, DO NOT lock it)

**Potential Pitfalls**

- The service templates are restrictive on purpose. Only consider using them if reliability and testability far outweigh the need for development velocity.
- A developer must stick to the virtual override functions and FSM definitions to avoid side effects--otherwise the design principles are moot.

## Defining a single MicroService

See [the unit test classes](./tests/example-services/) for example service implementations.

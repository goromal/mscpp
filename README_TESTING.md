# Testing Guide for mscpp

## Running Tests

### Basic Test Execution

```bash
# Build and run all tests (Debug mode with sanitizers)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ./unit-tests
```

### Running with ThreadSanitizer

To run tests with ThreadSanitizer (detects data races):

```bash
# Build with ThreadSanitizer
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DSECONDARY_SANITIZERS=ON
cmake --build build

# Run tests with suppressions file
cd build
TSAN_OPTIONS="suppressions=$(cd .. && pwd)/.tsan-suppressions" ./unit-tests
```

Or use the cpp-helper tool:

```bash
# Use absolute path for TSAN_OPTIONS
TSAN_OPTIONS="suppressions=$(pwd)/.tsan-suppressions" cpp-helper challenge all
```

### Running with AddressSanitizer (default in Debug mode)

```bash
# Build with AddressSanitizer (default)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ./unit-tests
```

## ThreadSanitizer Suppressions

The `.tsan-suppressions` file contains known acceptable data races in test code that do not reflect production usage patterns. See the file for detailed explanations of each suppression.

### Why Suppressions Are Needed

The IOAdapterTest stress test intentionally creates concurrent writes to ports from multiple threads to verify that the action scheduling queue is thread-safe under extreme conditions. This test pattern does NOT reflect production usage:

- **Production pattern (thread-safe)**: I/O thread → scheduleLogicalAction() (thread-safe) → reactor thread writes ports (single-threaded)
- **Test pattern (intentional race)**: Multiple threads → simulateExternalEvent() → concurrent port writes

The suppression allows CI tests to pass while maintaining test coverage for the action scheduling mechanism.

## Test Categories

- **Unit Tests**: Store pure functions and isolated components
- **Integration Tests**: Full reactor FSM state transitions (59 test cases)
- **Thread Safety Tests**: IOAdapter concurrent access patterns
- **Example Implementations**: grpc_echo_example and ros2_echo_example

## CI/CD Integration

For CI/CD pipelines, ensure the TSAN_OPTIONS environment variable is set:

```yaml
# Example GitHub Actions
- name: Run tests with ThreadSanitizer
  run: |
    TSAN_OPTIONS="suppressions=${{ github.workspace }}/.tsan-suppressions" \
    cmake -B build -DCMAKE_BUILD_TYPE=Debug -DSECONDARY_SANITIZERS=ON
    cmake --build build
    cd build && ./unit-tests
```

## Test Results

All tests should pass with:
- ✅ 59 test cases
- ✅ 489 assertions
- ✅ Zero sanitizer errors (with suppressions configured)

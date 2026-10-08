# FastDDS-Bridge

FastDDS-Bridge connects Fast DDS applications to Rosbridge-compatible tools such
as [Lichtblick](https://lichtblick-suite.github.io/lichtblick/). It reads type
schemas directly from IDL files and exposes matching DDS topics over a
Rosbridge WebSocket. No ROS runtime, ROS message packages, or per-topic YAML is
required.

## Lichtblick

Use the [hosted Lichtblick app](https://lichtblick-suite.github.io/lichtblick/)
or run your own deployment from the
[Lichtblick source repository](https://github.com/lichtblick-suite/lichtblick).
In Lichtblick, add a Rosbridge connection to the bridge's WebSocket URL. For a
local Lichtblick deployment at `http://localhost:8080`, use
`ws://localhost:9090`. The hosted app uses HTTPS, so connect it to a publicly
reachable secure WebSocket endpoint (`wss://`).

## Build

Requirements: Conan 2, CMake 3.27+, and a C++17 compiler.

```sh
conan profile detect
conan build . --output-folder=. --build=missing -s build_type=Release
```

## Run

Pass one or more IDL files, or a directory containing IDLs. Directories are
searched recursively.

```sh
source build/Release/generators/conanrun.sh
./build/Release/fastdds_bridge --idl examples/idl --domain 0 --port 9090
```

The bridge discovers DDS reader and writer endpoints at runtime. It exposes
every discovered topic whose type is defined by the supplied IDLs. The
Rosbridge topic name is the DDS topic name with a leading `/` added when needed.
Every exposed topic supports both Rosbridge `subscribe` and `publish`. Topics
appear in Lichtblick after a matching DDS endpoint is discovered.

The bridge's default WebSocket URL is `ws://localhost:9090`.

The sample publisher advertises three example topics from the supplied IDLs:

```sh
source build/Release/generators/conanrun.sh
./build/Release/fastdds_bridge_example_publisher --idl examples/idl
```

The examples cover plain scalar/string fields, nested structures, a bounded
sequence, a fixed array, an enum, and a keyed `VehicleState` topic. The sample
publisher writes vehicle IDs `101` and `202` repeatedly; writes with the same
`device_id` update one DDS instance, while each distinct ID represents a
separate instance. For Fast DDS's OMG JSON representation, enum values use the
fully-qualified IDL literal, such as `demo::msg::AUTONOMOUS`.

## Checks

Build and run the DDS key-instance check with:

```sh
conan build . --output-folder=. --build=missing -s build_type=Release
ctest --test-dir build/Release --output-on-failure
```

With the bridge and example publisher running, the Rosbridge smoke test also
checks schema discovery for nested/keyed types, both sample vehicle IDs,
sequence and enum data, and publishing a keyed message through the WebSocket:

```sh
ROSBRIDGE_URL=ws://localhost:9090 node scripts/rosbridge-smoke.mjs
```

## IDL inputs

You can pass files individually:

```sh
./build/Release/fastdds_bridge --idl path/to/Telemetry.idl --idl path/to/Pose.idl
```

All aggregated types declared in the files are registered as runtime Fast DDS
types. The bridge matches discovered DDS endpoint type names to those IDL types.
Fast DDS documents limitations for runtime IDL parsing, including maps,
bitsets, bitmasks, custom annotations, inheritance, and member-ID annotations.

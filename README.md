# FastDDS-Bridge

FastDDS-Bridge exposes matching Fast DDS topics through a Rosbridge-compatible
WebSocket for Lichtblick. It loads message schemas directly from IDL files; no
ROS runtime, ROS message packages, or topic-by-topic YAML is needed.

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
Every exposed topic supports both Rosbridge `subscribe` and `publish`.
Topics appear in Lichtblick after a matching DDS endpoint is discovered.

The Rosbridge WebSocket is at `ws://localhost:9090`. In Lichtblick at
`http://localhost:8080`, add a Rosbridge connection using that WebSocket URL.

The sample publisher advertises two example topics from the supplied IDLs:

```sh
source build/Release/generators/conanrun.sh
./build/Release/fastdds_bridge_example_publisher --idl examples/idl
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

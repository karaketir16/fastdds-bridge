# FastDDS-Bridge

Standalone Rosbridge-compatible WebSocket bridge for a configured set of Fast
DDS topics. IDL files define the data types; the bridge does not require ROS
packages or a ROS runtime.

## Build

Requirements: Conan 2, CMake 3.27+, and a C++17 compiler.

```sh
conan profile detect
conan build . --output-folder=. --build=missing -s build_type=Release
```

Conan installs the CMake build tool and the Fast DDS, IXWebSocket, yaml-cpp,
and nlohmann/json dependencies. The sample configuration is an explicit topic
allowlist: DDS topics outside this file are not created or exposed.

## Run

```sh
source build/Release/generators/conanrun.sh
./build/Release/fastdds_bridge --config config/bridge.yaml
```

In another terminal, publish sample DDS messages:

```sh
source build/Release/generators/conanrun.sh
./build/Release/fastdds_bridge_example_publisher --config config/bridge.yaml
```

The bridge serves a Rosbridge-compatible WebSocket at `ws://localhost:9090`.
In Lichtblick at `http://localhost:8080`, add a Rosbridge connection and use
that WebSocket URL. The sample publisher produces `/demo/telemetry` and
`/demo/path`; both can be subscribed to from Lichtblick. The bridge also
accepts `publish` requests for configured entries whose `allow_publish` is
`true`.

## Add your IDL topics

Add one configuration entry for each DDS topic and type to expose:

```yaml
topics:
  - dds_topic: VehicleStatus
    idl: ../your-idl/VehicleStatus.idl
    type_name: vehicle::msg::VehicleStatus
    rosbridge_topic: /vehicle/status
    wire_type: vehicle_msgs/msg/VehicleStatus
    allow_publish: false
```

`type_name` is the fully scoped IDL type name. `wire_type` is optional; by
default it is derived from `type_name` by replacing `::` with `/`. It is used
as a Rosbridge type identifier for schema discovery and does not need to refer
to an installed ROS package. IDL paths are resolved relative to the
configuration file. Restart the bridge after changing the allowlist.

The bridge uses Fast DDS runtime IDL parsing and DynamicData. It does not run
Fast DDS-Gen or require ROS. Supported runtime IDL constructs include
structures, primitive and string members, nested types, aliases, arrays,
sequences, unions, and enums. Fast DDS documents limitations including maps,
bitsets, bitmasks, custom annotations, inheritance, and member-ID annotations.

To check only configuration, type loading, and DDS entity creation without
opening the WebSocket port:

```sh
source build/Release/generators/conanrun.sh
./build/Release/fastdds_bridge --config config/bridge.yaml --validate-config
```

With the bridge and example publisher running, exercise Rosbridge discovery,
sample delivery, publish validation, and allowlist filtering with Node.js:

```sh
node scripts/rosbridge-smoke.mjs
```

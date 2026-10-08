import assert from 'node:assert/strict';

const url = process.env.ROSBRIDGE_URL ?? 'ws://localhost:9090';

function connect() {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(url);
    const pending = [];
    const waiters = [];
    ws.addEventListener('open', () => resolve({
      ws,
      pending,
      waiters,
      next(predicate, timeoutMs = 4000) {
        const index = pending.findIndex(predicate);
        if (index >= 0) return Promise.resolve(pending.splice(index, 1)[0]);
        return new Promise((resolveMessage, rejectMessage) => {
          const waiter = { predicate, resolve: resolveMessage, timer: undefined };
          waiters.push(waiter);
          waiter.timer = setTimeout(() => {
            const at = waiters.indexOf(waiter);
            if (at >= 0) waiters.splice(at, 1);
            rejectMessage(new Error(`Timed out waiting for a Rosbridge message from ${url}`));
          }, timeoutMs);
        });
      },
      send(message) { ws.send(JSON.stringify(message)); },
      close() { ws.close(); },
    }));
    ws.addEventListener('message', (event) => {
      const message = JSON.parse(event.data);
      const at = waiters.findIndex((waiter) => waiter.predicate(message));
      if (at >= 0) {
        const waiter = waiters.splice(at, 1)[0];
        clearTimeout(waiter.timer);
        waiter.resolve(message);
      }
      else pending.push(message);
    });
    ws.addEventListener('error', () => reject(new Error(`Could not connect to ${url}`)));
  });
}

const first = await connect();
const second = await connect();
try {
  first.send({ op: 'call_service', service: '/rosapi/topics', id: 'topics' });
  const topics = await first.next((message) => message.op === 'service_response' && message.id === 'topics');
  assert.equal(topics.result, true);
  assert.deepEqual([...topics.values.topics].sort(),
    ['/DemoPoseSequence', '/DemoTelemetry', '/DemoVehicleState'].sort());

  first.send({ op: 'call_service', service: '/rosapi/topic_type', id: 'type', args: { topic: '/DemoTelemetry' } });
  const type = await first.next((message) => message.op === 'service_response' && message.id === 'type');
  assert.equal(type.values.type, 'demo/msg/Telemetry');

  first.send({ op: 'call_service', service: '/rosapi/message_details', id: 'details', args: { type: type.values.type } });
  const details = await first.next((message) => message.op === 'service_response' && message.id === 'details');
  assert.deepEqual(details.values.typedefs[0].fieldnames, ['sample_sequence', 'value', 'label', 'active']);

  first.send({ op: 'call_service', service: '/rosapi/message_details', id: 'path-details', args: { type: 'demo/msg/PoseSequence' } });
  const pathDetails = await first.next((message) => message.op === 'service_response' && message.id === 'path-details');
  assert.deepEqual(pathDetails.values.typedefs[0].fieldtypes, ['int32', 'demo/msg/Point3', 'string']);
  assert.equal(pathDetails.values.typedefs.some((definition) => definition.type === 'demo/msg/Point3'), true);

  first.send({ op: 'call_service', service: '/rosapi/topic_type', id: 'vehicle-type', args: { topic: '/DemoVehicleState' } });
  const vehicleType = await first.next((message) => message.op === 'service_response' && message.id === 'vehicle-type');
  assert.equal(vehicleType.values.type, 'demo/msg/VehicleState');
  first.send({ op: 'call_service', service: '/rosapi/message_details', id: 'vehicle-details',
    args: { type: vehicleType.values.type } });
  const vehicleDetails = await first.next((message) => message.op === 'service_response' && message.id === 'vehicle-details');
  const vehicleDefinition = vehicleDetails.values.typedefs.find((definition) => definition.type === vehicleType.values.type);
  assert.deepEqual(vehicleDefinition.fieldnames, ['device_id', 'name', 'mode', 'position', 'route', 'covariance']);
  assert.deepEqual(vehicleDefinition.fieldtypes,
    ['int32', 'string', 'demo/msg/DriveMode', 'demo/msg/Vector3', 'demo/msg/Waypoint', 'float64']);
  assert.equal(vehicleDefinition.fieldarraylen[4], 0);
  assert.equal(vehicleDefinition.fieldarraylen[5], 3);
  assert.equal(vehicleDetails.values.typedefs.some((definition) => definition.type === 'demo/msg/Vector3'), true);
  assert.equal(vehicleDetails.values.typedefs.some((definition) => definition.type === 'demo/msg/Waypoint'), true);
  assert.equal(vehicleDetails.values.typedefs.some((definition) =>
    definition.type === 'demo/msg/DriveMode' && definition.constnames.includes('demo::msg::AUTONOMOUS')), true);

  first.send({ op: 'subscribe', topic: '/DemoTelemetry', type: type.values.type });
  first.send({ op: 'subscribe', topic: '/DemoPoseSequence', type: 'demo/msg/PoseSequence' });
  first.send({ op: 'subscribe', topic: '/DemoVehicleState', type: vehicleType.values.type });
  const nestedSample = await first.next((message) => message.op === 'publish' && message.topic === '/DemoPoseSequence');
  assert.equal(nestedSample.msg.path.length, 3);
  assert.equal(nestedSample.msg.path[1].y, 0.5);
  const vehicleIds = new Set();
  let vehicleSample;
  while (vehicleIds.size < 2) {
    vehicleSample = await first.next((message) => message.op === 'publish' &&
      message.topic === '/DemoVehicleState' && [101, 202].includes(message.msg?.device_id));
    vehicleIds.add(vehicleSample.msg.device_id);
  }
  assert.deepEqual([...vehicleIds].sort(), [101, 202]);
  assert.equal(vehicleSample.msg.position.z, 0.5);
  assert.equal(vehicleSample.msg.route.length >= 1, true);
  assert.equal(typeof vehicleSample.msg.mode, 'string');
  second.send({ op: 'subscribe', topic: '/DemoTelemetry', type: type.values.type });
  second.send({ op: 'unsubscribe', topic: '/DemoTelemetry' });
  first.send({
    op: 'publish', topic: '/DemoTelemetry', type: type.values.type,
    msg: { sample_sequence: 9001, value: 12.5, label: 'websocket smoke', active: true },
  });
  const echo = await first.next((message) => message.op === 'publish' &&
    message.topic === '/DemoTelemetry' && message.msg?.label === 'websocket smoke');
  assert.equal(echo.msg.sample_sequence, 9001);

  first.send({
    op: 'publish', topic: '/DemoVehicleState', type: vehicleType.values.type,
    msg: {
      device_id: 4242, name: 'websocket rover', mode: 'demo::msg::MANUAL',
      position: { x: 10, y: 20, z: 1 },
      route: [{ x: 11, y: 21, speed: 2.5 }], covariance: [0.1, 0.0, 0.1],
    },
  });
  const vehicleEcho = await first.next((message) => message.op === 'publish' &&
    message.topic === '/DemoVehicleState' && message.msg?.device_id === 4242);
  assert.equal(vehicleEcho.msg.route[0].speed, 2.5);
  await assert.rejects(
    second.next((message) => message.op === 'publish' && message.msg?.label === 'websocket smoke', 500),
    /Timed out waiting/,
  );

  first.send({ op: 'publish', topic: '/DemoTelemetry', msg: { sample_sequence: 'wrong' } });
  const invalid = await first.next((message) => message.op === 'status' && message.level === 'error');
  assert.match(invalid.msg, /IDL type/);

  first.send({ op: 'publish', topic: '/unknown_topic', msg: {} });
  const denied = await first.next((message) => message.op === 'status' && message.msg.includes('not been discovered'));
  assert.equal(denied.level, 'error');
  console.log('Rosbridge smoke check passed: discovery, schema, subscriptions, DDS publish, and schema rejection.');
} finally {
  first.close();
  second.close();
}

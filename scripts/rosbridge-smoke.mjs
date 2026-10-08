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
  assert.deepEqual(topics.values.topics, ['/demo/telemetry', '/demo/path']);
  assert.equal(topics.values.topics.includes('/not-allowlisted'), false);

  first.send({ op: 'call_service', service: '/rosapi/topic_type', id: 'type', args: { topic: '/demo/telemetry' } });
  const type = await first.next((message) => message.op === 'service_response' && message.id === 'type');
  assert.equal(type.values.type, 'fastdds_bridge/msg/Telemetry');

  first.send({ op: 'call_service', service: '/rosapi/message_details', id: 'details', args: { type: type.values.type } });
  const details = await first.next((message) => message.op === 'service_response' && message.id === 'details');
  assert.deepEqual(details.values.typedefs[0].fieldnames, ['sample_sequence', 'value', 'label', 'active']);

  first.send({ op: 'call_service', service: '/rosapi/message_details', id: 'path-details', args: { type: 'fastdds_bridge/msg/PoseSequence' } });
  const pathDetails = await first.next((message) => message.op === 'service_response' && message.id === 'path-details');
  assert.deepEqual(pathDetails.values.typedefs[0].fieldtypes, ['int32', 'fastdds_bridge/msg/Point3', 'string']);
  assert.equal(pathDetails.values.typedefs.some((definition) => definition.type === 'fastdds_bridge/msg/Point3'), true);

  first.send({ op: 'subscribe', topic: '/demo/telemetry', type: type.values.type });
  first.send({ op: 'subscribe', topic: '/demo/path', type: 'fastdds_bridge/msg/PoseSequence' });
  const nestedSample = await first.next((message) => message.op === 'publish' && message.topic === '/demo/path');
  assert.equal(nestedSample.msg.path.length, 3);
  assert.equal(nestedSample.msg.path[1].y, 0.5);
  second.send({ op: 'subscribe', topic: '/demo/telemetry', type: type.values.type });
  second.send({ op: 'unsubscribe', topic: '/demo/telemetry' });
  first.send({
    op: 'publish', topic: '/demo/telemetry', type: type.values.type,
    msg: { sample_sequence: 9001, value: 12.5, label: 'websocket smoke', active: true },
  });
  const echo = await first.next((message) => message.op === 'publish' &&
    message.topic === '/demo/telemetry' && message.msg?.label === 'websocket smoke');
  assert.equal(echo.msg.sample_sequence, 9001);
  await assert.rejects(
    second.next((message) => message.op === 'publish' && message.msg?.label === 'websocket smoke', 500),
    /Timed out waiting/,
  );

  first.send({ op: 'publish', topic: '/demo/telemetry', msg: { sample_sequence: 'wrong' } });
  const invalid = await first.next((message) => message.op === 'status' && message.level === 'error');
  assert.match(invalid.msg, /IDL type/);

  first.send({ op: 'publish', topic: '/not-allowlisted', msg: {} });
  const denied = await first.next((message) => message.op === 'status' && message.msg.includes('allowlist'));
  assert.equal(denied.level, 'error');
  console.log('Rosbridge smoke check passed: discovery, schema, subscriptions, DDS publish, schema rejection, allowlist rejection.');
} finally {
  first.close();
  second.close();
}

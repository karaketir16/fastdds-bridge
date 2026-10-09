import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { performance } from 'node:perf_hooks';

const [bridgeExecutable, publisherExecutable] = process.argv.slice(2);
assert.ok(bridgeExecutable && publisherExecutable,
  'usage: node scripts/rate-limit-smoke.mjs BRIDGE_EXECUTABLE PUBLISHER_EXECUTABLE');

function start(executable, args) {
  const child = spawn(executable, args, { cwd: process.cwd(), stdio: ['ignore', 'pipe', 'pipe'] });
  let output = '';
  child.stdout.setEncoding('utf8').on('data', (text) => { output += text; });
  child.stderr.setEncoding('utf8').on('data', (text) => { output += text; });
  child.outputText = () => output;
  return child;
}

function stop(child) {
  if (!child || child.exitCode !== null || child.signalCode !== null) return Promise.resolve();
  child.kill('SIGTERM');
  return Promise.race([
    new Promise((resolve) => child.once('exit', resolve)),
    delay(2000).then(() => { child.kill('SIGKILL'); }),
  ]);
}

async function waitForServer(url, bridge) {
  const deadline = Date.now() + 8000;
  while (Date.now() < deadline) {
    if (bridge.exitCode !== null) {
      throw new Error(`Bridge exited before accepting WebSocket clients:\n${bridge.outputText()}`);
    }
    try {
      return await new Promise((resolve, reject) => {
        const socket = new WebSocket(url);
        const timer = setTimeout(() => {
          socket.close();
          reject(new Error('WebSocket connection timed out'));
        }, 500);
        socket.addEventListener('open', () => {
          clearTimeout(timer);
          resolve(socket);
        }, { once: true });
        socket.addEventListener('error', () => {
          clearTimeout(timer);
          reject(new Error('Bridge is not listening yet'));
        }, { once: true });
      });
    } catch {
      await delay(100);
    }
  }
  throw new Error(`Bridge did not start at ${url}:\n${bridge.outputText()}`);
}

const temporaryDirectory = await mkdtemp(path.join(os.tmpdir(), 'fastdds-rate-limit-'));
const configPath = path.join(temporaryDirectory, 'rates.json');
const domain = 80 + Math.floor(Math.random() * 100);
const port = 20000 + Math.floor(Math.random() * 20000);
await writeFile(configPath, JSON.stringify({ topic_rates_hz: { '/DemoTelemetry': 0.5 } }));

let bridge;
let publisher;
let socket;
try {
  bridge = start(bridgeExecutable, [
    '--idl', 'examples/idl', '--domain', String(domain), '--port', String(port), '--config', configPath,
  ]);
  socket = await waitForServer(`ws://127.0.0.1:${port}`, bridge);

  const received = { '/DemoTelemetry': [], '/DemoPoseSequence': [] };
  const sampleWaiters = [];
  let serviceId = 0;
  socket.addEventListener('message', (event) => {
    const message = JSON.parse(event.data);
    if (message.op === 'publish' && received[message.topic]) {
      received[message.topic].push({ message, at: performance.now() });
      for (let index = sampleWaiters.length - 1; index >= 0; index--) {
        const waiter = sampleWaiters[index];
        if (received[waiter.topic].length >= waiter.count) {
          sampleWaiters.splice(index, 1);
          clearTimeout(waiter.timer);
          waiter.resolve(received[waiter.topic]);
        }
      }
    }
  });

  publisher = start(publisherExecutable, ['--idl', 'examples/idl', '--domain', String(domain)]);
  const topicsDeadline = Date.now() + 8000;
  let discovered = false;
  while (!discovered && Date.now() < topicsDeadline) {
    const id = `topics-${serviceId++}`;
    const response = new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('Timed out waiting for topic discovery')), 1000);
      const listener = (event) => {
        const message = JSON.parse(event.data);
        if (message.op === 'service_response' && message.id === id) {
          socket.removeEventListener('message', listener);
          clearTimeout(timer);
          resolve(message);
        }
      };
      socket.addEventListener('message', listener);
    });
    socket.send(JSON.stringify({ op: 'call_service', service: '/rosapi/topics', id }));
    try {
      const result = await response;
      discovered = result.result && result.values.topics.includes('/DemoTelemetry') &&
        result.values.topics.includes('/DemoPoseSequence');
    } catch {
      await delay(100);
    }
  }
  assert.equal(discovered, true, `Expected demo topics to be discovered.\n${bridge.outputText()}\n${publisher.outputText()}`);

  socket.send(JSON.stringify({ op: 'subscribe', topic: '/DemoTelemetry', type: 'demo/msg/Telemetry' }));
  socket.send(JSON.stringify({ op: 'subscribe', topic: '/DemoPoseSequence', type: 'demo/msg/PoseSequence' }));
  const waitForSamples = (topic, count, timeoutMs) => new Promise((resolve, reject) => {
    if (received[topic].length >= count) return resolve(received[topic]);
    const waiter = { topic, count, resolve, timer: undefined };
    waiter.timer = setTimeout(() => {
      const index = sampleWaiters.indexOf(waiter);
      if (index !== -1) sampleWaiters.splice(index, 1);
      reject(new Error(`Timed out waiting for ${count} samples on ${topic}`));
    }, timeoutMs);
    sampleWaiters.push(waiter);
  });
  const telemetrySamples = await waitForSamples('/DemoTelemetry', 2, 7000);
  await waitForSamples('/DemoPoseSequence', 2, 1500);
  assert.ok(telemetrySamples[1].at - telemetrySamples[0].at >= 1700,
    `Expected telemetry to be limited to 0.5 Hz; got ${(telemetrySamples[1].at - telemetrySamples[0].at).toFixed(0)} ms between samples`);
  assert.ok(received['/DemoPoseSequence'].length >= 2,
    'Unconfigured topics should continue streaming without a rate limit');
  console.log('Topic rate-limit smoke check passed: configured telemetry is throttled; unconfigured pose data is not.');
} finally {
  socket?.close();
  await Promise.all([stop(publisher), stop(bridge)]);
  await rm(temporaryDirectory, { recursive: true, force: true });
}

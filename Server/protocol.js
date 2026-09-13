export const PROTOCOL_VERSION = 7;
export const WEBSOCKET_PATH = "/play";

export const WORKER_IPC = Object.freeze({
  step: 1,
  command: 2,
  forfeit: 3,
  snapshotRequest: 4,
  ready: 128,
  snapshot: 129,
  acknowledgement: 130,
  result: 131,
});


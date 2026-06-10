import { describe, expect, it } from 'vitest';

import { normalizePortName, readEnv } from '../src/env';

describe('normalizePortName', () => {
  it('uppercases and replaces spaces with underscores', () => {
    expect(normalizePortName('voice chat')).toBe('VOICE_CHAT');
    expect(normalizePortName('  query ')).toBe('QUERY');
    expect(normalizePortName('Voice')).toBe('VOICE');
  });
});

describe('readEnv', () => {
  it('reads the default port', () => {
    const env = readEnv({ GAMEFLOW_DEFAULT_PORT: '7777' });
    expect(env.ports.default).toBe(7777);
  });

  it('reads named ports with normalization', () => {
    const env = readEnv({ GAMEFLOW_VOICE_CHAT_PORT: '9000' });
    expect(env.ports.get('voice chat')).toBe(9000);
    expect(env.ports.get('VOICE_CHAT')).toBe(9000);
  });

  it('reads TLS ports separately', () => {
    const env = readEnv({
      GAMEFLOW_DEFAULT_PORT: '7777',
      GAMEFLOW_TLS_DEFAULT_PORT: '8443',
      GAMEFLOW_TLS_VOICE_PORT: '8444',
    });
    expect(env.ports.tls.default).toBe(8443);
    expect(env.ports.tls.get('voice')).toBe(8444);
  });

  it('returns undefined for missing or invalid values', () => {
    const env = readEnv({ GAMEFLOW_DEFAULT_PORT: 'not-a-port', GAMEFLOW_EMPTY_PORT: '' });
    expect(env.ports.default).toBeUndefined();
    expect(env.ports.get('empty')).toBeUndefined();
    expect(env.ports.get('missing')).toBeUndefined();
    expect(env.ports.tls.default).toBeUndefined();
  });

  it('exposes region and build id', () => {
    const env = readEnv({ GAMEFLOW_REGION: 'us-east-1', GAMEFLOW_BUILD_ID: 'build-42' });
    expect(env.region).toBe('us-east-1');
    expect(env.buildId).toBe('build-42');
  });
});

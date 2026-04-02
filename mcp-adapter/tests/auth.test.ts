import assert from 'node:assert/strict';
import test from 'node:test';
import { extractBearerToken, isAuthorized } from '../src/auth.js';

test('extractBearerToken parses bearer credentials', () => {
  assert.equal(extractBearerToken('Bearer sk-qodeloc-dev'), 'sk-qodeloc-dev');
  assert.equal(extractBearerToken('bearer token-with-spaces is ok'), 'token-with-spaces is ok');
  assert.equal(extractBearerToken('Basic abc'), '');
  assert.equal(extractBearerToken(undefined), '');
});

test('isAuthorized matches allowed api keys', () => {
  assert.equal(isAuthorized('Bearer sk-qodeloc-dev', ['sk-qodeloc-dev']), true);
  assert.equal(isAuthorized('Bearer invalid', ['sk-qodeloc-dev']), false);
  assert.equal(isAuthorized(undefined, ['sk-qodeloc-dev']), false);
});

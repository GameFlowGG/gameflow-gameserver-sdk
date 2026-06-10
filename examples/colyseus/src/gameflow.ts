// Shared GameFlow SDK instance. The SDK is vendored at src/gameflow-sdk.js
// (the built bundle, zero dependencies) until it is published to npm; then
// this becomes: import { GameFlow } from '@gameflow.gg/gameserver-sdk'
// @ts-ignore vendored build artifact has no type declarations
import { GameFlow } from './gameflow-sdk.js';

export const gf = await GameFlow.connect();

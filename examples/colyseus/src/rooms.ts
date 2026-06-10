import { Room, type Client } from 'colyseus';

import { gf } from './gameflow.js';

export class GameRoom extends Room {
  onCreate(): void {
    this.onMessage('chat', (client, message) => {
      this.broadcast('chat', { from: client.sessionId, message });
    });
  }

  async onJoin(client: Client): Promise<void> {
    await gf.players.connect(client.sessionId);
    console.log(
      `[server] ${client.sessionId} joined (${gf.players.count()}/${gf.players.capacity()})`
    );
  }

  async onLeave(client: Client): Promise<void> {
    await gf.players.disconnect(client.sessionId);
    console.log(
      `[server] ${client.sessionId} left (${gf.players.count()}/${gf.players.capacity()})`
    );
  }
}

# Example GameFlow build: lean image, non-root (UID 1000), configured port exposed.
FROM node:24-alpine

WORKDIR /app
COPY package.json server.js ./
# In a real project: `npm install @gameflow/gameserver-sdk` (this example uses
# the workspace copy, baked in by the build context below).
COPY node_modules ./node_modules

USER 1000
EXPOSE 7777
CMD ["node", "server.js"]

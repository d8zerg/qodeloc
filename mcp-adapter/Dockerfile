FROM node:20-bookworm-slim

WORKDIR /app

COPY mcp-adapter/package*.json ./
RUN npm ci

COPY mcp-adapter ./
RUN npm run build && npm run bundle

WORKDIR /workspace/qodeloc

EXPOSE 3333

CMD ["node", "/app/dist/qodeloc-mcp-adapter.js"]

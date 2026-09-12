FROM node:22-alpine
WORKDIR /app
COPY package.json ./
COPY src ./src
ENV PORT=8787 INK_DB=/data/inkagent.sqlite
VOLUME /data
EXPOSE 8787
CMD ["node", "src/server.js"]

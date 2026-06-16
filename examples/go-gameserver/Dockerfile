# GameFlow build: custom engine. The platform extracts your upload, finds this
# Dockerfile at the root, builds it and runs the resulting image as the server.

# --- build stage -------------------------------------------------------------
FROM golang:1.23-bookworm AS builder
WORKDIR /app
COPY go.mod ./
# Resolve the published SDK module (and any transitive deps). go.sum is created
# by `go mod tidy` once the module is published; copy it too when present.
RUN go mod download
COPY . .
RUN CGO_ENABLED=0 go build -trimpath -o /gameserver .

# --- runtime stage -----------------------------------------------------------
FROM gcr.io/distroless/static-debian12
# Non-root, matching GameFlow's UID 1000 convention.
USER 1000
COPY --from=builder /gameserver /usr/local/bin/gameserver
# Container port. GameFlow injects GAMEFLOW_DEFAULT_PORT (default 7777) and maps
# it to a dynamically allocated public port.
EXPOSE 7777
ENTRYPOINT ["/usr/local/bin/gameserver"]

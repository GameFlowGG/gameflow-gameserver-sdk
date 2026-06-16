package gameflow

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"slices"
	"strings"
	"time"
)

// grpcOutOfRange is the gRPC OUT_OF_RANGE code: a list mutation rejected because
// the list is at capacity.
const grpcOutOfRange = 11

// sidecarTransport talks REST to the local GameFlow runtime over plain HTTP.
//
// In v1 the SDK talks to a local sidecar; this is an implementation detail and
// nothing here leaks into the public API, logs or errors.
type sidecarTransport struct {
	client         *http.Client
	watchClient    *http.Client
	baseURL        string
	requestTimeout time.Duration
	logger         Logger
}

func newSidecarTransport(baseURL string, requestTimeout time.Duration, logger Logger) *sidecarTransport {
	return &sidecarTransport{
		// Per-request timeouts are applied via context, so the unary client has
		// no global timeout. The watch client must stream indefinitely.
		client:         &http.Client{},
		watchClient:    &http.Client{},
		baseURL:        baseURL,
		requestTimeout: requestTimeout,
		logger:         logger,
	}
}

// url joins the base URL and a path by plain concatenation. Paths contain custom
// verbs with a literal ':' (lists/players:addValue); concatenation leaves the
// colon untouched.
func (t *sidecarTransport) url(path string) string { return t.baseURL + path }

// do sends a unary request with the per-request timeout. Statuses in `expected`
// are returned to the caller instead of being mapped to an error; any other
// non-2xx becomes a REQUEST_FAILED error.
func (t *sidecarTransport) do(ctx context.Context, method, path string, body any, what string, expected ...int) (*http.Response, error) {
	reqCtx, cancel := context.WithTimeout(ctx, t.requestTimeout)
	// The caller reads the body, so cancel must outlive this function for
	// success/expected responses; it is invoked once the body is closed.
	var reader io.Reader
	if body != nil {
		encoded, err := json.Marshal(body)
		if err != nil {
			cancel()
			return nil, requestFailed(fmt.Sprintf("could not encode %s request", what), 0, err)
		}
		reader = bytes.NewReader(encoded)
	}
	req, err := http.NewRequestWithContext(reqCtx, method, t.url(path), reader)
	if err != nil {
		cancel()
		return nil, requestFailed(fmt.Sprintf("could not build %s request", what), 0, err)
	}
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	res, err := t.client.Do(req)
	if err != nil {
		cancel()
		return nil, t.unreachable(what, err)
	}
	status := res.StatusCode
	if isSuccess(status) || slices.Contains(expected, status) {
		// Hand the response (and a cancel that frees the request context once the
		// body is closed) to the caller.
		res.Body = &cancelOnClose{ReadCloser: res.Body, cancel: cancel}
		return res, nil
	}
	gwErr := readGatewayError(res)
	cancel()
	msg := gwErr.Message
	if msg == "" {
		msg = fmt.Sprintf("%s failed with HTTP %d", what, status)
	}
	return nil, requestFailed(msg, status, nil)
}

func (t *sidecarTransport) unreachable(what string, cause error) *Error {
	return sidecarUnavailable(
		fmt.Sprintf("could not reach the GameFlow runtime at %s (%s)", t.baseURL, what),
		cause,
	)
}

func (t *sidecarTransport) ready(ctx context.Context) error {
	res, err := t.do(ctx, http.MethodPost, "/ready", struct{}{}, "POST /ready")
	if err != nil {
		return err
	}
	drainClose(res)
	return nil
}

func (t *sidecarTransport) health(ctx context.Context) error {
	res, err := t.do(ctx, http.MethodPost, "/health", struct{}{}, "POST /health")
	if err != nil {
		return err
	}
	drainClose(res)
	return nil
}

func (t *sidecarTransport) shutdown(ctx context.Context) error {
	res, err := t.do(ctx, http.MethodPost, "/shutdown", struct{}{}, "POST /shutdown")
	if err != nil {
		return err
	}
	drainClose(res)
	return nil
}

func (t *sidecarTransport) getGameServer(ctx context.Context) (*rawGameServer, error) {
	res, err := t.do(ctx, http.MethodGet, "/gameserver", nil, "GET /gameserver")
	if err != nil {
		return nil, err
	}
	defer drainClose(res)
	var gs rawGameServer
	if err := json.NewDecoder(res.Body).Decode(&gs); err != nil {
		return nil, requestFailed("could not decode the gameserver response", 0, err)
	}
	return &gs, nil
}

func (t *sidecarTransport) getPlayerList(ctx context.Context) (playerListSnapshot, error) {
	res, err := t.do(ctx, http.MethodGet, "/v1beta1/lists/"+playersList, nil, "GET players list", http.StatusNotFound)
	if err != nil {
		return playerListSnapshot{}, err
	}
	defer drainClose(res)
	if res.StatusCode == http.StatusNotFound {
		return disabledSnapshot(), nil
	}
	var list rawList
	if err := json.NewDecoder(res.Body).Decode(&list); err != nil {
		return playerListSnapshot{}, requestFailed("could not decode the players list", 0, err)
	}
	return parseList(&list), nil
}

func (t *sidecarTransport) addPlayer(ctx context.Context, sessionID string) (playerListSnapshot, error) {
	res, err := t.do(
		ctx, http.MethodPost, "/v1beta1/lists/"+playersList+":addValue",
		map[string]string{"value": sessionID}, "addValue",
		http.StatusBadRequest, http.StatusNotFound, http.StatusConflict,
	)
	if err != nil {
		return playerListSnapshot{}, err
	}

	if isSuccess(res.StatusCode) {
		// Some runtime versions answer mutations with an empty/default List
		// instead of the updated one; re-read the list rather than trust it.
		drainClose(res)
		return t.getPlayerList(ctx)
	}

	status := res.StatusCode
	gwErr := readGatewayError(res)
	drainClose(res)
	switch status {
	case http.StatusConflict:
		return playerListSnapshot{}, playerAlreadyConnected(sessionID)
	case http.StatusNotFound:
		return playerListSnapshot{}, playerTrackingDisabled()
	default:
		// 400 OUT_OF_RANGE means the list is at capacity.
		if gwErr.Code == grpcOutOfRange || indicatesCapacity(gwErr.Message) {
			return playerListSnapshot{}, serverFull(0, false)
		}
		msg := gwErr.Message
		if msg == "" {
			msg = fmt.Sprintf("addValue failed with HTTP %d", status)
		}
		return playerListSnapshot{}, requestFailed(msg, status, nil)
	}
}

func (t *sidecarTransport) removePlayer(ctx context.Context, sessionID string) (bool, playerListSnapshot, error) {
	res, err := t.do(
		ctx, http.MethodPost, "/v1beta1/lists/"+playersList+":removeValue",
		map[string]string{"value": sessionID}, "removeValue", http.StatusNotFound,
	)
	if err != nil {
		return false, playerListSnapshot{}, err
	}
	if res.StatusCode == http.StatusNotFound {
		drainClose(res)
		return false, playerListSnapshot{}, nil
	}
	drainClose(res)
	snap, err := t.getPlayerList(ctx)
	if err != nil {
		return false, playerListSnapshot{}, err
	}
	return true, snap, nil
}

func (t *sidecarTransport) watchGameServer(ctx context.Context, sink watchSink) error {
	// Long-lived stream: no per-request timeout, only ctx cancellation.
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, t.url("/watch/gameserver"), nil)
	if err != nil {
		return t.unreachable("GET /watch/gameserver", err)
	}
	res, err := t.watchClient.Do(req)
	if err != nil {
		return t.unreachable("GET /watch/gameserver", err)
	}
	defer drainClose(res)
	if !isSuccess(res.StatusCode) {
		return requestFailed(fmt.Sprintf("watch failed with HTTP %d", res.StatusCode), res.StatusCode, nil)
	}

	reader := bufio.NewReader(res.Body)
	for {
		line, err := reader.ReadBytes('\n')
		if len(line) > 0 {
			t.handleWatchLine(bytes.TrimRight(line, "\r\n"), sink)
		}
		if err != nil {
			if ctx.Err() != nil {
				return nil // cancelled by the caller
			}
			if err == io.EOF {
				return nil // stream ended; caller reconnects
			}
			return t.unreachable("watch stream", err)
		}
	}
}

func (t *sidecarTransport) handleWatchLine(line []byte, sink watchSink) {
	if len(bytes.TrimSpace(line)) == 0 {
		return
	}
	var parsed watchLine
	if err := json.Unmarshal(line, &parsed); err != nil {
		t.logger.Warn("watch: skipping malformed stream line")
		return
	}
	if parsed.Error != nil {
		msg := parsed.Error.Message
		if msg == "" {
			msg = "unknown"
		}
		t.logger.Warn("watch: stream error: " + msg)
		return
	}
	if parsed.Result != nil {
		sink(parsed.Result)
	}
}

func indicatesCapacity(message string) bool {
	lower := strings.ToLower(message)
	return strings.Contains(lower, "capacity") || strings.Contains(lower, "out of range")
}

func readGatewayError(res *http.Response) gatewayError {
	var gwErr gatewayError
	_ = json.NewDecoder(res.Body).Decode(&gwErr)
	return gwErr
}

func isSuccess(status int) bool { return status >= 200 && status < 300 }

// cancelOnClose invokes cancel once the response body is closed, freeing the
// per-request timeout context exactly when the caller is done reading.
type cancelOnClose struct {
	io.ReadCloser
	cancel context.CancelFunc
}

func (c *cancelOnClose) Close() error {
	err := c.ReadCloser.Close()
	c.cancel()
	return err
}

// drainClose drains and closes a response body so the HTTP connection can be
// reused, and releases the request context.
func drainClose(res *http.Response) {
	if res == nil || res.Body == nil {
		return
	}
	_, _ = io.Copy(io.Discard, res.Body)
	_ = res.Body.Close()
}

# Helper recovery implementation plan

Goal: distinguish Helper transport failures from kernel exit; keep reconnecting and log actionable, secret-free diagnostics.

Design: preserve the JSON protocol and existing checkout. Retry only read-only ping/status on a fresh pipe. Drain writes and honor the full response deadline. During lost status, keep the kernel running state, probe authenticated loopback /version asynchronously and check a retained Windows process handle. Only a confirmed dead process or an explicit stopped Helper status plus failed controller probe ends the session. Unknown/access-denied process state remains recoverable. Back off polling to at most five seconds; log first loss, periodic failure details and recovery. Cancel probes on stop/mode change; never replay start/stop after an ambiguous response.

Alternatives: increasing timeouts alone cannot verify liveness; restarting the kernel on pipe errors risks interrupting healthy traffic. Existing Qt sockets, timers and network classes provide the required recovery without a new dependency.

- [x] Add integration tests with an isolated local pipe and loopback controller: error context/redaction, delayed/fragmented responses, read retry, no write-command replay, transient loss/recovery, live/unknown/exited process, cancelled probes and failed stop.
- [x] Run the new tests against current behavior and observe expected failures.
- [x] Implement transport diagnostics/retry and recovery/liveness checks in ServiceClient and MihomoProcess.
- [x] Run focused tests, build GUI and Helper, then run the full CTest suite.
- [x] Review lifecycle, authentication, stale callbacks, log throttling and command replay; document behavior.

Constraints: no token, secret, payload or response bodies in diagnostics; no elevated service operations or disruption of the user's running core; test pipe selected at compile time. Use the current checkout and existing MSVC/Qt build.

Validation: initial regression run 8 failures / 4 passes; recovery cases then passed. Review found reset per-attempt timeout could block the UI too long: new deadline regression failed at 1625 ms, passed at approximately 800 ms after sharing one budget. Final MSVC Release build succeeded for GUI, Helper and tests; final CTest 17/17 targets passed (26.46 s). Loopback tests require normal execution permissions because the sandbox blocks local TCP. Independent review found no other actionable issues. Changes remain in the current working tree; no installed service or running application was replaced.

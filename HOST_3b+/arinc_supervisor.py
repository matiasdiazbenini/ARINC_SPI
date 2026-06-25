#!/usr/bin/env python3
import json
import os
import subprocess
import time
import urllib.error
import urllib.request
from datetime import datetime


STATE_CODES = {
    "UNKNOWN": 0,
    "BOOTING": 1,
    "WAITING_SNIFFER": 2,
    "SPI_SYNCING": 3,
    "RUNNING": 4,
    "ARINC_STALLED": 5,
    "CABLE_FAULT": 6,
    "BRIDGE_FAULT": 7,
    "DASHBOARD_FAULT": 8,
    "RECOVERING": 9,
}


def env_bool(name: str, default: bool) -> bool:
    value = os.getenv(name)
    if value is None:
        return default
    return value.strip().lower() not in ("0", "false", "no", "off")


def env_float(name: str, default: float) -> float:
    try:
        return float(os.getenv(name, str(default)))
    except ValueError:
        return default


def env_int(name: str, default: int) -> int:
    try:
        return int(os.getenv(name, str(default)))
    except ValueError:
        return default


BRIDGE_URL = os.getenv("ARINC_SUPERVISOR_BRIDGE_URL", "http://127.0.0.1:5100").rstrip("/")
INTERVAL_SEC = env_float("ARINC_SUPERVISOR_INTERVAL_SEC", 5.0)
HTTP_TIMEOUT_SEC = env_float("ARINC_SUPERVISOR_HTTP_TIMEOUT_SEC", 2.0)
STARTUP_GRACE_SEC = env_float("ARINC_SUPERVISOR_STARTUP_GRACE_SEC", 35.0)
TRAFFIC_STALL_SEC = env_float("ARINC_SUPERVISOR_TRAFFIC_STALL_SEC", 30.0)
FAULT_THRESHOLD = env_int("ARINC_SUPERVISOR_FAULT_THRESHOLD", 3)
RESTART_COOLDOWN_SEC = env_float("ARINC_SUPERVISOR_RESTART_COOLDOWN_SEC", 45.0)
SPI_RECOVER_COOLDOWN_SEC = env_float("ARINC_SUPERVISOR_SPI_RECOVER_COOLDOWN_SEC", 15.0)
SPI_RECOVER_MAX_ATTEMPTS = env_int("ARINC_SUPERVISOR_SPI_RECOVER_MAX_ATTEMPTS", 2)
ENABLE_ACTIONS = env_bool("ARINC_SUPERVISOR_ACTIONS", True)
ENABLE_SPI_RECOVER = env_bool("ARINC_SUPERVISOR_SPI_RECOVER", True)
RESTART_ON_ARINC_STALL = env_bool("ARINC_SUPERVISOR_RESTART_ON_ARINC_STALL", False)
BRIDGE_SERVICE = os.getenv("ARINC_SUPERVISOR_BRIDGE_SERVICE", "arinc-sniffer-bridge")
FLASK_SERVICE = os.getenv("ARINC_SUPERVISOR_FLASK_SERVICE", "arinc-dashboard-flask")
STATUS_PATH = os.getenv("ARINC_SUPERVISOR_STATUS", "arinc_supervisor_status.json").strip()
LOG_PATH = os.getenv("ARINC_SUPERVISOR_LOG", "arinc_supervisor_events.jsonl").strip()


def utc_timestamp() -> str:
    return datetime.utcnow().replace(microsecond=0).isoformat() + "Z"


def log_event(event: str, **fields) -> None:
    payload = {"ts": utc_timestamp(), "event": event, **fields}
    line = json.dumps(payload, sort_keys=True)
    print(line, flush=True)
    if LOG_PATH:
        try:
            with open(LOG_PATH, "a", encoding="utf-8") as handle:
                handle.write(line + "\n")
        except OSError as exc:
            print(json.dumps({
                "ts": utc_timestamp(),
                "event": "log_write_failed",
                "error": str(exc),
            }), flush=True)


def write_status_file(payload: dict) -> None:
    if not STATUS_PATH:
        return

    tmp_path = f"{STATUS_PATH}.tmp"
    directory = os.path.dirname(os.path.abspath(STATUS_PATH))
    if directory:
        os.makedirs(directory, exist_ok=True)

    with open(tmp_path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, sort_keys=True, indent=2)
        handle.write("\n")
    os.replace(tmp_path, STATUS_PATH)


def systemctl(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["systemctl", *args],
        capture_output=True,
        text=True,
        check=False,
    )


def service_is_active(name: str) -> bool:
    return systemctl("is-active", "--quiet", name).returncode == 0


def restart_services(reason: str, *services: str) -> bool:
    if not ENABLE_ACTIONS:
        log_event("restart_suppressed", reason=reason, services=list(services))
        return False

    completed = systemctl("restart", *services)
    ok = completed.returncode == 0
    log_event(
        "restart_services",
        reason=reason,
        services=list(services),
        ok=ok,
        stderr=completed.stderr.strip(),
    )
    return ok


def request_json(path: str, *, method: str = "GET", timeout: float = HTTP_TIMEOUT_SEC) -> dict:
    url = f"{BRIDGE_URL}{path}"
    data = None
    headers = {}
    if method != "GET":
        data = b"{}"
        headers["Content-Type"] = "application/json"

    request = urllib.request.Request(url, data=data, headers=headers, method=method)
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


class Supervisor:
    def __init__(self):
        self.started_at = time.monotonic()
        self.last_restart_at = 0.0
        self.last_spi_recover_at = 0.0
        self.http_failures = 0
        self.protocol_failures = 0
        self.service_failures = 0
        self.stall_failures = 0
        self.spi_recover_attempts = 0
        self.actions_total = 0
        self.restarts_total = 0
        self.spi_recovers_total = 0
        self.physical_faults_total = 0
        self.last_action = ""
        self.last_action_ts = ""
        self.last_state = None
        self.last_accepted = None
        self.last_received = None
        self.last_parity_error = None
        self.last_progress_at = time.monotonic()
        self.last_received_progress_at = time.monotonic()
        self.last_status = {}

    def uptime_sec(self) -> float:
        return time.monotonic() - self.started_at

    def in_startup_grace(self) -> bool:
        return self.uptime_sec() < STARTUP_GRACE_SEC

    def can_restart(self) -> bool:
        return (time.monotonic() - self.last_restart_at) >= RESTART_COOLDOWN_SEC

    def can_spi_recover(self) -> bool:
        return (time.monotonic() - self.last_spi_recover_at) >= SPI_RECOVER_COOLDOWN_SEC

    def set_last_action(self, action: str, reason: str) -> None:
        self.actions_total += 1
        self.last_action = f"{action}:{reason}"
        self.last_action_ts = utc_timestamp()

    def restart_bridge_stack(self, reason: str) -> None:
        if not self.can_restart():
            log_event("restart_cooldown", reason=reason)
            return

        if restart_services(reason, BRIDGE_SERVICE, FLASK_SERVICE):
            self.restarts_total += 1
            self.set_last_action("restart_bridge_stack", reason)
            self.last_restart_at = time.monotonic()
            self.http_failures = 0
            self.protocol_failures = 0
            self.service_failures = 0
            self.stall_failures = 0
            self.spi_recover_attempts = 0
            self.last_accepted = None
            self.last_received = None
            self.last_parity_error = None
            self.last_progress_at = time.monotonic()
            self.last_received_progress_at = time.monotonic()

    def recover_spi_transport(self, reason: str) -> bool:
        if not ENABLE_ACTIONS or not ENABLE_SPI_RECOVER:
            log_event("spi_recover_suppressed", reason=reason)
            return False
        if not self.can_spi_recover():
            log_event("spi_recover_cooldown", reason=reason)
            return False
        if self.spi_recover_attempts >= SPI_RECOVER_MAX_ATTEMPTS:
            log_event("spi_recover_limit", reason=reason, attempts=self.spi_recover_attempts)
            return False

        try:
            result = request_json("/control/recover_spi", method="POST")
        except (OSError, urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
            log_event("spi_recover_failed", reason=reason, error=str(exc))
            return False

        self.spi_recover_attempts += 1
        self.spi_recovers_total += 1
        self.last_spi_recover_at = time.monotonic()
        self.set_last_action("recover_spi", reason)
        log_event("spi_recover", reason=reason, result=result)
        return True

    def classify_services(self) -> tuple[list[str], dict]:
        active = {
            BRIDGE_SERVICE: service_is_active(BRIDGE_SERVICE),
            FLASK_SERVICE: service_is_active(FLASK_SERVICE),
        }
        inactive = [name for name, is_active in active.items() if not is_active]
        return inactive, active

    def status_payload(self,
                       state: str,
                       health: str,
                       issue: str,
                       *,
                       services=None,
                       stats=None,
                       detail=None) -> dict:
        payload = {
            "ts": utc_timestamp(),
            "state": state,
            "state_code": STATE_CODES.get(state, 0),
            "health": health,
            "issue": issue,
            "uptime_sec": round(self.uptime_sec(), 3),
            "startup_grace": self.in_startup_grace(),
            "actions_total": self.actions_total,
            "restarts_total": self.restarts_total,
            "spi_recovers_total": self.spi_recovers_total,
            "physical_faults_total": self.physical_faults_total,
            "last_action": self.last_action,
            "last_action_ts": self.last_action_ts,
            "failures": {
                "http": self.http_failures,
                "protocol": self.protocol_failures,
                "service": self.service_failures,
                "stall": self.stall_failures,
                "spi_recover_attempts": self.spi_recover_attempts,
            },
            "services": services or {},
            "stats": stats or {},
            "detail": detail or {},
        }
        return payload

    def publish_status(self, payload: dict) -> None:
        state = payload.get("state", "UNKNOWN")
        if state != self.last_state:
            log_event(
                "state_change",
                previous=self.last_state,
                current=state,
                issue=payload.get("issue", ""),
                health=payload.get("health", ""),
            )
            self.last_state = state

        self.last_status = payload
        try:
            write_status_file(payload)
        except OSError as exc:
            log_event("status_write_failed", error=str(exc))

    def make_stats_summary(self, stats: dict) -> dict:
        return {
            "connected": bool(stats.get("connected")),
            "last_error": str(stats.get("last_error") or ""),
            "accepted_words": int(stats.get("accepted_words") or 0),
            "received_words": int(stats.get("received_words") or 0),
            "parity_errors": int(stats.get("parity_errors_sniffer") or stats.get("parity_error") or 0),
            "spi_errors": int(stats.get("spi_errors") or 0),
            "spi_startup_errors": int(stats.get("spi_startup_errors") or 0),
            "detected_bit_rate_bps": int(stats.get("detected_bit_rate_bps") or 0),
            "last_rx_time": stats.get("last_rx_time"),
        }

    def evaluate_stats(self, stats: dict) -> tuple[str, str, str, dict]:
        summary = self.make_stats_summary(stats)
        now = time.monotonic()
        accepted = summary["accepted_words"]
        received = summary["received_words"]
        parity_error = summary["parity_errors"]
        connected = summary["connected"]
        last_error = summary["last_error"]

        accepted_delta = 0 if self.last_accepted is None else accepted - self.last_accepted
        received_delta = 0 if self.last_received is None else received - self.last_received
        parity_delta = 0 if self.last_parity_error is None else parity_error - self.last_parity_error

        if self.last_accepted is None or accepted > self.last_accepted:
            self.last_progress_at = now
            self.stall_failures = 0
        if self.last_received is None or received > self.last_received:
            self.last_received_progress_at = now

        stalled_for = now - self.last_progress_at
        received_stalled_for = now - self.last_received_progress_at
        detail = {
            "accepted_delta": accepted_delta,
            "received_delta": received_delta,
            "parity_delta": parity_delta,
            "stalled_for_sec": round(stalled_for, 3),
            "received_stalled_for_sec": round(received_stalled_for, 3),
        }

        if not connected or last_error:
            self.protocol_failures += 1
            state = "SPI_SYNCING" if self.in_startup_grace() else "BRIDGE_FAULT"
            health = "degraded" if self.in_startup_grace() else "fault"
            issue = "spi_protocol_fault" if last_error else "spi_not_connected"
            detail["protocol_failures"] = self.protocol_failures

            if self.protocol_failures >= FAULT_THRESHOLD:
                state = "RECOVERING"
                health = "recovering"
                if not self.recover_spi_transport(issue):
                    self.restart_bridge_stack(issue)
            return state, health, issue, detail

        self.protocol_failures = 0
        if accepted > 0 and accepted_delta > 0:
            self.spi_recover_attempts = 0
            return "RUNNING", "ok", "none", detail

        if self.in_startup_grace() and accepted == 0:
            return "WAITING_SNIFFER", "degraded", "waiting_first_valid_word", detail

        if stalled_for >= TRAFFIC_STALL_SEC:
            self.stall_failures += 1
            if received_delta > 0 or parity_delta > 0:
                issue = "arinc_invalid_or_misaligned"
                state = "ARINC_STALLED"
            elif received_stalled_for >= TRAFFIC_STALL_SEC:
                issue = "no_arinc_activity"
                state = "CABLE_FAULT"
            else:
                issue = "no_accepted_progress"
                state = "ARINC_STALLED"

            detail["stall_failures"] = self.stall_failures
            if state == "CABLE_FAULT" and self.last_state != "CABLE_FAULT":
                self.physical_faults_total += 1
            if RESTART_ON_ARINC_STALL and self.stall_failures >= FAULT_THRESHOLD:
                self.restart_bridge_stack(issue)
                return "RECOVERING", "recovering", issue, detail
            return state, "fault", issue, detail

        return "RUNNING", "ok", "none", detail

    def run_once(self) -> None:
        inactive, service_state = self.classify_services()
        if inactive:
            self.service_failures += 1
            if BRIDGE_SERVICE in inactive:
                state, health, issue = "BRIDGE_FAULT", "fault", "bridge_service_inactive"
            else:
                state, health, issue = "DASHBOARD_FAULT", "fault", "dashboard_service_inactive"

            detail = {"inactive_services": inactive, "service_failures": self.service_failures}
            if self.service_failures >= FAULT_THRESHOLD:
                state, health = "RECOVERING", "recovering"
                self.restart_bridge_stack(issue)

            self.publish_status(self.status_payload(
                state,
                health,
                issue,
                services=service_state,
                detail=detail,
            ))
            return
        else:
            self.service_failures = 0

        try:
            stats = request_json("/stats")
        except (OSError, urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
            self.http_failures += 1
            state = "BRIDGE_FAULT" if not self.in_startup_grace() else "BOOTING"
            health = "fault" if not self.in_startup_grace() else "degraded"
            issue = "bridge_http_fault"
            detail = {"error": str(exc), "http_failures": self.http_failures}
            if self.http_failures >= FAULT_THRESHOLD:
                state, health = "RECOVERING", "recovering"
                self.restart_bridge_stack(issue)
            self.publish_status(self.status_payload(
                state,
                health,
                issue,
                services=service_state,
                detail=detail,
            ))
            return

        self.http_failures = 0
        state, health, issue, detail = self.evaluate_stats(stats)
        summary = self.make_stats_summary(stats)
        self.last_accepted = summary["accepted_words"]
        self.last_received = summary["received_words"]
        self.last_parity_error = summary["parity_errors"]

        self.publish_status(self.status_payload(
            state,
            health,
            issue,
            services=service_state,
            stats=summary,
            detail=detail,
        ))


def main() -> None:
    supervisor = Supervisor()
    supervisor.publish_status(supervisor.status_payload(
        "BOOTING",
        "degraded",
        "supervisor_started",
        detail={"bridge_url": BRIDGE_URL, "interval_sec": INTERVAL_SEC, "actions": ENABLE_ACTIONS},
    ))
    log_event(
        "supervisor_started",
        bridge_url=BRIDGE_URL,
        interval_sec=INTERVAL_SEC,
        actions=ENABLE_ACTIONS,
        status_path=STATUS_PATH,
    )
    while True:
        supervisor.run_once()
        time.sleep(max(INTERVAL_SEC, 1.0))


if __name__ == "__main__":
    main()

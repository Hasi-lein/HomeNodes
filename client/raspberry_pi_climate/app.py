from __future__ import annotations

import json
import logging
import math
import os
import tempfile
import threading
import time
from collections import deque
from datetime import datetime, timezone
from pathlib import Path
from typing import Protocol

from flask import Flask, jsonify, render_template, request
from waitress import serve

from sensor import Esp32ClimateApi


LOG = logging.getLogger("homenodes.climate")
MAX_RECORDS = 10_000
COMPACTION_BATCH = 1_000
GRAPH_RANGES_HOURS = {1, 6, 24, 168, 720}


class Sensor(Protocol):
    def read(self) -> tuple[float, float]: ...


class ClimateState:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self.last_error: str | None = None
        self.last_success_at: str | None = None

    def record_success(self, sampled_at: str) -> None:
        with self._lock:
            self.last_success_at = sampled_at
            self.last_error = None

    def record_error(self, error: str) -> None:
        with self._lock:
            self.last_error = error

    def snapshot(self) -> dict[str, str | None]:
        with self._lock:
            return {
                "lastSuccessAt": self.last_success_at,
                "lastError": self.last_error,
            }


class JsonLinesStore:
    def __init__(
        self,
        file_path: str,
        max_records: int = MAX_RECORDS,
        compaction_batch: int = COMPACTION_BATCH,
    ) -> None:
        self.path = Path(file_path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.max_records = max_records
        self.compaction_batch = compaction_batch
        self._lock = threading.Lock()
        self._records: deque[dict[str, object]] = deque(maxlen=max_records)
        self._record_count = self._load()

        if self._record_count >= self.max_records + self.compaction_batch:
            self._compact()

    def _load(self) -> int:
        if not self.path.exists():
            return 0

        record_count = 0
        with self.path.open(encoding="utf-8") as data_file:
            for line_number, line in enumerate(data_file, start=1):
                try:
                    record = json.loads(line)
                except json.JSONDecodeError as error:
                    LOG.error(
                        "Ignoring invalid JSON record at %s:%d: %s",
                        self.path,
                        line_number,
                        error,
                    )
                    continue
                if not self._is_valid_record(record):
                    LOG.error(
                        "Ignoring invalid climate record at %s:%d",
                        self.path,
                        line_number,
                    )
                    continue
                self._records.append(record)
                record_count += 1
        return record_count

    @staticmethod
    def _is_valid_record(record: object) -> bool:
        if not isinstance(record, dict):
            return False
        sampled_at = record.get("sampledAt")
        temperature = record.get("temperatureC")
        humidity = record.get("humidityPercent")
        if not isinstance(sampled_at, str):
            return False
        if isinstance(temperature, bool) or not isinstance(temperature, (int, float)):
            return False
        if isinstance(humidity, bool) or not isinstance(humidity, (int, float)):
            return False
        if not math.isfinite(temperature) or not math.isfinite(humidity):
            return False
        try:
            timestamp = datetime.fromisoformat(sampled_at)
        except ValueError:
            return False
        return timestamp.tzinfo is not None

    def append(self, record: dict[str, object]) -> None:
        if not self._is_valid_record(record):
            raise ValueError("Climate record has an invalid shape")

        serialized = json.dumps(record, separators=(",", ":"), allow_nan=False)
        with self._lock:
            with self.path.open("a", encoding="utf-8") as data_file:
                data_file.write(serialized + "\n")
            self._records.append(record)
            self._record_count += 1

            if self._record_count >= self.max_records + self.compaction_batch:
                self._compact()

    def _compact(self) -> None:
        temporary_path: str | None = None
        try:
            with tempfile.NamedTemporaryFile(
                mode="w",
                encoding="utf-8",
                dir=self.path.parent,
                prefix=f"{self.path.name}.",
                suffix=".tmp",
                delete=False,
            ) as data_file:
                temporary_path = data_file.name
                for record in self._records:
                    data_file.write(
                        json.dumps(record, separators=(",", ":"), allow_nan=False)
                        + "\n"
                    )
                data_file.flush()
                os.fsync(data_file.fileno())
            os.replace(temporary_path, self.path)
        except OSError:
            if temporary_path is not None:
                try:
                    os.unlink(temporary_path)
                except FileNotFoundError:
                    pass
            raise
        self._record_count = len(self._records)
        LOG.info("Compacted climate log to %d records", self._record_count)

    def latest(self) -> dict[str, object] | None:
        with self._lock:
            return self._records[-1] if self._records else None

    def since(self, start_time: float) -> list[dict[str, object]]:
        with self._lock:
            records = list(self._records)

        matching = []
        for record in records:
            sampled_at = datetime.fromisoformat(record["sampledAt"]).timestamp()
            if sampled_at >= start_time:
                matching.append(record)
        return matching


def create_app(
    jsonl_path: str | None = None,
    sensor_name: str | None = None,
    store: JsonLinesStore | None = None,
) -> Flask:
    jsonl_path = jsonl_path or os.environ.get(
        "JSONL_PATH", "/data/readings.jsonl"
    )
    sensor_name = sensor_name or os.environ.get("SENSOR_NAME", "Raumklima")
    store = store or JsonLinesStore(jsonl_path)
    app = Flask(__name__)
    state = ClimateState()
    app.config["JSONL_PATH"] = jsonl_path
    app.config["SENSOR_NAME"] = sensor_name
    app.extensions["climate_state"] = state
    app.extensions["climate_store"] = store

    @app.get("/")
    def dashboard():
        return render_template("index.html", sensor_name=sensor_name)

    @app.get("/api/v1/status")
    def status():
        latest = store.latest()
        status_data = state.snapshot()
        return jsonify(
            {
                "client": {
                    "clientId": "raspberry-pi-climate",
                    "sensorName": sensor_name,
                    "status": "error" if status_data["lastError"] else (
                        "active" if status_data["lastSuccessAt"] else "waiting"
                    ),
                    **status_data,
                },
                "reading": latest,
            }
        )

    @app.get("/api/v1/readings")
    def readings():
        try:
            hours = int(request.args.get("hours", "24"))
        except ValueError:
            return jsonify({"error": "hours must be one of 1, 6, 24, 168, or 720"}), 400
        if hours not in GRAPH_RANGES_HOURS:
            return jsonify({"error": "hours must be one of 1, 6, 24, 168, or 720"}), 400

        start_time = time.time() - hours * 60 * 60
        return jsonify(
            {
                "hours": hours,
                "retainedRecords": MAX_RECORDS,
                "points": store.since(start_time),
            }
        )

    return app


def run_sensor_loop(
    sensor: Sensor,
    store: JsonLinesStore,
    state: ClimateState,
    interval_seconds: float,
    stop_event: threading.Event,
) -> None:
    next_sample_at = time.monotonic()
    while not stop_event.is_set():
        try:
            temperature_c, humidity_percent = sensor.read()
            sampled_at = datetime.now(timezone.utc).isoformat(timespec="seconds")
            store.append(
                {
                    "sampledAt": sampled_at,
                    "temperatureC": temperature_c,
                    "humidityPercent": humidity_percent,
                }
            )
            state.record_success(sampled_at)
            LOG.info(
                "Recorded temperature %.2f C and humidity %.2f%% at %s",
                temperature_c,
                humidity_percent,
                sampled_at,
            )
        except (OSError, RuntimeError, ValueError) as error:
            message = str(error)
            state.record_error(message)
            LOG.error("Climate sampling failed: %s", message)

        next_sample_at += interval_seconds
        delay = max(0.0, next_sample_at - time.monotonic())
        if stop_event.wait(delay):
            return


def main() -> None:
    logging.basicConfig(
        level=os.environ.get("LOG_LEVEL", "INFO").upper(),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    jsonl_path = os.environ.get("JSONL_PATH", "/data/readings.jsonl")
    try:
        interval_seconds = float(
            os.environ.get("SAMPLE_INTERVAL_SECONDS", "10")
        )
    except ValueError as error:
        raise SystemExit("SAMPLE_INTERVAL_SECONDS must be a positive number") from error
    if interval_seconds <= 0:
        raise SystemExit("SAMPLE_INTERVAL_SECONDS must be a positive number")

    app = create_app(jsonl_path)
    state = app.extensions["climate_state"]
    store = app.extensions["climate_store"]
    esp32_status_url = os.environ.get("ESP32_STATUS_URL")
    esp32_api_user = os.environ.get("ESP32_API_USER")
    esp32_api_password = os.environ.get("ESP32_API_PASSWORD")
    if not esp32_status_url or not esp32_api_user or not esp32_api_password:
        missing_settings = [
            name
            for name, value in (
                ("ESP32_STATUS_URL", esp32_status_url),
                ("ESP32_API_USER", esp32_api_user),
                ("ESP32_API_PASSWORD", esp32_api_password),
            )
            if not value
        ]
        raise SystemExit(
            "Missing required ESP32 API settings: " + ", ".join(missing_settings)
        )
    sensor = Esp32ClimateApi(
        status_url=esp32_status_url,
        username=esp32_api_user,
        password=esp32_api_password,
        ca_certificate_path=os.environ.get(
            "ESP32_CA_CERT", "/certs/esp32-relay-cert.pem"
        ),
    )
    stop_event = threading.Event()
    sampler = threading.Thread(
        target=run_sensor_loop,
        args=(sensor, store, state, interval_seconds, stop_event),
        name="climate-sampler",
        daemon=True,
    )
    sampler.start()
    LOG.info("Starting climate dashboard on internal port 8000")
    try:
        serve(app, host="0.0.0.0", port=8000, threads=4)
    finally:
        stop_event.set()
        sampler.join(timeout=interval_seconds + 1)


if __name__ == "__main__":
    main()

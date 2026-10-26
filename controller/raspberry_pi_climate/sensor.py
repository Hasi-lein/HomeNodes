from __future__ import annotations

import base64
import json
import math
import ssl
from urllib.parse import urlsplit, urlunsplit
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


MAX_RESPONSE_BYTES = 64 * 1024
MIN_TARGET_TEMPERATURE_C = 5.0
MAX_TARGET_TEMPERATURE_C = 35.0


def decode_status_response(response: bytes) -> dict[str, object]:
    try:
        payload = json.loads(response)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RuntimeError("ESP32 returned invalid JSON") from error
    if not isinstance(payload, dict):
        raise RuntimeError("ESP32 status response must be a JSON object")

    sensors = payload.get("sensors")
    if not isinstance(sensors, list):
        raise RuntimeError("ESP32 status response has no sensors array")

    sensor = next(
        (
            item
            for item in sensors
            if isinstance(item, dict) and item.get("id") == "room-climate"
        ),
        None,
    )
    if sensor is None:
        sensor = next(
            (
                item
                for item in sensors
                if isinstance(item, dict) and item.get("type") == "DHT20"
            ),
            None,
        )
    if sensor is None:
        raise RuntimeError("ESP32 status response has no DHT20 room-climate sensor")
    if sensor.get("status") != "on":
        raise RuntimeError("ESP32 DHT20 sensor is not reporting a fresh reading")

    temperature = sensor.get("temperatureC")
    humidity = sensor.get("relativeHumidityPercent")
    if (
        isinstance(temperature, bool)
        or not isinstance(temperature, (int, float))
        or not math.isfinite(temperature)
    ):
        raise RuntimeError("ESP32 returned an invalid temperature")
    if (
        isinstance(humidity, bool)
        or not isinstance(humidity, (int, float))
        or not math.isfinite(humidity)
        or humidity < 0
        or humidity > 100
    ):
        raise RuntimeError("ESP32 returned an invalid relative humidity")

    battery_voltage_v = None
    battery = next(
        (
            item
            for item in sensors
            if isinstance(item, dict) and item.get("id") == "battery-voltage"
        ),
        None,
    )
    if battery is not None and battery.get("status") == "on":
        voltage = battery.get("voltageV")
        if (
            isinstance(voltage, bool)
            or not isinstance(voltage, (int, float))
            or not math.isfinite(voltage)
        ):
            raise RuntimeError("ESP32 returned an invalid battery voltage")
        battery_voltage_v = float(voltage)

    client = payload.get("client")
    if not isinstance(client, dict):
        raise RuntimeError("ESP32 status response has no client object")
    temperature_control = client.get("temperatureControl")
    if not isinstance(temperature_control, dict):
        raise RuntimeError("ESP32 status response has no temperature control state")
    target_temperature = temperature_control.get("targetTemperatureC")
    if (
        isinstance(target_temperature, bool)
        or not isinstance(target_temperature, (int, float))
        or not math.isfinite(target_temperature)
    ):
        raise RuntimeError("ESP32 returned an invalid thermostat target")

    actuators = payload.get("actuators")
    if not isinstance(actuators, list):
        raise RuntimeError("ESP32 status response has no actuators array")
    heater = next(
        (
            item
            for item in actuators
            if isinstance(item, dict)
            and item.get("id") == "infrared-heater-relay"
        ),
        None,
    )
    if heater is None:
        heater = next(
            (
                item
                for item in actuators
                if isinstance(item, dict) and item.get("type") == "relay"
            ),
            None,
        )
    if heater is None or heater.get("status") not in {"on", "off"}:
        raise RuntimeError("ESP32 status response has no valid heater relay status")

    return {
        "temperatureC": float(temperature),
        "humidityPercent": float(humidity),
        "batteryVoltageV": battery_voltage_v,
        "heater": {
            "relayStatus": heater["status"],
            "enabled": client.get("heaterEnabled") is True,
            "controlReason": client.get("controlReason", "unknown"),
            "targetTemperatureC": float(target_temperature),
        },
    }


class Esp32ClimateApi:
    def __init__(
        self,
        status_url: str,
        username: str,
        password: str,
        ca_certificate_path: str,
        timeout_seconds: float = 8.0,
    ) -> None:
        if not status_url.startswith("https://"):
            raise ValueError("ESP32_STATUS_URL must use HTTPS")
        if not username or not password:
            raise ValueError("ESP32 API username and password must be configured")
        self.status_url = status_url
        parsed_url = urlsplit(status_url)
        if parsed_url.path.rstrip("/") != "/api/v1/status":
            raise ValueError("ESP32_STATUS_URL must end in /api/v1/status")
        self.settings_url = urlunsplit(
            (
                parsed_url.scheme,
                parsed_url.netloc,
                "/api/v1/settings",
                "",
                "",
            )
        )
        self.authorization = "Basic " + base64.b64encode(
            f"{username}:{password}".encode("utf-8")
        ).decode("ascii")
        self.tls_context = ssl.create_default_context(cafile=ca_certificate_path)
        self.timeout_seconds = timeout_seconds

    def _request_json(
        self, url: str, method: str = "GET", body: bytes | None = None
    ) -> dict[str, object]:
        request = Request(
            url,
            data=body,
            method=method,
            headers={
                "Accept": "application/json",
                "Authorization": self.authorization,
                **({"Content-Type": "application/json"} if body is not None else {}),
            },
        )
        try:
            with urlopen(
                request,
                timeout=self.timeout_seconds,
                context=self.tls_context,
            ) as response:
                if response.status != 200:
                    raise RuntimeError(
                        f"ESP32 status API returned HTTP {response.status}"
                    )
                body = response.read(MAX_RESPONSE_BYTES + 1)
        except HTTPError as error:
            raise RuntimeError(
                f"ESP32 status API returned HTTP {error.code}"
            ) from error
        except URLError as error:
            raise RuntimeError(f"ESP32 status API request failed: {error.reason}") from error

        if len(body) > MAX_RESPONSE_BYTES:
            raise RuntimeError("ESP32 status response exceeds the 64 KiB limit")
        try:
            payload = json.loads(body)
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise RuntimeError("ESP32 returned invalid JSON") from error
        if not isinstance(payload, dict):
            raise RuntimeError("ESP32 response must be a JSON object")
        if "error" in payload:
            raise RuntimeError(f"ESP32 rejected settings request: {payload['error']}")
        return payload

    def read(self) -> dict[str, object]:
        payload = self._request_json(self.status_url)
        return self._decode_payload(payload)

    @staticmethod
    def _decode_payload(payload: dict[str, object]) -> dict[str, object]:
        return decode_status_response(
            json.dumps(payload, allow_nan=False).encode("utf-8")
        )

    def set_target_temperature(self, target_temperature_c: float) -> dict[str, object]:
        if (
            not math.isfinite(target_temperature_c)
            or target_temperature_c < MIN_TARGET_TEMPERATURE_C
            or target_temperature_c > MAX_TARGET_TEMPERATURE_C
        ):
            raise ValueError("Target temperature must be between 5 and 35 °C")

        payload = self._request_json(
            self.settings_url,
            method="POST",
            body=json.dumps(
                {"targetTemperatureC": target_temperature_c},
                separators=(",", ":"),
                allow_nan=False,
            ).encode("utf-8"),
        )
        return self._decode_payload(payload)["heater"]

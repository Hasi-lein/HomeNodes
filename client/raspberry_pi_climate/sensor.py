import base64
import json
import math
import ssl
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


MAX_RESPONSE_BYTES = 64 * 1024


def decode_status_response(response: bytes) -> tuple[float, float]:
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

    return float(temperature), float(humidity)


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
        self.authorization = "Basic " + base64.b64encode(
            f"{username}:{password}".encode("utf-8")
        ).decode("ascii")
        self.tls_context = ssl.create_default_context(cafile=ca_certificate_path)
        self.timeout_seconds = timeout_seconds

    def read(self) -> tuple[float, float]:
        request = Request(
            self.status_url,
            headers={
                "Accept": "application/json",
                "Authorization": self.authorization,
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
        return decode_status_response(body)

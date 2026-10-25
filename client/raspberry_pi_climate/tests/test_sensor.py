import json
import unittest

from sensor import decode_status_response


def make_status(sensor):
    return json.dumps(
        {
            "client": {
                "heaterEnabled": True,
                "controlReason": "automatic_control",
                "temperatureControl": {"targetTemperatureC": 21.0},
            },
            "sensors": [
                sensor,
                {
                    "id": "battery-voltage",
                    "type": "battery",
                    "status": "on",
                    "voltageV": 53.42,
                },
            ],
            "actuators": [
                {
                    "id": "infrared-heater-relay",
                    "type": "relay",
                    "status": "off",
                }
            ],
        }
    ).encode("utf-8")


class Esp32StatusTests(unittest.TestCase):
    def test_decodes_fresh_dht20_reading(self):
        response = make_status(
            {
                "id": "room-climate",
                "name": "Temperatur und Luftfeuchtigkeit",
                "type": "DHT20",
                "status": "on",
                "temperatureC": 22.45,
                "relativeHumidityPercent": 48.2,
            }
        )

        self.assertEqual(
            decode_status_response(response),
            {
                "temperatureC": 22.45,
                "humidityPercent": 48.2,
                "batteryVoltageV": 53.42,
                "heater": {
                    "relayStatus": "off",
                    "enabled": True,
                    "controlReason": "automatic_control",
                    "targetTemperatureC": 21.0,
                },
            },
        )

    def test_finds_dht20_by_type_when_id_is_different(self):
        response = make_status(
            {
                "id": "sensor-2",
                "type": "DHT20",
                "status": "on",
                "temperatureC": 20.0,
                "relativeHumidityPercent": 50.0,
            }
        )

        self.assertEqual(
            decode_status_response(response)["temperatureC"], 20.0
        )

    def test_rejects_stale_sensor_reading(self):
        response = make_status(
            {
                "id": "room-climate",
                "type": "DHT20",
                "status": "off",
                "temperatureC": None,
                "relativeHumidityPercent": None,
            }
        )
        with self.assertRaisesRegex(RuntimeError, "fresh reading"):
            decode_status_response(response)

    def test_rejects_missing_dht20_sensor(self):
        with self.assertRaisesRegex(RuntimeError, "no DHT20"):
            decode_status_response(make_status({"id": "other", "type": "relay"}))

    def test_rejects_invalid_humidity(self):
        response = make_status(
            {
                "id": "room-climate",
                "type": "DHT20",
                "status": "on",
                "temperatureC": 20.0,
                "relativeHumidityPercent": 101.0,
            }
        )
        with self.assertRaisesRegex(RuntimeError, "humidity"):
            decode_status_response(response)

    def test_ignores_stale_battery_voltage(self):
        payload = json.loads(
            make_status(
                {
                    "id": "room-climate",
                    "type": "DHT20",
                    "status": "on",
                    "temperatureC": 20.0,
                    "relativeHumidityPercent": 50.0,
                }
            )
        )
        payload["sensors"][1].update(status="off", voltageV=None)

        self.assertIsNone(
            decode_status_response(json.dumps(payload).encode("utf-8"))[
                "batteryVoltageV"
            ]
        )

    def test_accepts_status_without_battery_sensor(self):
        payload = json.loads(
            make_status(
                {
                    "id": "room-climate",
                    "type": "DHT20",
                    "status": "on",
                    "temperatureC": 20.0,
                    "relativeHumidityPercent": 50.0,
                }
            )
        )
        payload["sensors"] = payload["sensors"][:1]

        self.assertIsNone(
            decode_status_response(json.dumps(payload).encode("utf-8"))[
                "batteryVoltageV"
            ]
        )

    def test_rejects_invalid_fresh_battery_voltage(self):
        payload = json.loads(
            make_status(
                {
                    "id": "room-climate",
                    "type": "DHT20",
                    "status": "on",
                    "temperatureC": 20.0,
                    "relativeHumidityPercent": 50.0,
                }
            )
        )
        payload["sensors"][1]["voltageV"] = "53.4"
        with self.assertRaisesRegex(RuntimeError, "battery voltage"):
            decode_status_response(json.dumps(payload).encode("utf-8"))

    def test_rejects_invalid_json(self):
        with self.assertRaisesRegex(RuntimeError, "invalid JSON"):
            decode_status_response(b"{")

    def test_rejects_missing_heater_state(self):
        payload = json.loads(make_status(
            {
                "id": "room-climate",
                "type": "DHT20",
                "status": "on",
                "temperatureC": 20.0,
                "relativeHumidityPercent": 50.0,
            }
        ))
        payload["actuators"] = []
        with self.assertRaisesRegex(RuntimeError, "heater relay"):
            decode_status_response(json.dumps(payload).encode("utf-8"))


if __name__ == "__main__":
    unittest.main()

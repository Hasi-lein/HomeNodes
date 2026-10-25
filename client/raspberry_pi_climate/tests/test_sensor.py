import json
import unittest

from sensor import decode_status_response


def make_status(sensor):
    return json.dumps({"sensors": [sensor]}).encode("utf-8")


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

        self.assertEqual(decode_status_response(response), (22.45, 48.2))

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

        self.assertEqual(decode_status_response(response), (20.0, 50.0))

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

    def test_rejects_invalid_json(self):
        with self.assertRaisesRegex(RuntimeError, "invalid JSON"):
            decode_status_response(b"{")


if __name__ == "__main__":
    unittest.main()

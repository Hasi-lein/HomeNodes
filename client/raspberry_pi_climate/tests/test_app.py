import json
import tempfile
import time
import unittest
from datetime import datetime, timezone
from pathlib import Path

from app import JsonLinesStore, create_app


def make_record(sampled_at, temperature=21.25, humidity=48.5):
    return {
        "sampledAt": datetime.fromtimestamp(
            sampled_at, tz=timezone.utc
        ).isoformat(timespec="seconds"),
        "temperatureC": temperature,
        "humidityPercent": humidity,
    }


class FakeClimateApi:
    def __init__(self):
        self.target_temperature = 21.0

    def set_target_temperature(self, target_temperature_c):
        self.target_temperature = target_temperature_c
        return {
            "relayStatus": "off",
            "enabled": True,
            "controlReason": "automatic_control",
            "targetTemperatureC": target_temperature_c,
        }


class ClimateApiTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.file_path = str(
            Path(self.temporary_directory.name) / "readings.jsonl"
        )
        self.store = JsonLinesStore(self.file_path)
        self.app = create_app(self.file_path, "Test-Raum", self.store)
        self.client = self.app.test_client()

    def tearDown(self):
        self.temporary_directory.cleanup()

    def test_status_reports_waiting_when_no_sample_was_taken(self):
        response = self.client.get("/api/v1/status")

        self.assertEqual(response.status_code, 200)
        data = response.get_json()
        self.assertEqual(data["client"]["status"], "waiting")
        self.assertEqual(data["client"]["sensorName"], "Test-Raum")
        self.assertIsNone(data["reading"])

    def test_status_returns_latest_appended_reading(self):
        record = make_record(time.time())
        record["heater"] = {
            "relayStatus": "off",
            "enabled": True,
            "controlReason": "automatic_control",
            "targetTemperatureC": 21.0,
        }
        self.store.append(record)

        response = self.client.get("/api/v1/status")

        self.assertEqual(response.status_code, 200)
        data = response.get_json()
        self.assertEqual(data["reading"]["temperatureC"], 21.25)
        self.assertEqual(data["reading"]["humidityPercent"], 48.5)
        self.assertEqual(data["heater"]["relayStatus"], "off")
        self.assertEqual(data["heater"]["targetTemperatureC"], 21.0)

    def test_graph_returns_individual_saved_measurements(self):
        sampled_at = time.time() - 5
        self.store.append(make_record(sampled_at, 20.0, 45.0))
        self.store.append(make_record(sampled_at + 1, 21.0, 46.0))

        response = self.client.get("/api/v1/readings?hours=1")

        self.assertEqual(response.status_code, 200)
        points = response.get_json()["points"]
        self.assertEqual(len(points), 2)
        self.assertEqual(points[0]["temperatureC"], 20.0)
        self.assertEqual(points[1]["temperatureC"], 21.0)

    def test_graph_rejects_unsupported_range(self):
        response = self.client.get("/api/v1/readings?hours=2")

        self.assertEqual(response.status_code, 400)

    def test_target_temperature_is_forwarded_to_esp_and_returned(self):
        climate_api = FakeClimateApi()
        self.app.extensions["climate_sensor"] = climate_api

        response = self.client.post(
            "/api/v1/heater/target",
            json={"targetTemperatureC": 22.5},
        )

        self.assertEqual(response.status_code, 200)
        self.assertEqual(climate_api.target_temperature, 22.5)
        self.assertEqual(
            response.get_json()["heater"]["targetTemperatureC"], 22.5
        )

    def test_target_temperature_rejects_out_of_range_value(self):
        climate_api = FakeClimateApi()
        self.app.extensions["climate_sensor"] = climate_api

        response = self.client.post(
            "/api/v1/heater/target",
            json={"targetTemperatureC": 36},
        )

        self.assertEqual(response.status_code, 400)
        self.assertEqual(climate_api.target_temperature, 21.0)

    def test_target_temperature_rejects_unknown_fields(self):
        response = self.client.post(
            "/api/v1/heater/target",
            json={"targetTemperatureC": 22, "heaterEnabled": True},
        )

        self.assertEqual(response.status_code, 400)

    def test_store_compacts_in_batches_and_retains_newest_records(self):
        store = JsonLinesStore(self.file_path, max_records=3, compaction_batch=2)
        for index in range(5):
            store.append(
                make_record(time.time() + index, temperature=float(index))
            )

        records = [
            json.loads(line)
            for line in Path(self.file_path).read_text().splitlines()
        ]
        self.assertEqual(len(records), 3)
        self.assertEqual(
            [record["temperatureC"] for record in records], [2.0, 3.0, 4.0]
        )

    def test_store_recovers_latest_records_after_restart(self):
        self.store.append(make_record(time.time()))

        restarted_store = JsonLinesStore(self.file_path)

        self.assertEqual(restarted_store.latest(), self.store.latest())


if __name__ == "__main__":
    unittest.main()

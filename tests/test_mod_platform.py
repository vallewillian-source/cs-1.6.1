"""Contract/failure tests that do not need Valve assets, a display or an API key."""
from __future__ import annotations
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SIDECAR = ROOT / "mods/bots-jev/sidecar"
sys.path.insert(0, str(SIDECAR))
import brain
import mailbox
import typesafe

spec = importlib.util.spec_from_file_location("supervisor", ROOT / "tools/modhost/supervisor.py")
supervisor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(supervisor)


def observation(**changes):
    result = dict(v=2, session="test", map="de_dust2", round=1, revision=1,
                  observed_at=time.monotonic(), team="T", phase="freeze", bomb="carried", bomb_zone="none",
                  alive_us=4, alive_them=4, players_us=4, players_them=4, money_avg_us=4500, money_avg_them=4500,
                  score_us=0, score_them=0, history="")
    result.update(changes)
    return result


class MailboxTests(unittest.TestCase):
    def test_wire_alignment_and_session_generation_age(self):
        self.assertEqual(mailbox.HEADER.size, 88)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sample.bin"
            mailbox.write(path, {"round": 3}, "test", 7)
            self.assertEqual(mailbox.read(path, "test", 7), {"round": 3})
            self.assertIsNone(mailbox.read(path, "other-session", 7))
            self.assertIsNone(mailbox.read(path, "test", 8))
            with patch.object(mailbox.time, "monotonic", return_value=time.monotonic() + 11):
                self.assertIsNone(mailbox.read(path, "test", 7))
            path.write_bytes(b"partially copied")
            self.assertIsNone(mailbox.read(path, "test", 7))

    def test_reject_future_and_oversized_publications(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sample.bin"
            with patch.object(mailbox.time, "monotonic", return_value=time.monotonic() + 100):
                mailbox.write(path, {}, "test", 1)
            self.assertIsNone(mailbox.read(path, "test", 1))
            with self.assertRaises(ValueError):
                mailbox.write(path, {"data": "x" * 70000}, "test", 1)


class AdvisorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.state = Path(self.temp.name)
        self.brain = brain.Brain(self.state, "test", 1)

    def tearDown(self):
        self.temp.cleanup()

    def put_observation(self, obs):
        mailbox.write(self.state / "channels/observe-t.bin", obs, "test", 1)

    def test_rate_limited_observation_remains_pending(self):
        obs = observation()
        self.put_observation(obs)
        self.brain.last_call["T"] = time.monotonic()
        self.brain.tick()
        self.assertIsNone(self.brain.last_observation["T"])
        self.brain.last_call["T"] = 0
        self.brain.cache[brain.signature("T", obs)] = "RUSH_B"
        self.brain.tick()
        plan = mailbox.read(self.state / "channels/plan-t.bin", "test", 1)
        self.assertEqual(plan["intent"], "RUSH_B")
        self.assertEqual(plan["revision"], 1)

    def test_late_model_response_never_drives_next_revision(self):
        obs = observation()
        self.put_observation(obs)
        def answer(**_):
            self.put_observation(observation(revision=2))
            return dict(choice="RUSH_B", latency_ms=20, usage={})
        with patch.object(typesafe, "choose_operation", side_effect=answer):
            self.assertTrue(self.brain.decide("T", obs))
        self.assertFalse((self.state / "channels/plan-t.bin").exists())

    def test_invalid_cache_entry_cannot_bypass_intent_validation(self):
        obs = observation()
        self.brain.cache[brain.signature("T", obs)] = "RUN_ARBITRARY_COMMAND"
        self.brain.no_model = True
        self.brain.decide("T", obs)
        self.assertFalse((self.state / "channels/plan-t.bin").exists())

    def test_heartbeat_history_does_not_cause_inference_feedback_loop(self):
        obs = observation()
        self.put_observation(obs)
        with patch.object(typesafe, "choose_operation", return_value=dict(choice="RUSH_B", latency_ms=10, usage={})) as model:
            self.brain.decide("T", obs)
            newer = dict(obs, observed_at=time.monotonic(), history="RUSH_B")
            self.put_observation(newer)
            self.brain.decide("T", newer)
            self.assertEqual(model.call_count, 1)

    def test_empty_teams_do_not_trigger_model_calls(self):
        self.assertFalse(brain.validate_observation(observation(players_them=0), "test", "T"))

    def test_model_error_is_rate_limited_and_leaves_no_plan(self):
        obs = observation()
        with patch.object(typesafe, "choose_operation", side_effect=RuntimeError("offline")) as model:
            self.assertFalse(self.brain.decide("T", obs))
            self.assertFalse(self.brain.decide("T", obs))
            self.assertEqual(model.call_count, 1)
        self.assertFalse((self.state / "channels/plan-t.bin").exists())

    def test_session_map_and_old_observations_rejected(self):
        for obs in (observation(session="old"), observation(map="cs_office"), observation(observed_at=time.monotonic()-11)):
            self.assertFalse(brain.validate_observation(obs, "test", "T"))

    def test_transport_and_invalid_response_failures_are_distinct(self):
        obs = observation()
        for error, counter in ((typesafe.TransportError("offline"), "transport_errors"),
                               (ValueError("invalid probabilities"), "invalid_responses")):
            self.brain.last_call["T"] = 0
            with patch.object(typesafe, "choose_operation", side_effect=error):
                self.assertFalse(self.brain.decide("T", obs))
            self.assertEqual(self.brain.stats[counter], 1)
        self.assertEqual(self.brain.stats["errors"], 2)

    def test_probability_validation_rejects_boolean_nan_and_wrong_winner(self):
        for answer in (
            dict(choice="a", probabilities={"a": True, "b": False}, confidence=1),
            dict(choice="a", probabilities={"a": float("nan"), "b": 0}, confidence=1),
            dict(choice="a", probabilities={"a": .1, "b": .9}, confidence=.9),
        ):
            with self.assertRaises(ValueError):
                typesafe._validate_choice(answer, {"a", "b"})


class PackageTests(unittest.TestCase):
    def test_component_cannot_escape_package(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "example"
            package.mkdir()
            (package / "mod.json").write_text(json.dumps(dict(manifest_version=1, id="example", components={"client": "../other.so"})))
            self.assertIn("fora do pacote", supervisor.packages(root)["example"]["error"])

    def test_packaged_files_and_components_are_independent(self):
        mods = supervisor.packages(ROOT / "mods")
        self.assertEqual(set(mods), {"bots-jev", "x-ray-debug", "vr", "null"})
        self.assertIn("server", mods["bots-jev"]["components"])
        self.assertIn("server", mods["x-ray-debug"]["components"])
        self.assertNotIn("sidecar", mods["x-ray-debug"]["components"])


class NativeTests(unittest.TestCase):
    def test_real_xray_filters_players_crouch_and_expired_snapshots(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / "xray"
            subprocess.run(["c++", "-std=c++17", "-I"+str(ROOT/"sdk/include"),
                            str(ROOT/"tests/mods/xray.cpp"), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_real_advisor_rejects_stale_plans_expires_and_releases_bots(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / "advisor"
            subprocess.run(["c++", "-std=c++17", "-O0", "-I"+str(ROOT/"sdk/include"),
                            "-I"+str(ROOT/"third_party/json"), str(ROOT/"tests/mods/advisor.cpp"),
                            str(ROOT/"mods/bots-jev/server/advisor.cpp"), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_projection_and_cross_arch_snapshot_layout(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / "projection"
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-I"+str(ROOT/"sdk/include"),
                            "-I"+str(ROOT/"mods/x-ray-debug"), str(ROOT/"tests/mods/projection.cpp"), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_runtime_conflict_background_snapshot_and_log_retention(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / "runtime"
            subprocess.run(["c++", "-std=c++17", "-O0", "-pthread", "-I"+str(ROOT/"sdk/include"),
                            "-I"+str(ROOT/"sdk/host"), "-I"+str(ROOT/"third_party/json"),
                            str(ROOT/"tests/mods/runtime.cpp"), str(ROOT/"sdk/host/runtime.cpp"), "-ldl", "-o", str(exe)], check=True)
            subprocess.run([str(exe), directory], check=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
